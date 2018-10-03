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
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "sys/platform.h"
#include "framework/Licensee.h"
#include "framework/FileSystem.h"
#include "sys/nx/nx_public.h"
#include "sys/sys_local.h"

#include <locale.h>

extern "C" u32 __nx_nv_transfermem_size = 0x800000;

static char path_argv[MAX_OSPATH];

bool Sys_GetPath(sysPath_t type, idStr &path) {
	path.Clear();

	switch(type) {
	case PATH_BASE:
	case PATH_CONFIG:
	case PATH_SAVE:
		path = BUILD_DATADIR;
		return true;

	case PATH_EXE:
		if (path_argv[0] != 0) {
			path = path_argv;
			return true;
		}

		return false;
	}
	return true;
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	NX_Shutdown();
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	// use a syscall to get available memory
	u64 avail = 0;

	// id0 = 6, id1 = 0 => TotalMemoryAvailable
	Result rc = svcGetInfo( &avail, 6, CUR_PROCESS_HANDLE, 0 );

	// applets get at least like 300 mb
	if ( R_FAILED(rc) ) avail = 304 * 1024 * 1024;

	int mb = (int)( avail / ( 1024 * 1024 ) );
	// round to the nearest 16Mb
	mb = ( mb + 8 ) & ~15;
	return mb;
}

/*
==================
Sys_DoStartProcess
if we don't fork, this function never returns
the no-fork lets you keep the terminal when you're about to spawn an installer

if the command contains spaces, system() is used. Otherwise the more straightforward execl ( system() blows though )
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {
	printf( "Sys_DoStartProcess: unimplemented\n" );
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	static bool	quit_spamguard = false;

	if ( quit_spamguard ) {
		common->DPrintf( "Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url );
		return;
	}

	printf( "Sys_OpenURL: unimplemented\n" );

	if ( quit ) quit_spamguard = true;

	// execute this just for the quit side effect
	sys->StartProcess( "wewlad", quit );
}

/*
===============
main
===============
*/
#define MAX_FAKEARGS 32

int main(int argc, char **argv) {
	socketInitializeDefault();
#if defined(DEBUG) || defined(NXLINK_DEBUG)
	nxlinkStdio();
#endif

	strncpy( path_argv, "/switch/dhewm3", MAX_OSPATH );

	// dirtily inject args if needed
	static char *fake_argv[MAX_FAKEARGS + 1] = { NULL };
	int fake_argc = argc;
	for (int i = 0; i < argc && i < MAX_FAKEARGS; ++i)
		fake_argv[i] = argv[i];
#ifdef _D3XP
	// load RoE by default if present, until I can figure out a better way to do this
	if (fake_argc + 3 < MAX_FAKEARGS) {
		fake_argv[fake_argc++] = (char *)"+set";
		fake_argv[fake_argc++] = (char *)"fs_game";
		fake_argv[fake_argc++] = (char *)"d3xp";
	}
#endif
#if defined(DEBUG) || defined(NXLINK_DEBUG)
	// enable developer mode when debug is enabled
	if (fake_argc + 3 < MAX_FAKEARGS) {
		fake_argv[fake_argc++] = (char *)"+set";
		fake_argv[fake_argc++] = (char *)"developer";
		fake_argv[fake_argc++] = (char *)"1";
	}
#endif
	fake_argv[fake_argc] = NULL;

	if ( fake_argc > 1 ) {
		common->Init( fake_argc-1, &fake_argv[1] );
	} else {
		common->Init( 0, NULL );
	}

	while ( appletMainLoop() ) {
		common->Frame();
	}
	return 0;
}
