#pragma once

#include <windows.h>

typedef int qboolean;
#define qtrue 1
#define qfalse 0

// Syscall IDs from g_public.h
enum gameImport_t {
    G_PRINT = 0,
    G_ERROR,
    G_MILLISECONDS,
    G_CVAR_REGISTER,
    G_CVAR_UPDATE,
    G_CVAR_SET,
    G_CVAR_VARIABLE_INTEGER_VALUE,
    G_CVAR_VARIABLE_STRING_BUFFER,
    G_CVAR_LATCHEDVARIABLESTRINGBUFFER,
    G_ARGC,
    G_ARGV,
    G_FS_FOPEN_FILE,
    G_FS_READ,
    G_FS_WRITE,
    G_FS_RENAME,
    G_FS_FCLOSE_FILE,
    G_SEND_CONSOLE_COMMAND,
    G_LOCATE_GAME_DATA,
    G_DROP_CLIENT,
    G_SEND_SERVER_COMMAND,
    G_SET_CONFIGSTRING,
    G_GET_CONFIGSTRING,
    G_GET_USERINFO,
    G_SET_USERINFO,
    G_GET_SERVERINFO,
    G_SET_BRUSH_MODEL,
    G_TRACE,
    G_POINT_CONTENTS,
    G_IN_PVS,
    G_IN_PVS_IGNORE_PORTALS,
    G_ADJUST_AREA_PORTAL_STATE,
    G_AREAS_CONNECTED,
    G_LINKENTITY,
    G_UNLINKENTITY,
    G_ENTITIES_IN_BOX,
    G_ENTITY_CONTACT,
    G_BOT_ALLOCATE_CLIENT,
    G_BOT_FREE_CLIENT,
    G_GET_USERCMD,
    G_GET_ENTITY_TOKEN,
    G_FS_GETFILELIST,
    G_DEBUG_POLYGON_CREATE,
    G_DEBUG_POLYGON_DELETE,
    G_REAL_TIME,
    G_SNAPVECTOR,
    G_TRACECAPSULE,
    G_ENTITY_CONTACTCAPSULE,
    G_GETTAG,
    G_REGISTERTAG,
    G_REGISTERSOUND,
    G_GET_SOUND_LENGTH,
    G_SENDMESSAGE = 160,
    G_MESSAGESTATUS,
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
    GAME_SNAPSHOT_CALLBACK,
    BOTAI_START_FRAME,
    GAME_MESSAGERECEIVED = 12,
};

// vmCvar structure matching ET's layout
typedef struct {
    int handle;
    int modificationCount;
    float value;
    int integer;
    char string[256];
} vmCvar_t;

// Cvar flags
#define CVAR_ARCHIVE        0x00000001
#define CVAR_USERINFO       0x00000002
#define CVAR_SERVERINFO     0x00000004
#define CVAR_ROM            0x00000008
#define CVAR_LATCH          0x00000010
#define CVAR_CHEAT          0x00000020

// Team constants
#define TEAM_FREE       0
#define TEAM_SPECTATOR  1
#define TEAM_ALLIES     2
#define TEAM_AXIS       3
#define TEAM_AUTO       4

// Proxy cvars
extern vmCvar_t proxy_version;
extern vmCvar_t proxy_debug;
extern vmCvar_t proxy_loglevel;
extern vmCvar_t proxy_autobalance;
extern vmCvar_t proxy_balance_threshold;
extern vmCvar_t proxy_balance_minplayers;
extern vmCvar_t proxy_balance_force;

// Client info structure
typedef struct {
    int clientNum;
    char name[64];
    char ip[64];
    char guid[64];
    qboolean connected;
    qboolean isBot;
} proxy_client_t;

#define MAX_PROXY_CLIENTS 64
extern proxy_client_t g_clients[MAX_PROXY_CLIENTS];

// ====================================================================
// Stats System
// ====================================================================

typedef struct {
    char guid[64];
    char name[64];
    int kills;
    int deaths;
    int headshots;
    int shotsFired;
    int shotsHit;
    int teamKills;
    int suicides;
    int longestKillStreak;
    int currentKillStreak;
    float totalDamageDealt;
    float totalDamageTaken;
    int firstSeen;
    int lastSeen;
    int totalPlaytime;
} player_stats_t;

#define MAX_STATS_ENTRIES 1024
extern player_stats_t g_stats[MAX_STATS_ENTRIES];
extern int g_statsCount;

