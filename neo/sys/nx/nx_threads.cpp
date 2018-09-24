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

#include <SDL_version.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
#include <time.h>

#include "sys/platform.h"
#include "framework/Common.h"

#include "sys/sys_public.h"

#include <threads.h>

static mtx_t	mutex[MAX_CRITICAL_SECTIONS] = { };
static cnd_t		cond[MAX_TRIGGER_EVENTS] = { };
static bool			signaled[MAX_TRIGGER_EVENTS] = { };
static bool			waiting[MAX_TRIGGER_EVENTS] = { };

static xthreadInfo	*thread[MAX_THREADS] = { };
static size_t		thread_count = 0;

/*
==============
Sys_Sleep
==============
*/
void Sys_Sleep(int msec) {
	if (msec <= 0) return;

	const struct timespec ts = {
		.tv_sec = (time_t)msec / 1000,
		.tv_nsec = ((long)msec % 1000) * 1000000,
	};

	thrd_sleep(&ts, NULL);
}

/*
================
Sys_Milliseconds
================
*/
unsigned int Sys_Milliseconds() {
	return SDL_GetTicks();
}

/*
==================
Sys_InitThreads
==================
*/
void Sys_InitThreads() {
	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
		int ret = mtx_init(mutex + i, mtx_plain);

		if (ret != thrd_success) {
			Sys_Printf("ERROR: mtx_init failed: %d\n", ret);
			return;
		}
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
		int ret = cnd_init(cond + i);

		if (ret != thrd_success) {
			Sys_Printf("ERROR: cnd_init failed: %d\n", ret);
			return;
		}

		signaled[i] = false;
		waiting[i] = false;
	}

	// threads
	for (int i = 0; i < MAX_THREADS; i++)
		thread[i] = NULL;

	thread_count = 0;
}

/*
==================
Sys_ShutdownThreads
==================
*/
void Sys_ShutdownThreads() {
	// threads
	for (int i = 0; i < MAX_THREADS; i++) {
		if (!thread[i])
			continue;
		Sys_Printf("WARNING: Thread '%s' still running\n", thread[i]->name);
		// thrd_detach(*(thread[i]->threadHandle));
		thread[i] = NULL;
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
		cnd_destroy(cond + i);
		signaled[i] = false;
		waiting[i] = false;
	}

	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++)
		mtx_destroy(mutex + i);
}

/*
==================
Sys_EnterCriticalSection
==================
*/
void Sys_EnterCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

	if (mtx_lock(mutex + index) != thrd_success)
		common->Error("ERROR: mtx_lock failed\n");
}

/*
==================
Sys_LeaveCriticalSection
==================
*/
void Sys_LeaveCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

	if (mtx_unlock(mutex + index) != thrd_success)
		common->Error("ERROR: mtx_unlock failed\n");
}

/*
======================================================
wait and trigger events
we use a single lock to manipulate the conditions, CRITICAL_SECTION_SYS

the semantics match the win32 version. signals raised while no one is waiting stay raised until a wait happens (which then does a simple pass-through)

NOTE: we use the same mutex for all the events. I don't think this would become much of a problem
cond_wait unlocks atomically with setting the wait condition, and locks it back before exiting the function
the potential for time wasting lock waits is very low
======================================================
*/

/*
==================
Sys_WaitForEvent
==================
*/
void Sys_WaitForEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	assert(!waiting[index]);	// WaitForEvent from multiple threads? that wouldn't be good
	if (signaled[index]) {
		// emulate windows behaviour: signal has been raised already. clear and keep going
		signaled[index] = false;
	} else {
		waiting[index] = true;
		int ret = cnd_wait(cond + index, mutex + CRITICAL_SECTION_SYS);
		if (ret != thrd_success)
			common->Error("ERROR: cnd_wait failed %d\n", ret);
		waiting[index] = false;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

/*
==================
Sys_TriggerEvent
==================
*/
void Sys_TriggerEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	if (waiting[index]) {
		if (cnd_signal(cond + index) != thrd_success)
			common->Error("ERROR: cnd_signal failed\n");
	} else {
		// emulate windows behaviour: if no thread is waiting, leave the signal on so next wait keeps going
		signaled[index] = true;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

/*
==================
Sys_CreateThread
==================
*/
void Sys_CreateThread(xthread_t function, void *parms, xthreadInfo& info, const char *name) {
	static int thrd_id = 1;

	Sys_EnterCriticalSection();

	int ret = thrd_create(&(info.threadHandle), function, parms);
	if (ret != thrd_success) {
		common->Error("ERROR: thrd_create for '%s' failed: %d\n", name, ret);
		Sys_LeaveCriticalSection();
		return;
	}

	info.name = name;
	info.threadId = thrd_id++;

	if (thread_count < MAX_THREADS)
		thread[thread_count++] = &info;
	else
		common->DPrintf("WARNING: MAX_THREADS reached\n");

	Sys_LeaveCriticalSection();
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread(xthreadInfo& info) {
	thrd_join(info.threadHandle, NULL);

	info.name = NULL;
	info.threadId = 0;

	Sys_EnterCriticalSection();

	for (int i = 0; i < thread_count; i++) {
		if (&info == thread[i]) {
			thread[i] = NULL;

			int j;
			for (j = i + 1; j < thread_count; j++)
				thread[j - 1] = thread[j];

			thread[j - 1] = NULL;
			thread_count--;

			break;
		}
	}

	Sys_LeaveCriticalSection( );
}

/*
==================
Sys_GetThreadName
find the name of the calling thread
==================
*/
const char *Sys_GetThreadName(int *index) {
	const char *name;

	Sys_EnterCriticalSection();

	thrd_t cur = thrd_current();

	if (cur) {
		for (int i = 0; i < thread_count; i++) {
			if (thread[i] && thrd_equal(cur, thread[i]->threadHandle)) {
				if (index)
					*index = i;

				name = thread[i]->name;

				Sys_LeaveCriticalSection();

				return name;
			}
		}
	}

	if (index)
		*index = -1;

	Sys_LeaveCriticalSection();

	return "main";
}

// timer stuff //

static thrd_t t_thread;
static unsigned int t_interval = 0;
static void *t_parms = NULL;
static xtimerfunc_t t_callback = NULL;
static bool t_exit = false;

static int SysTimerThread( void *param ) {
	unsigned int interval = t_interval;
	unsigned int scheduled = Sys_Milliseconds() + interval;
	unsigned int ret, tick;

	while (!t_exit) {
		tick = Sys_Milliseconds();

		if (tick < scheduled)
			Sys_Sleep(scheduled - tick);

		ret = t_callback(interval, NULL);
		if (!ret) break;

		interval = ret;
		scheduled = tick + interval;
	}

	return 0;
}

int Sys_StartTimer( unsigned int interval, xtimerfunc_t function, void *parms ) {
	t_parms = parms;
	t_interval = interval;
	t_callback = function;
	t_exit = false;
	return thrd_create(&t_thread, SysTimerThread, (void *)&t_exit) == thrd_success;
}

void Sys_StopTimer( void ) {
	t_exit = true;
	thrd_join(t_thread, NULL);
}
