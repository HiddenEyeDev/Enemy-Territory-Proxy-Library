#pragma once

// Game module public interface
// This defines the standard ET game module entry points

// vmMain command constants
#define VM_INIT                    0
#define VM_CONSOLE_COMMAND         1
#define VM_GAME_INIT               2
#define VM_GAME_SHUTDOWN           3
#define VM_GAME_CLIENT_CONNECT     4
#define VM_GAME_CLIENT_BEGIN       5
#define VM_GAME_CLIENT_USERINFO_CHANGED 6
#define VM_GAME_CLIENT_DISCONNECT  7
#define VM_GAME_CLIENT_COMMAND     8
#define VM_GAME_CLIENT_THINK       9
#define VM_GAME_RUN_FRAME          10
#define VM_GAME_SPAWN_ENTITIES     11
#define VM_GAME_REVERSE_COMMAND    12
