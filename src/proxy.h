#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Original DLL handle
extern HMODULE g_original_dll;

// Function pointer type for vmMain
typedef int (__cdecl *vmMain_t)(int command, int arg0, int arg1, int arg2, const char *arg3, const char *arg4, const char *arg5, const char *arg6, const char *arg7, const char *arg8, const char *arg9, const char *arg10, const char *arg11);

// Pointer to original vmMain
extern vmMain_t g_original_vmMain;

// Initialize the proxy (load original DLL)
bool proxy_init();

// Shutdown the proxy
void proxy_shutdown();
