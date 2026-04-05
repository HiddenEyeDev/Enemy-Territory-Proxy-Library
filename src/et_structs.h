#pragma once

// Basic ET structures for proxy functionality
// These are minimal structures needed for the proxy to function

#define MAX_STRING_CHARS 1024

// Game commands passed to vmMain
enum gameImportCommand_t {
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
    GAME_SPAWN_ENTITIES,
    GAME_REVERSE_COMMAND,
};

// Server version identifier
#define ET_VERSION "ET 2.60b"
#define SILENT_MOD_VERSION "0.9.0"
