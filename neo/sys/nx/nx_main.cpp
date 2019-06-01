/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include <switch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/time.h>

#include "sys/platform.h"
#include "idlib/containers/StrList.h"
#include "framework/FileSystem.h"
#include "framework/KeyInput.h"
#include "framework/EditField.h"
#include "sys/sys_local.h"

#include "sys/nx/nx_public.h"

#define					COMMAND_HISTORY 64

static int				input_hide = 0;

idEditField				input_field;
static char				input_ret[256];

static idStr			history[ COMMAND_HISTORY ];	// cycle buffer
static int				history_count = 0;			// buffer fill up
static int				history_start = 0;			// current history start
static int				history_current = 0;			// goes back in history
idEditField				history_backup;				// the base edit line

// terminal support
idCVar in_tty( "in_tty", "1", CVAR_BOOL | CVAR_INIT | CVAR_SYSTEM, "terminal tab-completion and history" );

static bool				tty_enabled = false;

// pid - useful when you attach to gdb..
idCVar com_pid( "com_pid", "0", CVAR_INTEGER | CVAR_INIT | CVAR_SYSTEM, "process id" );

// exit - quit - error --------------------------------------------------------

static int set_exit = 0;
static char exit_spawn[ 1024 ] = { 0 };

// overclock mode stuff

idCVar nx_overclock(
	"nx_overclock", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE,
	"Switch overclock level: 0 - none, 1 - low, 2 - med, 3 - max"
);

static bool clock_changed = false;
static u32 old_clock_gpu, old_clock_cpu, old_clock_emc;
static constexpr u32 oc_clock_cpu[] = { 1224000000, 1683000000, 1785000000 };
static constexpr u32 oc_clock_gpu[] = { 691200000, 768000000, 768000000 };
static constexpr u32 oc_clock_emc[] = { 1600000000, 1600000000, 1600000000 };

/*
================
NX_UpdateOverclock
================
*/
static inline void NX_ClearOverclock(void) {
	if (clock_changed) {
		// set old clocks
		pcvSetClockRate(PcvModule_CpuBus, old_clock_cpu);
		pcvSetClockRate(PcvModule_GPU, old_clock_gpu);
		pcvSetClockRate(PcvModule_EMC, old_clock_emc);
		clock_changed = false;
	}
}

static inline void NX_SetOverclock(int oclevel) {
	common->Printf( "NX_SetOverclock(%d): setting clocks to %u / %u / %u\n",
		oclevel,
		oc_clock_cpu[oclevel],
		oc_clock_gpu[oclevel],
		oc_clock_emc[oclevel]
	);
	// preserve old clocks to turn shit back off later
	pcvGetClockRate(PcvModule_CpuBus, &old_clock_cpu);
	pcvGetClockRate(PcvModule_GPU, &old_clock_gpu);
	pcvGetClockRate(PcvModule_EMC, &old_clock_emc);
	// set OC clocks
	pcvSetClockRate(PcvModule_CpuBus, oc_clock_cpu[oclevel]);
	pcvSetClockRate(PcvModule_GPU, oc_clock_gpu[oclevel]);
	pcvSetClockRate(PcvModule_EMC, oc_clock_emc[oclevel]);
	clock_changed = true;
}

void NX_UpdateOverclock(void) {
	// get the cvar
	int oclevel = cvarSystem->GetCVarInteger( "nx_overclock" ) - 1;
	if (oclevel >= 0 && oclevel < 3)
		NX_SetOverclock(oclevel);
	else
		NX_ClearOverclock();
}

/*
================
NX_Exit
================
*/
void NX_Exit(int ret) {
	// clock back to old values, just in case
	NX_ClearOverclock();
	pcvExit();
	socketExit();
	appletUnlockExit();
	// in case of signal, handler tries a common->Quit
	// we use set_exit to maintain a correct exit code
	if ( set_exit ) {
		exit( set_exit );
	}
	exit( ret );
}

/*
================
NX_SetExit
================
*/
void NX_SetExit(int ret) {
	set_exit = 0;
}

/*
===============
NX_SetExitSpawn
set the process to be spawned when we quit
===============
*/
void NX_SetExitSpawn( const char *exeName ) {
	idStr::Copynz( exit_spawn, exeName, 1024 );
}

/*
==================
idSysLocal::StartProcess
if !quit, start the process asap
otherwise, push it for execution at exit
(i.e. let complete shutdown of the game and freeing of resources happen)
NOTE: might even want to add a small delay?
==================
*/
void idSysLocal::StartProcess( const char *exeName, bool quit ) {
	if ( quit ) {
		common->DPrintf( "Sys_StartProcess %s (delaying until final exit)\n", exeName );
		NX_SetExitSpawn( exeName );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		return;
	}

	common->DPrintf( "Sys_StartProcess %s\n", exeName );
	Sys_DoStartProcess( exeName );
}

/*
================
Sys_Quit
================
*/
void Sys_Quit(void) {
	NX_Exit( EXIT_SUCCESS );
}

/*
================
Sys_Mkdir
================
*/
void Sys_Mkdir( const char *path ) {
	mkdir(path, 0777);
}

