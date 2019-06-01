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
#include <SDL.h>
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

bool Sys_GetPath(sysPath_t type, idStr &path) {
	path.Clear();

	switch(type) {
	case PATH_BASE:
	case PATH_CONFIG:
	case PATH_SAVE:
		path = BUILD_DATADIR;
		return true;
	case PATH_EXE:
		path = ".";
		return true;
	}

	return false;
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

#ifdef _D3XP
static int fake_argc = 3;
static char *fake_argv[] = {
  (char *)"+set",
  (char *)"fs_game",
  (char *)"d3xp",
  nullptr
};
#else
static int fake_argc = 0;
static char *fake_argv[] = {
  nullptr
};
#endif

int main(int argc, char **argv) {
	pcvInitialize();
	socketInitializeDefault();
#if defined(DEBUG) || defined(NXLINK_DEBUG)
	nxlinkStdio();
#endif

	common->Init( fake_argc, fake_argv );

	NX_UpdateOverclock();

	appletLockExit();

	while ( appletMainLoop() ) {
		common->Frame();
	}

	// we drop from the main loop when the user quits via the HOME button
	// because of appletLockExit, so we gotta deinit shit properly

	printf("clean exit\n");
	common->Quit();

	return 0;
}
