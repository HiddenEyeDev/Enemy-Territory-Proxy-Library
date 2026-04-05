#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#include "proxy_hooks.h"

static HMODULE g_hModule = NULL;
static HMODULE g_orig = NULL;
static BOOL g_init_attempted = FALSE;

typedef int (__cdecl *vmMain_t)(int, int, int, int, const char*, const char*, const char*, const char*, const char*, const char*, const char*, const char*, const char*);
static vmMain_t g_vmMain = NULL;

typedef int (__cdecl *dllEntry_t)(int (__cdecl *)(int, ...));
static dllEntry_t g_dllEntry = NULL;

static int (__cdecl *g_engine_syscall)(int, ...) = NULL;

vmCvar_t proxy_version;

static void InitOriginal(void) {
    if (g_init_attempted) return;
    g_init_attempted = TRUE;

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);

    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(path, L"qagame_mp_x86_orig.dll");

    g_orig = LoadLibraryW(path);
    if (!g_orig) return;

    g_vmMain = (vmMain_t)GetProcAddress(g_orig, "vmMain");
    g_dllEntry = (dllEntry_t)GetProcAddress(g_orig, "dllEntry");

    if (!g_vmMain || !g_dllEntry) {
        if (g_orig) FreeLibrary(g_orig);
        g_orig = NULL;
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_orig) FreeLibrary(g_orig);
    }
    return TRUE;
}

void Proxy_Print(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    if (g_engine_syscall) {
        g_engine_syscall(G_PRINT, buf);
    }
}

void Proxy_RegisterCvar(vmCvar_t* cvar, const char* name, const char* defaultValue, int flags) {
    if (g_engine_syscall) {
        g_engine_syscall(G_CVAR_REGISTER, cvar, name, defaultValue, flags);
    }
}

void Proxy_SendServerCommand(int clientNum, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    if (g_engine_syscall) {
        g_engine_syscall(G_SEND_SERVER_COMMAND, clientNum, buf);
    }
}

void Proxy_Init(void) {
    Proxy_RegisterCvar(&proxy_version, "proxy_version", "0.1.0", CVAR_ROM | CVAR_SERVERINFO);
    Proxy_Print("^3Proxy Library ^7v%s loaded\n", proxy_version.string);
}

qboolean Proxy_HandleCommand(void) {
    char cmd[256];
    if (!g_engine_syscall) return qfalse;

    int argc = g_engine_syscall(G_ARGC);
    if (argc < 1) return qfalse;

    g_engine_syscall(G_ARGV, 0, cmd, sizeof(cmd));

    if (_stricmp(cmd, "proxy_status") == 0) {
        Proxy_Print("^3Proxy Library Status^7\n");
        Proxy_Print("  Version: %s\n", proxy_version.string);
        Proxy_Print("  Original DLL: %s\n", g_orig ? "loaded" : "not loaded");
        return qtrue;
    }

    return qfalse;
}

__declspec(dllexport) int __cdecl vmMain(int cmd, int a0, int a1, int a2, const char* a3, const char* a4, const char* a5, const char* a6, const char* a7, const char* a8, const char* a9, const char* a10, const char* a11) {
    if (!g_vmMain) InitOriginal();
    if (!g_vmMain) return -1;

    switch (cmd) {
        case GAME_INIT:
            Proxy_Init();
            break;
        case GAME_CONSOLE_COMMAND:
            if (Proxy_HandleCommand()) {
                return 1;
            }
            break;
        default:
            break;
    }

    return g_vmMain(cmd, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

__declspec(dllexport) int __cdecl dllEntry(int (__cdecl *syscall)(int, ...)) {
    if (!g_dllEntry) InitOriginal();
    if (!g_dllEntry) return 0;

    g_engine_syscall = syscall;
    return g_dllEntry(syscall);
}