void Stats_Init(void);
void Stats_Load(void);
void Stats_Save(void);
player_stats_t* Stats_GetOrCreate(const char* guid, const char* name);
player_stats_t* Stats_GetByGUID(const char* guid);
void Stats_RecordKill(const char* killerGUID, const char* victimGUID, qboolean headshot);
void Stats_RecordDeath(const char* victimGUID);
void Stats_RecordShotsFired(const char* guid, int count);
void Stats_RecordShotsHit(const char* guid, int count);
void Stats_RecordTeamKill(const char* killerGUID, const char* victimGUID);
void Stats_RecordSuicide(const char* guid);
float Stats_GetAccuracy(player_stats_t* stats);
float Stats_GetKDRatio(player_stats_t* stats);
float Stats_GetSkillRating(player_stats_t* stats);
const char* Stats_GetSkillTier(float rating);
void Stats_PrintPlayerStats(player_stats_t* stats);
void Stats_PrintTopKills(int count);
void Stats_PrintTopAccuracy(int count);
void Stats_PrintTopKD(int count);
void Stats_PrintTopHeadshots(int count);
void Stats_ResetAll(void);
void Stats_ResetPlayer(const char* guid);

// ====================================================================
// Awards/Achievements System
// ====================================================================

#define MAX_AWARDS 40
#define MAX_AWARD_NAME 32
#define MAX_AWARD_DESC 64
#define MAX_PLAYER_AWARDS 64
#define MAX_AWARD_PLAYERS 512

typedef struct {
    int id;
    char name[MAX_AWARD_NAME];
    char description[MAX_AWARD_DESC];
    qboolean repeatable;
    qboolean hidden;
} award_def_t;

typedef struct {
    int awardId;
    int count;
    int firstEarned;
    int lastEarned;
} player_award_t;

typedef struct {
    char guid[64];
    player_award_t awards[MAX_PLAYER_AWARDS];
    int awardCount;
} player_awards_t;

// Award tracking state (per match)
typedef struct {
    int clientNum;
    int lastKills[10];
    int lastKillCount;
    int lastKiller;
    int lastDeathTime;
    int killsThisRound;
    int deathsThisRound;
    int headshotsThisRound;
    int lastHeadshotTimes[5];
    int lastHeadshotCount;
    int killsByVictim[MAX_PROXY_CLIENTS];
    qboolean firstBlood;
    qboolean firstToDie;
} match_tracker_t;

// Award IDs
enum {
    AWARD_FIRST_BLOOD = 1,
    AWARD_FIRST_TO_DIE,
    AWARD_DOUBLE_TROUBLE,
    AWARD_TRIPLE_THREAT,
    AWARD_MEGA_KILL,
    AWARD_ULTRA_KILL,
    AWARD_MONSTER_KILL,
    AWARD_MULTI_SLAYER,
    AWARD_HEADHUNTER,
    AWARD_SHARPSHOOTER,
    AWARD_HAT_TRICK,
    AWARD_STREAK_MASTER,
    AWARD_UNSTOPPABLE,
    AWARD_GODLIKE,
    AWARD_DOMINATOR,
    AWARD_REVENGE,
    AWARD_LONG_SHOT,
    AWARD_CENTURY,
    AWARD_VETERAN,
    AWARD_LEGEND,
    AWARD_SURVIVOR,
    AWARD_MEDIC,
    AWARD_ENGINEER,
    AWARD_DEMOLITION,
    AWARD_OBJECTIVE_PLAYER,
    AWARD_TEAM_KILLER,
    AWARD_SUICIDE_KING,
    AWARD_COMEBACK,
    AWARD_PAYBACK,
    AWARD_BULLDOZER,
};

extern match_tracker_t g_matchTrackers[MAX_PROXY_CLIENTS];

void Awards_Init(void);
void Awards_Load(void);
void Awards_Save(void);
player_awards_t* Awards_GetPlayer(const char* guid);
void Awards_CheckOnKill(int killerNum, int victimNum, qboolean headshot, float distance);
void Awards_CheckOnDeath(int victimNum, int killerNum);
void Awards_CheckOnRoundEnd(void);
void Awards_CheckOnMedicRevive(int medicNum, int patientNum);
void Awards_CheckOnObjective(int clientNum, const char* objectiveType);
void Awards_ResetRound(void);
void Awards_GiveAward(int clientNum, int awardId);
void Awards_PrintPlayerAwards(const char* guid, int clientNum);

// ====================================================================
// Player Profile System
// ====================================================================

