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

#include <SDL.h>
#include <EGL/egl.h>
#include "glad41/glad.h"

#include "sys/platform.h"
#include "framework/Licensee.h"

#include "renderer/tr_local.h"

idCVar in_nograb("in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "prevents input grabbing");
idCVar r_waylandcompat("r_waylandcompat", "0", CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ARCHIVE, "wayland compatible framebuffer");

static bool grabbed = false;

static EGLDisplay s_display;
static EGLSurface s_surface;
static EGLContext s_context;
static NWindow *s_win;

/*
===============
switch egl stuff
===============
*/

static void SetMesaConfig(void) {
	// Uncomment below to disable error checking and save CPU time (useful for production):
	setenv("MESA_NO_ERROR", "1", 1);

	// Uncomment below to enable Mesa logging:
	// setenv("EGL_LOG_LEVEL", "debug", 1);
	// setenv("MESA_VERBOSE", "all", 1);
	// setenv("NOUVEAU_MESA_DEBUG", "1", 1);

	// Uncomment below to enable shader debugging in Nouveau:
	// setenv("NV50_PROG_OPTIMIZE", "0", 1);
	// setenv("NV50_PROG_DEBUG", "1", 1);
	// setenv("NV50_PROG_CHIPSET", "0x120", 1);
	// setenv("MESA_GL_VERSION_OVERRIDE", "3.2COMPAT", 1);
}

static bool InitEGL(NWindow *win) {
	SetMesaConfig();

	s_win = win;

	// Connect to the EGL default display
	s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!s_display) {
		common->Warning( "Could not connect to display! error: %d", eglGetError() );
		goto _fail0;
	}

	// Initialize the EGL display connection
	eglInitialize(s_display, NULL, NULL);

	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		common->Warning( "Could not set API! error: %d", eglGetError() );
		goto _fail1;
	}

	// Get an appropriate EGL framebuffer configuration
	EGLConfig config;
	EGLint numConfigs;
	static const EGLint attributeList[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	eglChooseConfig(s_display, attributeList, &config, 1, &numConfigs);
	if (numConfigs == 0) {
		common->Warning( "No config found! error: %d", eglGetError() );
		goto _fail1;
	}

	// Create an EGL window surface
	s_surface = eglCreateWindowSurface(s_display, config, win, NULL);
	if (!s_surface) {
		common->Warning( "Surface creation failed! error: %d", eglGetError() );
		goto _fail1;
	}

	static const EGLint ctxAttributeList[] =  {
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
		EGL_NONE
	};

	// Create an EGL rendering context
	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, ctxAttributeList);
	if (!s_context) {
		common->Warning( "Context creation failed! error: %d", eglGetError() );
		goto _fail2;
	}

	// Connect the context to the surface
	eglMakeCurrent(s_display, s_surface, s_surface, s_context);
	return true;

_fail2:
	eglDestroySurface(s_display, s_surface);
	s_surface = NULL;
_fail1:
	eglTerminate(s_display);
	s_display = NULL;
_fail0:
	s_win = NULL;
	return false;
}

static void DeinitEGL(void)
{
	if (s_display)
	{
		eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (s_context)
		{
			eglDestroyContext(s_display, s_context);
			s_context = NULL;
		}
		if (s_surface)
		{
			eglDestroySurface(s_display, s_surface);
			s_surface = NULL;
		}
		eglTerminate(s_display);
		s_display = NULL;
		s_win = NULL;
	}
}

/*
===================
GLimp_Init
===================
*/
bool GLimp_Init(glimpParms_t parms) {
	common->Printf("Initializing OpenGL subsystem\n");

	// assert(SDL_WasInit(SDL_INIT_VIDEO));

	int colorbits = 24;
	int depthbits = 24;
	int stencilbits = 8;

	if (!s_display) {
		if (!InitEGL(nwindowGetDefault()))
			common->Warning("Could not init EGL: %s\n", eglGetError());
		gladLoadGL();
	}

	eglSwapInterval(s_display, r_swapInterval.GetInteger());

	glConfig.vidWidth = 1280;
	glConfig.vidHeight = 720;
	glConfig.isFullscreen = true;

	if (s_win) nwindowSetCrop(s_win, 0, 0, glConfig.vidWidth, glConfig.vidHeight);

	common->Printf("Using %d color bits, %d depth, %d stencil display\n",
					colorbits, depthbits, stencilbits);

	glConfig.colorBits = colorbits;
	glConfig.depthBits = depthbits;
	glConfig.stencilBits = stencilbits;

	glConfig.displayFrequency = 0;

	if (!s_display) {
		common->Warning("No usable GL mode found: %s", SDL_GetError());
		return false;
	}

	return true;
}

/*
===================
GLimp_SetScreenParms
===================
*/
bool GLimp_SetScreenParms(glimpParms_t parms) {
	common->DPrintf("TODO: GLimp_ActivateContext\n");
	return true;
}

/*
===================
GLimp_Shutdown
===================
*/
void GLimp_Shutdown() {
	common->Printf("Shutting down OpenGL subsystem\n");
	DeinitEGL();
}

/*
===================
GLimp_SwapBuffers
===================
*/
void GLimp_SwapBuffers() {
	eglSwapBuffers(s_display, s_surface);
}

/*
=================
GLimp_SetGamma
=================
*/
void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	common->Printf("Gamma ramp not supported\n");
}

/*
=================
GLimp_ActivateContext
=================
*/
void GLimp_ActivateContext() {
	common->DPrintf("TODO: GLimp_ActivateContext\n");
}

/*
=================
GLimp_DeactivateContext
=================
*/
void GLimp_DeactivateContext() {
	common->DPrintf("TODO: GLimp_DeactivateContext\n");
}

/*
===================
GLimp_ExtensionPointer
===================
*/
GLExtension_t GLimp_ExtensionPointer(const char *name) {
	// assert(SDL_WasInit(SDL_INIT_VIDEO));

	return (GLExtension_t)eglGetProcAddress(name);
}

void GLimp_GrabInput(int flags) {
	grabbed = true;
}
