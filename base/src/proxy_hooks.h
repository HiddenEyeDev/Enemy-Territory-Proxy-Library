#pragma once

#include <windows.h>

typedef int qboolean;
#define qtrue 1
#define qfalse 0

// Syscall IDs
enum gameImport_t {
    G_PRINT = 0,
    G_ERROR,
    G_MILLISECONDS,
    G_CVAR_REGISTER,
    G_CVAR_UPDATE,
    G_CVAR_SET,
    G_CVAR_VARIABLE_INTEGER_VALUE,
    G_CVAR_VARIABLE_STRING_BUFFER,
    G_ARGC,
    G_ARGV,
    G_SEND_CONSOLE_COMMAND,
    G_DROP_CLIENT,
    G_SEND_SERVER_COMMAND,
    G_GET_USERINFO,
    G_SET_USERINFO,
};

// vmMain command IDs
enum gameExport_t {
    GAME_INIT = 0,
    GAME_SHUTDOWN,
    GAME_CLIENT_CONNECT,
    GAME_CLIENT_BEGIN,
    GAME_CLIENT_USERINFO_CHANGED,
    GAME_CLIENT_DISCONNECT,
    GAME_CLIENT_COMMAND,
    GAME_CLIENT_THINK,
    GAME_RUN_FRAME,
    GAME_CONSOLE_COMMAND,
};

// vmCvar structure
typedef struct {
    int handle;
    int modificationCount;
    float value;
    int integer;
    char string[256];
} vmCvar_t;

// Cvar flags
#define CVAR_ARCHIVE    0x00000001
#define CVAR_SERVERINFO 0x00000004
#define CVAR_ROM        0x00000008

// Proxy cvars
extern vmCvar_t proxy_version;

// Initialize proxy
void Proxy_Init(void);

// Handle console commands
qboolean Proxy_HandleCommand(void);

// Helpers
void Proxy_Print(const char* fmt, ...);
void Proxy_RegisterCvar(vmCvar_t* cvar, const char* name, const char* defaultValue, int flags);
void Proxy_SendServerCommand(int clientNum, const char* fmt, ...);