#define MAX_SIGNATURE_LEN 128
#define MAX_TITLE_LEN 32
#define MAX_CLAN_TAG_LEN 8
#define MAX_FRIENDS 32

typedef struct {
    int wins;
    int losses;
    int mvpCount;
    int date; // timestamp of match
} match_result_t;

#define MAX_RECENT_MATCHES 10

typedef struct {
    char guid[64];
    char name[64];
    
    // Level & XP
    int level;
    int xp;
    int xpToNext;
    
    // Reputation
    int repPositive;
    int repNegative;
    int lastRepGiven[MAX_PROXY_CLIENTS]; // Prevent spam
    
    // Identity
    char signature[MAX_SIGNATURE_LEN];
    char customTitle[MAX_TITLE_LEN];
    char clanTag[MAX_CLAN_TAG_LEN];
    int favoriteClass; // 0=none, 1=soldier, 2=medic, 3=engineer, 4=covertops
    int playtimeMinutes;
    
    // Recent Performance
    match_result_t recentMatches[MAX_RECENT_MATCHES];
    int recentMatchCount;
    
    // Social
    char friends[MAX_FRIENDS][64];
    int friendCount;
    qboolean lookingForGroup;
    
    // Unlocks
    qboolean unlockedChatColor;
    qboolean unlockedVoteWeight;
    qboolean unlockedCustomJoinMsg;
    char joinMessage[MAX_SIGNATURE_LEN];
    
    // Tracking
    int firstSeen;
    int lastSeen;
    int totalMatches;
} player_profile_t;

#define MAX_PROFILE_PLAYERS 512

extern player_profile_t g_profiles[MAX_PROFILE_PLAYERS];
extern int g_profileCount;

// Initialize profiles
void Profiles_Init(void);
void Profiles_Load(void);
void Profiles_Save(void);

// Get or create profile
player_profile_t* Profiles_GetOrCreate(const char* guid, const char* name);
player_profile_t* Profiles_GetByGUID(const char* guid);

// XP & Leveling
void Profiles_AddXP(const char* guid, int amount);
void Profiles_CheckLevelUp(const char* guid);

// Reputation
qboolean Profiles_GiveRep(int giverNum, int targetNum, qboolean positive);
float Profiles_GetRepPercent(player_profile_t* profile);

// Commands
qboolean Profiles_HandleCommand(int clientNum, const char* cmd, int argc);

// Match tracking
void Profiles_RecordMatchResult(const char* guid, qboolean won, qboolean mvp);

// Print player card
void Profiles_PrintCard(const char* guid, int clientNum);

// ====================================================================
// Autobalance System
// ====================================================================

typedef struct {
    int clientNum;
    char guid[64];
    char name[64];
    int team;
    float skill;
} balance_player_t;

#define MAX_BALANCE_PLAYERS 64

typedef struct {
    float alliesSkill;
    float axisSkill;
    int alliesCount;
    int axisCount;
} team_balance_t;

void Balance_UpdatePlayerSkills(void);
team_balance_t Balance_GetInfo(void);
qboolean Balance_NeedsBalance(void);
void Balance_AutoBalance(void);
int Balance_GetBestTeam(void);
void Balance_PrintStatus(void);
qboolean Balance_HandleCommand(const char* cmdLine, int clientNum);

// ====================================================================
// Client Management
// ====================================================================

void Clients_Init(void);
const char* Clients_OnConnect(int clientNum, qboolean firstTime, qboolean isBot);
void Clients_OnBegin(int clientNum);
void Clients_OnDisconnect(int clientNum);
void Clients_OnUserInfoChanged(int clientNum);
proxy_client_t* Clients_Get(int clientNum);
void Clients_Kick(int clientNum, const char* reason);
void Clients_BanIP(const char* ip, const char* reason);
qboolean Clients_IsIPBanned(const char* ip);
void Clients_PrintList(void);

// ====================================================================
// Proxy Core
// ====================================================================

void Proxy_InitCvars(void);
qboolean Proxy_HandleCommand(void);
void Proxy_Print(const char* fmt, ...);
void Proxy_RegisterCvar(vmCvar_t* cvar, const char* name, const char* defaultValue, int flags);
int Proxy_GetCvarInteger(const char* name);
void Proxy_GetCvarString(const char* name, char* buffer, int bufferSize);
void Proxy_SetCvar(const char* name, const char* value);
void Proxy_SendConsoleCommand(const char* text);
void Proxy_SendServerCommand(int clientNum, const char* fmt, ...);
void Proxy_UpdateCvar(vmCvar_t* cvar);