/*
================
Sys_ListFiles
================
*/
int Sys_ListFiles( const char *directory, const char *extension, idStrList &list ) {
	struct dirent *d;
	DIR *fdir;
	bool dironly = false;
	char search[MAX_OSPATH];
	struct stat st;
	bool debug;

	list.Clear();

	debug = cvarSystem->GetCVarBool( "fs_debug" );

	if (!extension)
		extension = "";

	// passing a slash as extension will find directories
	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		dironly = true;
	}

	// search
	// NOTE: case sensitivity of directory path can screw us up here
	if ((fdir = opendir(directory)) == NULL) {
		if (debug) {
			common->Printf("Sys_ListFiles: opendir %s failed\n", directory);
		}
		return -1;
	}

	while ((d = readdir(fdir)) != NULL) {
		idStr::snPrintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
			continue;
		if (!dironly) {
			idStr look(search);
			idStr ext;
			look.ExtractFileExtension(ext);
			if (extension[0] != '\0' && ext.Icmp(&extension[1]) != 0) {
				continue;
			}
		}
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;

		list.Append(d->d_name);
	}

	closedir(fdir);

	if ( debug ) {
		common->Printf( "Sys_ListFiles: %d entries in %s\n", list.Num(), directory );
	}

	return list.Num();
}

/*
================
NX_Cwd
================
*/
const char *NX_Cwd( void ) {
	static char cwd[MAX_OSPATH];

	if (getcwd( cwd, sizeof( cwd ) - 1 ))
		cwd[MAX_OSPATH-1] = 0;
	else
		cwd[0] = 0;

	return cwd;
}

/*
=================
Sys_Init
=================
*/
void Sys_Init( void ) {
	
	NX_InitConsoleInput();
	com_pid.SetInteger( getpid() );
	common->Printf( "pid: %d\n", com_pid.GetInteger() );
	common->Printf( "%d MB System Memory\n", Sys_GetSystemRam() );
}

/*
=================
NX_Shutdown
=================
*/
void NX_Shutdown( void ) {
	for ( int i = 0; i < COMMAND_HISTORY; i++ ) {
		history[ i ].Clear();
	}
	// pcvExit();
	// socketExit();
}

/*
=================
Sys_DLL_Load
TODO: OSX - use the native API instead? NSModule
=================
*/
uintptr_t Sys_DLL_Load( const char *path ) {
	return (uintptr_t)NULL;
}

/*
=================
Sys_DLL_GetProcAddress
=================
*/
void* Sys_DLL_GetProcAddress( uintptr_t handle, const char *sym ) {
	return NULL;
}

/*
=================
Sys_DLL_Unload
=================
*/
void Sys_DLL_Unload( uintptr_t handle ) {
	// NOP
}

/*
================
Sys_ShowConsole
================
*/
void Sys_ShowConsole( int visLevel, bool quitOnClose ) { }

// ---------------------------------------------------------------------------

ID_TIME_T Sys_FileTimeStamp(FILE * fp) {
	struct stat st;
	fstat(fileno(fp), &st);
	return st.st_mtime;
}

char *Sys_GetClipboardData(void) {
	Sys_Printf( "TODO: Sys_GetClipboardData\n" );
	return NULL;
}

void Sys_SetClipboardData( const char *string ) {
	Sys_Printf( "TODO: Sys_SetClipboardData\n" );
}

/*
================
Sys_LockMemory
================
*/
bool Sys_LockMemory( void *ptr, int bytes ) {
	return true;
}

/*
================
Sys_UnlockMemory
================
*/
bool Sys_UnlockMemory( void *ptr, int bytes ) {
	return true;
}

/*
================
Sys_SetPhysicalWorkMemory
================
*/
void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
	common->DPrintf( "TODO: Sys_SetPhysicalWorkMemory\n" );
}

/*
===========
Sys_GetDriveFreeSpace
return in MegaBytes
===========
*/
int Sys_GetDriveFreeSpace( const char *path ) {
	common->DPrintf( "TODO: Sys_GetDriveFreeSpace\n" );
	return 1000 * 1024;
}

/*
===============
NX_InitConsoleInput
===============
*/
void NX_InitConsoleInput( void ) {
	common->StartupVariable( "in_tty", false );
}

/*
================
Sys_ConsoleInput
Checks for a complete line of text typed in at the console.
Return NULL if a complete line is not ready.
================
*/
char *Sys_ConsoleInput( void ) {
	return NULL;
}

/*
===============
low level output
===============
*/

void Sys_DebugPrintf( const char *fmt, ... ) {
	va_list argptr;

	va_start( argptr, fmt );
	vprintf( fmt, argptr );
	va_end( argptr );
}

void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	vprintf( fmt, arg );
}

void Sys_Printf(const char *msg, ...) {
	va_list argptr;

	va_start( argptr, msg );
	vprintf( msg, argptr );
	va_end( argptr );
}

void Sys_VPrintf(const char *msg, va_list arg) {
	vprintf(msg, arg);
}

/*
================
Sys_Error
================
*/
void Sys_Error(const char *error, ...) {
	va_list argptr;

	Sys_Printf( "Sys_Error: " );
	va_start( argptr, error );
	Sys_DebugVPrintf( error, argptr );
	va_end( argptr );
	Sys_Printf( "\n" );

	NX_Exit( EXIT_FAILURE );
}
