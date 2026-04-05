#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "proxy_hooks.h"

static HMODULE g_hModule = NULL;
static HMODULE g_orig = NULL;
static BOOL g_init_attempted = FALSE;

typedef int (__cdecl *vmMain_t)(int, int, int, int, const char*, const char*, const char*, const char*, const char*, const char*, const char*, const char*, const char*);
static vmMain_t g_vmMain = NULL;

typedef int (__cdecl *dllEntry_t)(int (__cdecl *)(int, ...));
static dllEntry_t g_dllEntry = NULL;

static int (__cdecl *g_engine_syscall)(int, ...) = NULL;

// Proxy cvars
vmCvar_t proxy_version;
vmCvar_t proxy_debug;
vmCvar_t proxy_loglevel;
vmCvar_t proxy_autobalance;
vmCvar_t proxy_balance_threshold;
vmCvar_t proxy_balance_minplayers;
vmCvar_t proxy_balance_force;

// Client tracking
proxy_client_t g_clients[MAX_PROXY_CLIENTS];

// Stats
player_stats_t g_stats[MAX_STATS_ENTRIES];
int g_statsCount = 0;

// Ban list
#define MAX_BANS 128
typedef struct {
    char ip[64];
    char reason[256];
    qboolean active;
} ban_entry_t;

static ban_entry_t g_bans[MAX_BANS];

// Awards definitions
static award_def_t g_awardDefs[MAX_AWARDS] = {
    { AWARD_FIRST_BLOOD,       "FirstBlood",      "First kill of the round",          qtrue,  qfalse },
    { AWARD_FIRST_TO_DIE,      "DeadMeat",        "First to die in the round",        qtrue,  qfalse },
    { AWARD_DOUBLE_TROUBLE,    "DoubleTrouble",   "Kill 2 enemies within 3 seconds",  qtrue,  qfalse },
    { AWARD_TRIPLE_THREAT,     "TripleThreat",    "Kill 3 enemies within 5 seconds",  qtrue,  qfalse },
    { AWARD_MEGA_KILL,         "MegaKill",        "Kill 4 enemies within 7 seconds",  qtrue,  qfalse },
    { AWARD_ULTRA_KILL,        "UltraKill",       "Kill 5 enemies within 10 seconds", qtrue,  qfalse },
    { AWARD_MONSTER_KILL,      "MonsterKill",     "Kill 6+ enemies within 12 seconds",qtrue,  qfalse },
    { AWARD_MULTI_SLAYER,      "MultiSlayer",     "Kill 3+ enemies in 1 second",      qtrue,  qfalse },
    { AWARD_HEADHUNTER,        "Headhunter",      "Get 3 headshots in one round",     qtrue,  qfalse },
    { AWARD_SHARPSHOOTER,      "Sharpshooter",    "Get 5 headshots in one round",     qtrue,  qfalse },
    { AWARD_HAT_TRICK,         "HatTrick",        "Kill the same player 3 times",     qtrue,  qfalse },
    { AWARD_STREAK_MASTER,     "StreakMaster",    "Get a 10 kill streak",             qtrue,  qfalse },
    { AWARD_UNSTOPPABLE,       "Unstoppable",     "Get a 15 kill streak",             qtrue,  qfalse },
    { AWARD_GODLIKE,           "Godlike",         "Get a 20 kill streak",             qtrue,  qfalse },
    { AWARD_DOMINATOR,         "Dominator",       "Get a 25 kill streak",             qtrue,  qfalse },
    { AWARD_REVENGE,           "Revenge",         "Kill someone who killed you",      qtrue,  qfalse },
    { AWARD_LONG_SHOT,         "LongShot",        "Kill from 1500+ units away",       qtrue,  qfalse },
    { AWARD_CENTURY,           "Century",         "Reach 100 total kills",            qfalse, qfalse },
    { AWARD_VETERAN,           "Veteran",         "Reach 500 total kills",            qfalse, qfalse },
    { AWARD_LEGEND,            "Legend",          "Reach 1000 total kills",           qfalse, qfalse },
    { AWARD_SURVIVOR,          "Survivor",        "Win a round without dying",        qtrue,  qfalse },
    { AWARD_MEDIC,             "FieldMedic",      "Revive 5 teammates in a round",    qtrue,  qfalse },
    { AWARD_ENGINEER,          "MasterBuilder",   "Build/repair 5 constructs",        qtrue,  qfalse },
    { AWARD_DEMOLITION,        "Demolition",      "Destroy 5 enemy constructions",    qtrue,  qfalse },
    { AWARD_OBJECTIVE_PLAYER,  "ObjectiveHero",   "Complete 3 objectives in a round", qtrue,  qfalse },
    { AWARD_TEAM_KILLER,       "TeamKiller",      "Kill 3 teammates",                 qtrue,  qfalse },
    { AWARD_SUICIDE_KING,      "DarwinAward",     "Die 5 times in one round",         qtrue,  qfalse },
    { AWARD_COMEBACK,          "ComebackKid",     "Win after being down 3+ rounds",   qtrue,  qfalse },
    { AWARD_PAYBACK,           "Payback",         "Kill your last killer 3 times",    qtrue,  qfalse },
    { AWARD_BULLDOZER,         "Bulldozer",       "Kill 5 enemies without taking damage", qtrue, qfalse },
};

// Match trackers
match_tracker_t g_matchTrackers[MAX_PROXY_CLIENTS];

// Player awards storage
static player_awards_t g_playerAwards[MAX_AWARD_PLAYERS];
static int g_awardPlayerCount = 0;

// Player profiles
player_profile_t g_profiles[MAX_PROFILE_PLAYERS];
int g_profileCount = 0;

// Balance
static balance_player_t g_balancePlayers[MAX_BALANCE_PLAYERS];
static int g_balanceCount = 0;

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
        Stats_Save();
        Awards_Save();
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

int Proxy_GetCvarInteger(const char* name) {
    if (g_engine_syscall) {
        return g_engine_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, name);
    }
    return 0;
}

void Proxy_GetCvarString(const char* name, char* buffer, int bufferSize) {
    if (g_engine_syscall) {
        g_engine_syscall(G_CVAR_VARIABLE_STRING_BUFFER, name, buffer, bufferSize);
    }
}

void Proxy_SetCvar(const char* name, const char* value) {
    if (g_engine_syscall) {
        g_engine_syscall(G_CVAR_SET, name, value);
    }
}

void Proxy_SendConsoleCommand(const char* text) {
    if (g_engine_syscall) {
        g_engine_syscall(G_SEND_CONSOLE_COMMAND, text);
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

void Proxy_UpdateCvar(vmCvar_t* cvar) {
    if (g_engine_syscall) {
        g_engine_syscall(G_CVAR_UPDATE, cvar);
    }
}

void Proxy_InitCvars(void) {
    Proxy_RegisterCvar(&proxy_version, "proxy_version", "0.1.0", CVAR_ROM | CVAR_SERVERINFO);
    Proxy_RegisterCvar(&proxy_debug, "proxy_debug", "0", CVAR_ARCHIVE);
    Proxy_RegisterCvar(&proxy_loglevel, "proxy_loglevel", "1", CVAR_ARCHIVE);
    Proxy_RegisterCvar(&proxy_autobalance, "proxy_autobalance", "1", CVAR_ARCHIVE);
    Proxy_RegisterCvar(&proxy_balance_threshold, "proxy_balance_threshold", "150", CVAR_ARCHIVE);
    Proxy_RegisterCvar(&proxy_balance_minplayers, "proxy_balance_minplayers", "4", CVAR_ARCHIVE);
    Proxy_RegisterCvar(&proxy_balance_force, "proxy_balance_force", "0", CVAR_ARCHIVE);
    Proxy_Print("^3Proxy Library ^7v%s loaded\n", proxy_version.string);
}

// ====================================================================
// Stats System
// ====================================================================

static const char* STATS_FILE = "proxy_stats.dat";

void Stats_Init(void) {
    memset(g_stats, 0, sizeof(g_stats));
    g_statsCount = 0;
    Stats_Load();
    Proxy_Print("^3Stats system initialized^7 - %d player records loaded\n", g_statsCount);
}

void Stats_Load(void) {
    FILE* f = fopen(STATS_FILE, "rb");
    if (!f) return;

    fread(&g_statsCount, sizeof(int), 1, f);
    if (g_statsCount > MAX_STATS_ENTRIES) g_statsCount = MAX_STATS_ENTRIES;
    fread(g_stats, sizeof(player_stats_t), g_statsCount, f);
    fclose(f);
}

void Stats_Save(void) {
    FILE* f = fopen(STATS_FILE, "wb");
    if (!f) return;

    fwrite(&g_statsCount, sizeof(int), 1, f);
    fwrite(g_stats, sizeof(player_stats_t), g_statsCount, f);
    fclose(f);
}

player_stats_t* Stats_GetOrCreate(const char* guid, const char* name) {
    player_stats_t* existing = Stats_GetByGUID(guid);
    if (existing) {
        strncpy_s(existing->name, sizeof(existing->name), name, sizeof(existing->name) - 1);
        return existing;
    }

    if (g_statsCount >= MAX_STATS_ENTRIES) return NULL;

    player_stats_t* stats = &g_stats[g_statsCount++];
    memset(stats, 0, sizeof(player_stats_t));
    strncpy_s(stats->guid, sizeof(stats->guid), guid, sizeof(stats->guid) - 1);
    strncpy_s(stats->name, sizeof(stats->name), name, sizeof(stats->name) - 1);
    stats->firstSeen = GetTickCount();
    return stats;
}

player_stats_t* Stats_GetByGUID(const char* guid) {
    for (int i = 0; i < g_statsCount; i++) {
        if (strcmp(g_stats[i].guid, guid) == 0) {
            return &g_stats[i];
        }
    }
    return NULL;
}

void Stats_RecordKill(const char* killerGUID, const char* victimGUID, qboolean headshot) {
    player_stats_t* killer = Stats_GetByGUID(killerGUID);
    if (killer) {
        killer->kills++;
        killer->currentKillStreak++;
        if (killer->currentKillStreak > killer->longestKillStreak) {
            killer->longestKillStreak = killer->currentKillStreak;
        }
        if (headshot) killer->headshots++;
    }
}

void Stats_RecordDeath(const char* victimGUID) {
    player_stats_t* victim = Stats_GetByGUID(victimGUID);
    if (victim) {
        victim->deaths++;
        victim->currentKillStreak = 0;
    }
}

void Stats_RecordShotsFired(const char* guid, int count) {
    player_stats_t* stats = Stats_GetByGUID(guid);
    if (stats) stats->shotsFired += count;
}

void Stats_RecordShotsHit(const char* guid, int count) {
    player_stats_t* stats = Stats_GetByGUID(guid);
    if (stats) stats->shotsHit += count;
}

void Stats_RecordTeamKill(const char* killerGUID, const char* victimGUID) {
    player_stats_t* killer = Stats_GetByGUID(killerGUID);
    if (killer) {
        killer->teamKills++;
        killer->currentKillStreak = 0;
    }
}

void Stats_RecordSuicide(const char* guid) {
    player_stats_t* stats = Stats_GetByGUID(guid);
    if (stats) {
        stats->suicides++;
        stats->deaths++;
        stats->currentKillStreak = 0;
    }
}

float Stats_GetAccuracy(player_stats_t* stats) {
    if (!stats || stats->shotsFired == 0) return 0.0f;
    return (float)stats->shotsHit / (float)stats->shotsFired * 100.0f;
}

float Stats_GetKDRatio(player_stats_t* stats) {
    if (!stats) return 0.0f;
    if (stats->deaths == 0) return (float)stats->kills;
    return (float)stats->kills / (float)stats->deaths;
}

float Stats_GetSkillRating(player_stats_t* stats) {
    if (!stats) return 0.0f;

    float rating = 0.0f;

    float kd = Stats_GetKDRatio(stats);
    if (kd > 5.0f) kd = 5.0f;
    rating += (kd / 5.0f) * 400.0f;

    float acc = Stats_GetAccuracy(stats);
    if (acc > 80.0f) acc = 80.0f;
    rating += (acc / 80.0f) * 350.0f;

    float kills = (float)stats->kills;
    if (kills > 5000.0f) kills = 5000.0f;
    rating += (kills / 5000.0f) * 150.0f;

    float streak = (float)stats->longestKillStreak;
    if (streak > 30.0f) streak = 30.0f;
    rating += (streak / 30.0f) * 100.0f;

    return rating;
}

const char* Stats_GetSkillTier(float rating) {
    if (rating >= 800.0f) return "^1Elite";
    if (rating >= 650.0f) return "^4Veteran";
    if (rating >= 500.0f) return "^3Skilled";
    if (rating >= 350.0f) return "^6Regular";
    if (rating >= 200.0f) return "^7Novice";
    return "^8Rookie";
}

static int CompareByKills(const void* a, const void* b) {
    const player_stats_t* sa = (const player_stats_t*)a;
    const player_stats_t* sb = (const player_stats_t*)b;
    return sb->kills - sa->kills;
}

static int CompareByAccuracy(const void* a, const void* b) {
    const player_stats_t* sa = (const player_stats_t*)a;
    const player_stats_t* sb = (const player_stats_t*)b;
    float accA = sa->shotsFired > 0 ? (float)sa->shotsHit / (float)sa->shotsFired : 0.0f;
    float accB = sb->shotsFired > 0 ? (float)sb->shotsHit / (float)sb->shotsFired : 0.0f;
    if (accA < accB) return 1;
    if (accA > accB) return -1;
    return 0;
}

static int CompareByKD(const void* a, const void* b) {
    const player_stats_t* sa = (const player_stats_t*)a;
    const player_stats_t* sb = (const player_stats_t*)b;
    float kdA = sa->deaths > 0 ? (float)sa->kills / (float)sa->deaths : (float)sa->kills;
    float kdD = sb->deaths > 0 ? (float)sb->kills / (float)sb->deaths : (float)sb->kills;
    if (kdA < kdD) return 1;
    if (kdA > kdD) return -1;
    return 0;
}

static int CompareByHeadshots(const void* a, const void* b) {
    const player_stats_t* sa = (const player_stats_t*)a;
    const player_stats_t* sb = (const player_stats_t*)b;
    return sb->headshots - sa->headshots;
}

static void PrintSeparator(int clientNum) {
    if (clientNum >= 0) {
        Proxy_SendServerCommand(clientNum, "print \"  ^7----------------------------------------\n\"");
    } else {
        Proxy_Print("  ^7----------------------------------------\n");
    }
}

void Stats_PrintPlayerStats(player_stats_t* stats) {
    if (!stats) {
        Proxy_Print("^3No stats found for this player^7\n");
        return;
    }

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Player Stats: ^7%s\n", stats->name);
    Proxy_Print("^3========================================\n");
    Proxy_Print("  ^7GUID: ^3%s\n", stats->guid);
    PrintSeparator(-1);
    Proxy_Print("  ^7Kills:        ^2%-6d^7  Deaths:     ^1%-6d\n", stats->kills, stats->deaths);
    Proxy_Print("  ^7K/D Ratio:    ^3%.2f^7    Headshots:  ^6%-6d\n", Stats_GetKDRatio(stats), stats->headshots);
    Proxy_Print("  ^7Accuracy:     ^3%.1f%%^7   HS Rate:    ^6%.1f%%\n", Stats_GetAccuracy(stats),
        stats->kills > 0 ? (float)stats->headshots / (float)stats->kills * 100.0f : 0.0f);
    PrintSeparator(-1);
    Proxy_Print("  ^7Shots Fired:  ^3%-6d^7  Shots Hit:  ^2%-6d\n", stats->shotsFired, stats->shotsHit);
    Proxy_Print("  ^7Team Kills:   ^1%-6d^7  Suicides:   ^6%-6d\n", stats->teamKills, stats->suicides);
    PrintSeparator(-1);
    Proxy_Print("  ^7Longest Streak: ^3%d^7    Current:    ^2%d\n", stats->longestKillStreak, stats->currentKillStreak);
    Proxy_Print("  ^7Total Damage: ^3%.0f^7    Taken:      ^1%.0f\n", stats->totalDamageDealt, stats->totalDamageTaken);
    Proxy_Print("  ^7Playtime:     ^3%d min^7\n", stats->totalPlaytime / 60000);
    Proxy_Print("^3========================================\n");
}

void Stats_PrintTopKills(int count) {
    if (g_statsCount == 0) {
        Proxy_Print("^3No stats recorded yet^7\n");
        return;
    }

    player_stats_t sorted[MAX_STATS_ENTRIES];
    memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
    qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByKills);

    if (count > g_statsCount) count = g_statsCount;

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Top %d Players by Kills\n", count);
    Proxy_Print("^3========================================\n");
    for (int i = 0; i < count; i++) {
        Proxy_Print("  ^7%2d. ^3%-20s ^2%-5d ^7K/D: ^3%.2f ^7HS: ^6%d\n",
            i + 1, sorted[i].name, sorted[i].kills,
            Stats_GetKDRatio(&sorted[i]), sorted[i].headshots);
    }
    Proxy_Print("^3========================================\n");
}

void Stats_PrintTopAccuracy(int count) {
    if (g_statsCount == 0) {
        Proxy_Print("^3No stats recorded yet^7\n");
        return;
    }

    player_stats_t sorted[MAX_STATS_ENTRIES];
    memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
    qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByAccuracy);

    if (count > g_statsCount) count = g_statsCount;

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Top %d Players by Accuracy\n", count);
    Proxy_Print("^3========================================\n");
    for (int i = 0; i < count; i++) {
        Proxy_Print("  ^7%2d. ^3%-20s ^3%.1f%% ^7(%d/%d)\n",
            i + 1, sorted[i].name, Stats_GetAccuracy(&sorted[i]),
            sorted[i].shotsHit, sorted[i].shotsFired);
    }
    Proxy_Print("^3========================================\n");
}

void Stats_PrintTopKD(int count) {
    if (g_statsCount == 0) {
        Proxy_Print("^3No stats recorded yet^7\n");
        return;
    }

    player_stats_t sorted[MAX_STATS_ENTRIES];
    memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
    qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByKD);

    if (count > g_statsCount) count = g_statsCount;

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Top %d Players by K/D Ratio\n", count);
    Proxy_Print("^3========================================\n");
    for (int i = 0; i < count; i++) {
        Proxy_Print("  ^7%2d. ^3%-20s ^3%.2f ^7(%dK/%dD)\n",
            i + 1, sorted[i].name, Stats_GetKDRatio(&sorted[i]),
            sorted[i].kills, sorted[i].deaths);
    }
    Proxy_Print("^3========================================\n");
}

void Stats_PrintTopHeadshots(int count) {
    if (g_statsCount == 0) {
        Proxy_Print("^3No stats recorded yet^7\n");
        return;
    }

    player_stats_t sorted[MAX_STATS_ENTRIES];
    memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
    qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByHeadshots);

    if (count > g_statsCount) count = g_statsCount;

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Top %d Players by Headshots\n", count);
    Proxy_Print("^3========================================\n");
    for (int i = 0; i < count; i++) {
        Proxy_Print("  ^7%2d. ^3%-20s ^6%-5d ^7HS Rate: ^3%.1f%%\n",
            i + 1, sorted[i].name, sorted[i].headshots,
            sorted[i].kills > 0 ? (float)sorted[i].headshots / (float)sorted[i].kills * 100.0f : 0.0f);
    }
    Proxy_Print("^3========================================\n");
}

void Stats_ResetAll(void) {
    memset(g_stats, 0, sizeof(g_stats));
    g_statsCount = 0;
    Stats_Save();
    Proxy_Print("^1All stats have been reset^7\n");
}

void Stats_ResetPlayer(const char* guid) {
    for (int i = 0; i < g_statsCount; i++) {
        if (strcmp(g_stats[i].guid, guid) == 0) {
            memset(&g_stats[i], 0, sizeof(player_stats_t));
            strncpy_s(g_stats[i].guid, sizeof(g_stats[i].guid), guid, sizeof(g_stats[i].guid) - 1);
            Stats_Save();
            Proxy_Print("^1Stats reset for player %s^7\n", guid);
            return;
        }
    }
    Proxy_Print("^3Player not found: %s^7\n", guid);
}

// ====================================================================
// Awards System
// ====================================================================

static const char* AWARDS_FILE = "proxy_awards.dat";

void Awards_Init(void) {
    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        match_tracker_t* mt = &g_matchTrackers[i];
        memset(mt, 0, sizeof(match_tracker_t));
        mt->clientNum = i;
    }
    g_awardPlayerCount = 0;
    Awards_Load();
    Proxy_Print("^3Awards system initialized^7 - %d player records loaded\n", g_awardPlayerCount);
}

void Awards_Load(void) {
    FILE* f = fopen(AWARDS_FILE, "rb");
    if (!f) return;

    fread(&g_awardPlayerCount, sizeof(int), 1, f);
    if (g_awardPlayerCount > MAX_AWARD_PLAYERS) g_awardPlayerCount = MAX_AWARD_PLAYERS;
    fread(g_playerAwards, sizeof(player_awards_t), g_awardPlayerCount, f);
    fclose(f);
}

void Awards_Save(void) {
    FILE* f = fopen(AWARDS_FILE, "wb");
    if (!f) return;

    fwrite(&g_awardPlayerCount, sizeof(int), 1, f);
    fwrite(g_playerAwards, sizeof(player_awards_t), g_awardPlayerCount, f);
    fclose(f);
}

player_awards_t* Awards_GetPlayer(const char* guid) {
    for (int i = 0; i < g_awardPlayerCount; i++) {
        if (strcmp(g_playerAwards[i].guid, guid) == 0) {
            return &g_playerAwards[i];
        }
    }

    if (g_awardPlayerCount >= MAX_AWARD_PLAYERS) return NULL;

    player_awards_t* pa = &g_playerAwards[g_awardPlayerCount++];
    memset(pa, 0, sizeof(player_awards_t));
    strncpy_s(pa->guid, sizeof(pa->guid), guid, sizeof(pa->guid) - 1);
    return pa;
}

static player_award_t* Awards_GetAward(player_awards_t* pa, int awardId) {
    for (int i = 0; i < pa->awardCount; i++) {
        if (pa->awards[i].awardId == awardId) {
            return &pa->awards[i];
        }
    }
    return NULL;
}

static void Awards_Announce(int clientNum, int awardId) {
    if (clientNum < 0 || clientNum >= MAX_PROXY_CLIENTS) return;

    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl || !cl->connected) return;

    award_def_t* def = NULL;
    for (int i = 0; i < MAX_AWARDS; i++) {
        if (g_awardDefs[i].id == awardId) {
            def = &g_awardDefs[i];
            break;
        }
    }
    if (!def) return;

    player_awards_t* pa = Awards_GetPlayer(cl->guid);
    player_award_t* award = pa ? Awards_GetAward(pa, awardId) : NULL;
    int count = award ? award->count : 1;

    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        if (g_clients[i].connected && !g_clients[i].isBot) {
            Proxy_SendServerCommand(i, "cp \"^6*** ^7%s ^6has earned: ^3[%dx] %s^6 ***\"",
                cl->name, count, def->name);
        }
    }
}

void Awards_GiveAward(int clientNum, int awardId) {
    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl || !cl->connected || !cl->guid[0]) return;

    award_def_t* def = NULL;
    for (int i = 0; i < MAX_AWARDS; i++) {
        if (g_awardDefs[i].id == awardId) {
            def = &g_awardDefs[i];
            break;
        }
    }
    if (!def) return;

    player_awards_t* pa = Awards_GetPlayer(cl->guid);
    if (!pa) return;

    player_award_t* existing = Awards_GetAward(pa, awardId);
    if (existing) {
        if (def->repeatable) {
            existing->count++;
            existing->lastEarned = GetTickCount();
        } else {
            return;
        }
    } else {
        if (pa->awardCount >= MAX_PLAYER_AWARDS) return;

        player_award_t* newAward = &pa->awards[pa->awardCount++];
        newAward->awardId = awardId;
        newAward->count = 1;
        newAward->firstEarned = GetTickCount();
        newAward->lastEarned = GetTickCount();
    }

    Awards_Announce(clientNum, awardId);
}

void Awards_ResetRound(void) {
    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        match_tracker_t* mt = &g_matchTrackers[i];
        mt->lastKillCount = 0;
        mt->lastHeadshotCount = 0;
        mt->killsThisRound = 0;
        mt->deathsThisRound = 0;
        mt->headshotsThisRound = 0;
        mt->firstBlood = qfalse;
        mt->firstToDie = qfalse;
        mt->lastKiller = -1;
        mt->lastDeathTime = 0;
        memset(mt->killsByVictim, 0, sizeof(mt->killsByVictim));
    }
}

void Awards_CheckOnKill(int killerNum, int victimNum, qboolean headshot, float distance) {
    if (killerNum < 0 || killerNum >= MAX_PROXY_CLIENTS) return;
    if (victimNum < 0 || victimNum >= MAX_PROXY_CLIENTS) return;

    match_tracker_t* killer = &g_matchTrackers[killerNum];
    match_tracker_t* victim = &g_matchTrackers[victimNum];
    proxy_client_t* killerCl = Clients_Get(killerNum);
    if (!killerCl || !killerCl->connected) return;

    int now = GetTickCount();

    killer->killsThisRound++;
    killer->killsByVictim[victimNum]++;

    if (headshot) {
        killer->headshotsThisRound++;
        if (killer->lastHeadshotCount < 5) {
            killer->lastHeadshotTimes[killer->lastHeadshotCount] = now;
        }
        killer->lastHeadshotCount++;
    }

    if (!killer->firstBlood) {
        killer->firstBlood = qtrue;
        Awards_GiveAward(killerNum, AWARD_FIRST_BLOOD);
    }

    if (victim->lastKiller == killerNum) {
        Awards_GiveAward(killerNum, AWARD_REVENGE);
    }

    if (killer->killsByVictim[victimNum] >= 3) {
        Awards_GiveAward(killerNum, AWARD_HAT_TRICK);
        killer->killsByVictim[victimNum] = 0;
    }

    if (distance > 1500.0f) {
        Awards_GiveAward(killerNum, AWARD_LONG_SHOT);
    }

    if (killer->lastKillCount < 10) {
        killer->lastKills[killer->lastKillCount] = now;
    }
    killer->lastKillCount++;

    if (killer->lastKillCount >= 3) {
        int windowStart = killer->lastKills[killer->lastKillCount - 3];
        int diff = now - windowStart;

        if (diff <= 5000) Awards_GiveAward(killerNum, AWARD_TRIPLE_THREAT);
        if (diff <= 3000) Awards_GiveAward(killerNum, AWARD_DOUBLE_TROUBLE);
    }

    if (killer->lastKillCount >= 4) {
        int windowStart = killer->lastKills[killer->lastKillCount - 4];
        if ((now - windowStart) <= 7000) Awards_GiveAward(killerNum, AWARD_MEGA_KILL);
    }

    if (killer->lastKillCount >= 5) {
        int windowStart = killer->lastKills[killer->lastKillCount - 5];
        if ((now - windowStart) <= 10000) Awards_GiveAward(killerNum, AWARD_ULTRA_KILL);
    }

    if (killer->lastKillCount >= 6) {
        int windowStart = killer->lastKills[killer->lastKillCount - 6];
        if ((now - windowStart) <= 12000) Awards_GiveAward(killerNum, AWARD_MONSTER_KILL);
    }

    if (killer->lastKillCount >= 3) {
        int recent1 = killer->lastKills[killer->lastKillCount - 1];
        int recent3 = killer->lastKills[killer->lastKillCount - 3];
        if ((recent1 - recent3) <= 1000) Awards_GiveAward(killerNum, AWARD_MULTI_SLAYER);
    }

    player_stats_t* stats = killerCl->guid[0] ? Stats_GetByGUID(killerCl->guid) : NULL;
    if (stats) {
        if (stats->currentKillStreak >= 10) Awards_GiveAward(killerNum, AWARD_STREAK_MASTER);
        if (stats->currentKillStreak >= 15) Awards_GiveAward(killerNum, AWARD_UNSTOPPABLE);
        if (stats->currentKillStreak >= 20) Awards_GiveAward(killerNum, AWARD_GODLIKE);
        if (stats->currentKillStreak >= 25) Awards_GiveAward(killerNum, AWARD_DOMINATOR);

        if (stats->kills >= 100) Awards_GiveAward(killerNum, AWARD_CENTURY);
        if (stats->kills >= 500) Awards_GiveAward(killerNum, AWARD_VETERAN);
        if (stats->kills >= 1000) Awards_GiveAward(killerNum, AWARD_LEGEND);
    }

    if (headshot) {
        if (killer->headshotsThisRound >= 3) Awards_GiveAward(killerNum, AWARD_HEADHUNTER);
        if (killer->headshotsThisRound >= 5) Awards_GiveAward(killerNum, AWARD_SHARPSHOOTER);
    }

    victim->lastKiller = killerNum;
    victim->lastDeathTime = now;
}

void Awards_CheckOnDeath(int victimNum, int killerNum) {
    if (victimNum < 0 || victimNum >= MAX_PROXY_CLIENTS) return;

    match_tracker_t* victim = &g_matchTrackers[victimNum];
    victim->deathsThisRound++;

    if (!victim->firstToDie) {
        victim->firstToDie = qtrue;
        Awards_GiveAward(victimNum, AWARD_FIRST_TO_DIE);
    }

    if (victim->deathsThisRound >= 5) {
        Awards_GiveAward(victimNum, AWARD_SUICIDE_KING);
    }
}

void Awards_CheckOnRoundEnd(void) {}
void Awards_CheckOnMedicRevive(int medicNum, int patientNum) {}
void Awards_CheckOnObjective(int clientNum, const char* objectiveType) {}

void Awards_PrintPlayerAwards(const char* guid, int clientNum) {
    player_awards_t* pa = Awards_GetPlayer(guid);
    if (!pa || pa->awardCount == 0) {
        if (clientNum >= 0) {
            Proxy_SendServerCommand(clientNum, "print \"^3No awards earned yet^7\n\"");
        } else {
            Proxy_Print("^3No awards earned yet^7\n");
        }
        return;
    }

    if (clientNum >= 0) {
        Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^3Player Awards\n\"");
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
    } else {
        Proxy_Print("^3========================================\n");
        Proxy_Print("  ^3Player Awards\n");
        Proxy_Print("^3========================================\n");
    }

    for (int i = 0; i < pa->awardCount; i++) {
        player_award_t* award = &pa->awards[i];
        award_def_t* def = NULL;
        for (int j = 0; j < MAX_AWARDS; j++) {
            if (g_awardDefs[j].id == award->awardId) {
                def = &g_awardDefs[j];
                break;
            }
        }
        if (!def) continue;

        char countStr[16];
        if (def->repeatable && award->count > 1) {
            snprintf(countStr, sizeof(countStr), "[%dx]", award->count);
        } else {
            countStr[0] = '\0';
        }

        if (clientNum >= 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^7%-16s ^3%-8s^7- %s\n\"",
                def->name, countStr, def->description);
        } else {
            Proxy_Print("  ^7%-16s ^3%-8s^7- %s\n", def->name, countStr, def->description);
        }
    }

    if (clientNum >= 0) {
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^7Total Awards: ^3%d\n\"", pa->awardCount);
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
    } else {
        Proxy_Print("^3========================================\n");
        Proxy_Print("  ^7Total Awards: ^3%d\n", pa->awardCount);
        Proxy_Print("^3========================================\n");
    }
}

// ====================================================================
// Player Profile System
// ====================================================================

static const char* PROFILES_FILE = "proxy_profiles.dat";

void Profiles_Init(void) {
    g_profileCount = 0;
    Profiles_Load();
    Proxy_Print("^3Profile system initialized^7 - %d player records loaded\n", g_profileCount);
}

void Profiles_Load(void) {
    FILE* f = fopen(PROFILES_FILE, "rb");
    if (!f) return;

    fread(&g_profileCount, sizeof(int), 1, f);
    if (g_profileCount > MAX_PROFILE_PLAYERS) g_profileCount = MAX_PROFILE_PLAYERS;
    fread(g_profiles, sizeof(player_profile_t), g_profileCount, f);
    fclose(f);
}

void Profiles_Save(void) {
    FILE* f = fopen(PROFILES_FILE, "wb");
    if (!f) return;

    fwrite(&g_profileCount, sizeof(int), 1, f);
    fwrite(g_profiles, sizeof(player_profile_t), g_profileCount, f);
    fclose(f);
}

player_profile_t* Profiles_GetOrCreate(const char* guid, const char* name) {
    player_profile_t* existing = Profiles_GetByGUID(guid);
    if (existing) {
        strncpy_s(existing->name, sizeof(existing->name), name, sizeof(existing->name) - 1);
        return existing;
    }

    if (g_profileCount >= MAX_PROFILE_PLAYERS) return NULL;

    player_profile_t* p = &g_profiles[g_profileCount++];
    memset(p, 0, sizeof(player_profile_t));
    strncpy_s(p->guid, sizeof(p->guid), guid, sizeof(p->guid) - 1);
    strncpy_s(p->name, sizeof(p->name), name, sizeof(p->name) - 1);
    p->level = 1;
    p->xpToNext = 1000;
    p->firstSeen = GetTickCount();
    return p;
}

player_profile_t* Profiles_GetByGUID(const char* guid) {
    for (int i = 0; i < g_profileCount; i++) {
        if (strcmp(g_profiles[i].guid, guid) == 0) {
            return &g_profiles[i];
        }
    }
    return NULL;
}

static const char* Profiles_GetLevelTitle(int level) {
    if (level >= 50) return "^1Legend";
    if (level >= 40) return "^4Warlord";
    if (level >= 30) return "^5Commander";
    if (level >= 20) return "^3Veteran";
    if (level >= 10) return "^6Soldier";
    if (level >= 5)  return "^7Recruit";
    return "^8Rookie";
}

void Profiles_AddXP(const char* guid, int amount) {
    player_profile_t* p = Profiles_GetByGUID(guid);
    if (!p) return;

    p->xp += amount;

    while (p->xp >= p->xpToNext) {
        p->xp -= p->xpToNext;
        p->level++;
        p->xpToNext = (int)(p->xpToNext * 1.15f);

        // Unlocks
        if (p->level == 5)  p->unlockedChatColor = qtrue;
        if (p->level == 10) p->unlockedVoteWeight = qtrue;
        if (p->level == 15) p->unlockedCustomJoinMsg = qtrue;

        // Announce level up
        for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
            if (g_clients[i].connected && !g_clients[i].isBot) {
                Proxy_SendServerCommand(i, "cp \"^6*** ^7%s ^6reached ^3Level %d^6 [%s]^6 ***\"",
                    p->name, p->level, Profiles_GetLevelTitle(p->level));
            }
        }
    }
}

void Profiles_CheckLevelUp(const char* guid) {
    // Handled in AddXP
}

qboolean Profiles_GiveRep(int giverNum, int targetNum, qboolean positive) {
    if (giverNum < 0 || giverNum >= MAX_PROXY_CLIENTS) return qfalse;
    if (targetNum < 0 || targetNum >= MAX_PROXY_CLIENTS) return qfalse;
    if (giverNum == targetNum) return qfalse;

    proxy_client_t* giver = Clients_Get(giverNum);
    proxy_client_t* target = Clients_Get(targetNum);
    if (!giver || !target || !giver->connected || !target->connected) return qfalse;
    if (!giver->guid[0] || !target->guid[0]) return qfalse;

    player_profile_t* giverProfile = Profiles_GetByGUID(giver->guid);
    player_profile_t* targetProfile = Profiles_GetByGUID(target->guid);
    if (!giverProfile || !targetProfile) return qfalse;

    // Prevent rep spam (once per match per player)
    if (giverProfile->lastRepGiven[targetNum]) return qfalse;
    giverProfile->lastRepGiven[targetNum] = 1;

    if (positive) {
        targetProfile->repPositive++;
    } else {
        targetProfile->repNegative++;
    }

    // Small XP reward for giving rep
    Profiles_AddXP(giver->guid, 10);

    return qtrue;
}

float Profiles_GetRepPercent(player_profile_t* profile) {
    if (!profile) return 50.0f;
    int total = profile->repPositive + profile->repNegative;
    if (total == 0) return 50.0f;
    return (float)profile->repPositive / (float)total * 100.0f;
}

void Profiles_RecordMatchResult(const char* guid, qboolean won, qboolean mvp) {
    player_profile_t* p = Profiles_GetByGUID(guid);
    if (!p) return;

    p->totalMatches++;

    if (p->recentMatchCount < MAX_RECENT_MATCHES) {
        match_result_t* mr = &p->recentMatches[p->recentMatchCount++];
        mr->wins = won ? 1 : 0;
        mr->losses = won ? 0 : 1;
        mr->mvpCount = mvp ? 1 : 0;
        mr->date = GetTickCount();
    } else {
        // Shift array
        for (int i = 0; i < MAX_RECENT_MATCHES - 1; i++) {
            p->recentMatches[i] = p->recentMatches[i + 1];
        }
        match_result_t* mr = &p->recentMatches[MAX_RECENT_MATCHES - 1];
        mr->wins = won ? 1 : 0;
        mr->losses = won ? 0 : 1;
        mr->mvpCount = mvp ? 1 : 0;
        mr->date = GetTickCount();
    }
}

static const char* Profiles_GetFavoriteClass(int classId) {
    switch (classId) {
        case 1: return "Soldier";
        case 2: return "Medic";
        case 3: return "Engineer";
        case 4: return "Covert Ops";
        default: return "None";
    }
}

void Profiles_PrintCard(const char* guid, int clientNum) {
    player_profile_t* p = Profiles_GetByGUID(guid);
    if (!p) {
        if (clientNum >= 0) {
            Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
        } else {
            Proxy_Print("^3Player not found^7\n");
        }
        return;
    }

    // Calculate recent trend
    int recentWins = 0, recentLosses = 0, recentMVPs = 0;
    for (int i = 0; i < p->recentMatchCount; i++) {
        recentWins += p->recentMatches[i].wins;
        recentLosses += p->recentMatches[i].losses;
        recentMVPs += p->recentMatches[i].mvpCount;
    }

    const char* title = p->customTitle[0] ? p->customTitle : Profiles_GetLevelTitle(p->level);
    float repPct = Profiles_GetRepPercent(p);

    if (clientNum >= 0) {
        Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^3Player Card: ^7[%s] %s\n\"", title, p->name);
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^7Level: ^3%d^7 (XP: ^3%d^7/^3%d^7)\n\"", p->level, p->xp, p->xpToNext);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Reputation: ^3%.0f%%^7 (+%d / -%d)\n\"", repPct, p->repPositive, p->repNegative);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Playtime: ^3%d min^7 | Matches: ^3%d\n\"", p->playtimeMinutes, p->totalMatches);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Recent: ^2%dW^7 / ^1%dL^7 | MVPs: ^6%d\n\"", recentWins, recentLosses, recentMVPs);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Fav Class: ^3%s\n\"", Profiles_GetFavoriteClass(p->favoriteClass));
        if (p->clanTag[0]) {
            Proxy_SendServerCommand(clientNum, "print \"  ^7Clan: ^3[%s]\n\"", p->clanTag);
        }
        if (p->signature[0]) {
            Proxy_SendServerCommand(clientNum, "print \"  ^7Signature: ^6\"%s\"\n\"", p->signature);
        }
        if (p->friendCount > 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^7Friends: ^3%d\n\"", p->friendCount);
        }
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
    } else {
        Proxy_Print("^3========================================\n");
        Proxy_Print("  ^3Player Card: ^7[%s] %s\n", title, p->name);
        Proxy_Print("^3========================================\n");
        Proxy_Print("  ^7Level: ^3%d^7 (XP: ^3%d^7/^3%d^7)\n", p->level, p->xp, p->xpToNext);
        Proxy_Print("  ^7Reputation: ^3%.0f%%^7 (+%d / -%d)\n", repPct, p->repPositive, p->repNegative);
        Proxy_Print("  ^7Playtime: ^3%d min^7 | Matches: ^3%d\n", p->playtimeMinutes, p->totalMatches);
        Proxy_Print("  ^7Recent: ^2%dW^7 / ^1%dL^7 | MVPs: ^6%d\n", recentWins, recentLosses, recentMVPs);
        Proxy_Print("  ^7Fav Class: ^3%s\n", Profiles_GetFavoriteClass(p->favoriteClass));
        if (p->clanTag[0]) {
            Proxy_Print("  ^7Clan: ^3[%s]\n", p->clanTag);
        }
        if (p->signature[0]) {
            Proxy_Print("  ^7Signature: ^6\"%s\"\n", p->signature);
        }
        if (p->friendCount > 0) {
            Proxy_Print("  ^7Friends: ^3%d\n", p->friendCount);
        }
        Proxy_Print("^3========================================\n");
    }
}

qboolean Profiles_HandleCommand(int clientNum, const char* cmd, int argc) {
    if (_stricmp(cmd, "profile") == 0 || _stricmp(cmd, "card") == 0) {
        char arg[128] = {0};
        if (argc >= 2) {
            g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));
        }

        if (arg[0] == '\0') {
            proxy_client_t* cl = Clients_Get(clientNum);
            if (cl && cl->connected) {
                if (!cl->guid[0]) {
                    Proxy_SendServerCommand(clientNum, "print \"^3Profile unavailable: No GUID detected^7\n\"");
                    return qtrue;
                }
                Profiles_GetOrCreate(cl->guid, cl->name);
                Profiles_PrintCard(cl->guid, clientNum);
            } else {
                Proxy_SendServerCommand(clientNum, "print \"^3Could not determine your profile^7\n\"");
            }
            return qtrue;
        } else {
            int cn = atoi(arg);
            proxy_client_t* targetCl = NULL;

            if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
                targetCl = Clients_Get(cn);
            }

            if (!targetCl) {
                for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
                    if (g_clients[i].connected && strstr(g_clients[i].name, arg)) {
                        targetCl = &g_clients[i];
                        break;
                    }
                }
            }

            if (targetCl && targetCl->connected) {
                if (!targetCl->guid[0]) {
                    Proxy_SendServerCommand(clientNum, "print \"^3%s has no GUID^7\n\"", targetCl->name);
                    return qtrue;
                }
                Profiles_GetOrCreate(targetCl->guid, targetCl->name);
                Profiles_PrintCard(targetCl->guid, clientNum);
            } else {
                Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
            }
        }
        return qtrue;
    }

    if (_stricmp(cmd, "sig") == 0 || _stricmp(cmd, "signature") == 0) {
        proxy_client_t* cl = Clients_Get(clientNum);
        if (!cl || !cl->connected || !cl->guid[0]) {
            Proxy_SendServerCommand(clientNum, "print \"^3Could not set signature^7\n\"");
            return qtrue;
        }

        player_profile_t* p = Profiles_GetByGUID(cl->guid);
        if (!p) {
            p = Profiles_GetOrCreate(cl->guid, cl->name);
        }

        // Build signature text from all args
        char sigText[MAX_SIGNATURE_LEN] = {0};
        int offset = 0;
        for (int i = 1; i < argc; i++) {
            char arg[128];
            g_engine_syscall(G_ARGV, i, arg, sizeof(arg));
            if (i > 1) {
                offset += snprintf(sigText + offset, sizeof(sigText) - offset, " ");
            }
            offset += snprintf(sigText + offset, sizeof(sigText) - offset, "%s", arg);
        }

        if (sigText[0] == '\0') {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7sig <your signature>\n\"");
            return qtrue;
        }

        // Sanitize: strip color codes to prevent abuse
        char clean[MAX_SIGNATURE_LEN];
        int j = 0;
        for (int i = 0; sigText[i] && j < MAX_SIGNATURE_LEN - 1; i++) {
            if (sigText[i] == '^' && sigText[i + 1] >= '0' && sigText[i + 1] <= '9') {
                i++; // Skip color code
                continue;
            }
            clean[j++] = sigText[i];
        }
        clean[j] = '\0';

        strncpy_s(p->signature, sizeof(p->signature), clean, sizeof(p->signature) - 1);
        Proxy_SendServerCommand(clientNum, "print \"^2Signature set: ^6\"%s\"\n\"", clean);
        return qtrue;
    }

    if (_stricmp(cmd, "title") == 0) {
        proxy_client_t* cl = Clients_Get(clientNum);
        if (!cl || !cl->connected || !cl->guid[0]) return qtrue;

        player_profile_t* p = Profiles_GetByGUID(cl->guid);
        if (!p) p = Profiles_GetOrCreate(cl->guid, cl->name);

        if (argc < 2) {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7title <custom title>\n\"");
            Proxy_SendServerCommand(clientNum, "print \"^7Current: ^3%s\n\"", p->customTitle[0] ? p->customTitle : "Auto (based on level)");
            return qtrue;
        }

        char titleText[MAX_TITLE_LEN] = {0};
        int offset = 0;
        for (int i = 1; i < argc; i++) {
            char arg[128];
            g_engine_syscall(G_ARGV, i, arg, sizeof(arg));
            if (i > 1) offset += snprintf(titleText + offset, sizeof(titleText) - offset, " ");
            offset += snprintf(titleText + offset, sizeof(titleText) - offset, "%s", arg);
        }

        strncpy_s(p->customTitle, sizeof(p->customTitle), titleText, sizeof(p->customTitle) - 1);
        Proxy_SendServerCommand(clientNum, "print \"^2Title set: ^3%s\n\"", titleText);
        return qtrue;
    }

    if (_stricmp(cmd, "clan") == 0) {
        proxy_client_t* cl = Clients_Get(clientNum);
        if (!cl || !cl->connected || !cl->guid[0]) return qtrue;

        player_profile_t* p = Profiles_GetByGUID(cl->guid);
        if (!p) p = Profiles_GetOrCreate(cl->guid, cl->name);

        if (argc < 2) {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7clan <tag>\n\"");
            return qtrue;
        }

        char tag[MAX_CLAN_TAG_LEN];
        g_engine_syscall(G_ARGV, 1, tag, sizeof(tag));
        strncpy_s(p->clanTag, sizeof(p->clanTag), tag, sizeof(p->clanTag) - 1);
        Proxy_SendServerCommand(clientNum, "print \"^2Clan tag set: ^3[%s]\n\"", tag);
        return qtrue;
    }

    if (_stricmp(cmd, "rep") == 0) {
        if (argc < 3) {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7rep <+|-> <player>\n\"");
            return qtrue;
        }

        char dir[8], targetArg[128];
        g_engine_syscall(G_ARGV, 1, dir, sizeof(dir));
        g_engine_syscall(G_ARGV, 2, targetArg, sizeof(targetArg));

        qboolean positive = (_stricmp(dir, "+") == 0 || _stricmp(dir, "pos") == 0 || _stricmp(dir, "good") == 0);

        int targetNum = -1;
        int cn = atoi(targetArg);
        if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
            targetNum = cn;
        } else {
            for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
                if (g_clients[i].connected && strstr(g_clients[i].name, targetArg)) {
                    targetNum = i;
                    break;
                }
            }
        }

        if (targetNum < 0) {
            Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
            return qtrue;
        }

        if (Profiles_GiveRep(clientNum, targetNum, positive)) {
            proxy_client_t* target = Clients_Get(targetNum);
            if (positive) {
                Proxy_SendServerCommand(clientNum, "print \"^2Reputation given to ^7%s^2 (+)\n\"", target ? target->name : "player");
                if (target) {
                    Proxy_SendServerCommand(targetNum, "print \"^7%s ^2gave you a positive reputation (+)\n\"",
                        g_clients[clientNum].name);
                }
            } else {
                Proxy_SendServerCommand(clientNum, "print \"^1Reputation given to ^7%s^1 (-)\n\"", target ? target->name : "player");
                if (target) {
                    Proxy_SendServerCommand(targetNum, "print \"^7%s ^1gave you a negative reputation (-)\n\"",
                        g_clients[clientNum].name);
                }
            }
        } else {
            Proxy_SendServerCommand(clientNum, "print \"^3Cannot give reputation (already given this match or self)^7\n\"");
        }
        return qtrue;
    }

    if (_stricmp(cmd, "lfg") == 0) {
        proxy_client_t* cl = Clients_Get(clientNum);
        if (!cl || !cl->connected || !cl->guid[0]) return qtrue;

        player_profile_t* p = Profiles_GetByGUID(cl->guid);
        if (!p) p = Profiles_GetOrCreate(cl->guid, cl->name);

        p->lookingForGroup = !p->lookingForGroup;

        if (p->lookingForGroup) {
            for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
                if (g_clients[i].connected && !g_clients[i].isBot) {
                    Proxy_SendServerCommand(i, "print \"^7%s ^3is looking for a group!^7\n\"", cl->name);
                }
            }
        } else {
            Proxy_SendServerCommand(clientNum, "print \"^3LFG status cleared^7\n\"");
        }
        return qtrue;
    }

    return qfalse;
}

// ====================================================================
// Autobalance System
// ====================================================================
// Autobalance System
// ====================================================================

void Balance_UpdatePlayerSkills(void) {
    g_balanceCount = 0;

    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        if (g_clients[i].connected && !g_clients[i].isBot && g_clients[i].guid[0]) {
            player_stats_t* stats = Stats_GetByGUID(g_clients[i].guid);
            balance_player_t* bp = &g_balancePlayers[g_balanceCount];

            bp->clientNum = i;
            strncpy_s(bp->guid, sizeof(bp->guid), g_clients[i].guid, sizeof(bp->guid) - 1);
            strncpy_s(bp->name, sizeof(bp->name), g_clients[i].name, sizeof(bp->name) - 1);
            bp->team = TEAM_FREE;
            bp->skill = stats ? Stats_GetSkillRating(stats) : 0.0f;

            char userinfo[1024];
            if (g_engine_syscall) {
                g_engine_syscall(G_GET_USERINFO, i, userinfo, sizeof(userinfo));
                const char* teamStr = strstr(userinfo, "\\team\\");
                if (teamStr) {
                    teamStr += 6;
                    int teamVal = atoi(teamStr);
                    if (teamVal == TEAM_ALLIES || teamVal == TEAM_AXIS) {
                        bp->team = teamVal;
                    }
                }
            }

            g_balanceCount++;
        }
    }
}

team_balance_t Balance_GetInfo(void) {
    team_balance_t info = { 0 };

    for (int i = 0; i < g_balanceCount; i++) {
        if (g_balancePlayers[i].team == TEAM_ALLIES) {
            info.alliesSkill += g_balancePlayers[i].skill;
            info.alliesCount++;
        } else if (g_balancePlayers[i].team == TEAM_AXIS) {
            info.axisSkill += g_balancePlayers[i].skill;
            info.axisCount++;
        }
    }

    return info;
}

qboolean Balance_NeedsBalance(void) {
    if (!proxy_autobalance.integer) return qfalse;

    Balance_UpdatePlayerSkills();
    team_balance_t info = Balance_GetInfo();

    int totalPlayers = info.alliesCount + info.axisCount;
    if (totalPlayers < proxy_balance_minplayers.integer) return qfalse;

    int countDiff = info.alliesCount - info.axisCount;
    if (countDiff < -1 || countDiff > 1) return qtrue;

    float skillDiff = info.alliesSkill - info.axisSkill;
    if (skillDiff < 0.0f) skillDiff = -skillDiff;

    return skillDiff > (float)proxy_balance_threshold.integer;
}

int Balance_GetBestTeam(void) {
    Balance_UpdatePlayerSkills();
    team_balance_t info = Balance_GetInfo();

    if (info.alliesCount < info.axisCount - 1) return TEAM_ALLIES;
    if (info.axisCount < info.alliesCount - 1) return TEAM_AXIS;

    if (info.alliesSkill <= info.axisSkill) return TEAM_ALLIES;
    return TEAM_AXIS;
}

static void Balance_SwitchPlayer(int clientNum, int newTeam) {
    if (!g_engine_syscall) return;

    char userinfo[1024];
    g_engine_syscall(G_GET_USERINFO, clientNum, userinfo, sizeof(userinfo));

    char newInfo[1024];
    const char* teamStr = strstr(userinfo, "\\team\\");
    if (teamStr) {
        const char* afterTeam = strchr(teamStr + 6, '\\');
        int prefixLen = (int)(teamStr - userinfo);
        strncpy_s(newInfo, sizeof(newInfo), userinfo, prefixLen);
        char teamVal[16];
        snprintf(teamVal, sizeof(teamVal), "\\team\\%d", newTeam);
        strncat_s(newInfo, sizeof(newInfo), teamVal, _TRUNCATE);
        if (afterTeam) {
            strncat_s(newInfo, sizeof(newInfo), afterTeam, _TRUNCATE);
        }
    } else {
        strncpy_s(newInfo, sizeof(newInfo), userinfo, sizeof(newInfo) - 1);
        char teamVal[32];
        snprintf(teamVal, sizeof(teamVal), "\\team\\%d", newTeam);
        strncat_s(newInfo, sizeof(newInfo), teamVal, _TRUNCATE);
    }

    g_engine_syscall(G_SET_USERINFO, clientNum, newInfo);
}

void Balance_AutoBalance(void) {
    if (!proxy_autobalance.integer) return;

    Balance_UpdatePlayerSkills();
    team_balance_t info = Balance_GetInfo();

    int totalPlayers = info.alliesCount + info.axisCount;
    if (totalPlayers < proxy_balance_minplayers.integer) return;

    float skillDiff = info.alliesSkill - info.axisSkill;
    if (skillDiff < 0.0f) skillDiff = -skillDiff;

    if (skillDiff <= (float)proxy_balance_threshold.integer) return;

    Proxy_Print("^3Autobalance triggered! Skill diff: ^6%.1f^7 (threshold: ^6%d^7)\n",
        skillDiff, proxy_balance_threshold.integer);

    int strongTeam, weakTeam;
    if (info.alliesSkill > info.axisSkill) {
        strongTeam = TEAM_ALLIES;
        weakTeam = TEAM_AXIS;
    } else {
        strongTeam = TEAM_AXIS;
        weakTeam = TEAM_ALLIES;
    }

    balance_player_t* weakest = NULL;
    float weakestSkill = 99999.0f;
    for (int i = 0; i < g_balanceCount; i++) {
        if (g_balancePlayers[i].team == strongTeam) {
            if (g_balancePlayers[i].skill < weakestSkill) {
                weakestSkill = g_balancePlayers[i].skill;
                weakest = &g_balancePlayers[i];
            }
        }
    }

    if (weakest) {
        Proxy_Print("^3Moving ^7%s ^3(skill: ^6%.1f^3) from ^7%s ^3to ^7%s\n",
            weakest->name, weakestSkill,
            strongTeam == TEAM_ALLIES ? "Allies" : "Axis",
            weakTeam == TEAM_ALLIES ? "Allies" : "Axis");

        Balance_SwitchPlayer(weakest->clientNum, weakTeam);

        Proxy_SendServerCommand(weakest->clientNum,
            "cp \"^3You have been moved to %s for team balance\"",
            weakTeam == TEAM_ALLIES ? "Allies" : "Axis");
    }
}

void Balance_PrintStatus(void) {
    Balance_UpdatePlayerSkills();
    team_balance_t info = Balance_GetInfo();

    Proxy_Print("^3========================================\n");
    Proxy_Print("^3  Team Balance Status\n");
    Proxy_Print("^3========================================\n");
    Proxy_Print("  ^7Allies: ^2%d^7 players | Skill: ^3%.1f\n", info.alliesCount, info.alliesSkill);
    Proxy_Print("  ^7Axis:   ^1%d^7 players | Skill: ^3%.1f\n", info.axisCount, info.axisSkill);

    float diff = info.alliesSkill - info.axisSkill;
    if (diff < 0.0f) diff = -diff;
    Proxy_Print("  ^7Skill Diff: ^6%.1f^7 (threshold: ^6%d^7)\n", diff, proxy_balance_threshold.integer);
    Proxy_Print("  ^7Autobalance: %s\n", proxy_autobalance.integer ? "^2ON" : "^1OFF");
    Proxy_Print("  ^7Min Players: ^3%d\n", proxy_balance_minplayers.integer);
    Proxy_Print("^3========================================\n");

    if (info.alliesCount > 0) {
        Proxy_Print("^2  Allies:\n");
        for (int i = 0; i < g_balanceCount; i++) {
            if (g_balancePlayers[i].team == TEAM_ALLIES) {
                Proxy_Print("    ^7%-20s ^3%.1f ^7[%s]\n",
                    g_balancePlayers[i].name, g_balancePlayers[i].skill,
                    Stats_GetSkillTier(g_balancePlayers[i].skill));
            }
        }
    }
    if (info.axisCount > 0) {
        Proxy_Print("^1  Axis:\n");
        for (int i = 0; i < g_balanceCount; i++) {
            if (g_balancePlayers[i].team == TEAM_AXIS) {
                Proxy_Print("    ^7%-20s ^3%.1f ^7[%s]\n",
                    g_balancePlayers[i].name, g_balancePlayers[i].skill,
                    Stats_GetSkillTier(g_balancePlayers[i].skill));
            }
        }
    }
    Proxy_Print("^3========================================\n");
}

qboolean Balance_HandleCommand(const char* cmdLine, int clientNum) {
    char cmd[256];
    const char* p = cmdLine;
    while (*p == ' ') p++;

    int i = 0;
    while (*p && *p != ' ' && i < 255) {
        cmd[i++] = *p++;
    }
    cmd[i] = '\0';

    if (_stricmp(cmd, "balance") == 0 || _stricmp(cmd, "proxy_balance") == 0) {
        if (clientNum >= 0) {
            Balance_UpdatePlayerSkills();
            team_balance_t info = Balance_GetInfo();
            Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"  ^3Team Balance Status\n\"");
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"  ^7Allies: ^2%d^7 | Skill: ^3%.1f\n\"", info.alliesCount, info.alliesSkill);
            Proxy_SendServerCommand(clientNum, "print \"  ^7Axis:   ^1%d^7 | Skill: ^3%.1f\n\"", info.axisCount, info.axisSkill);
            float diff = info.alliesSkill - info.axisSkill;
            if (diff < 0.0f) diff = -diff;
            Proxy_SendServerCommand(clientNum, "print \"  ^7Skill Diff: ^6%.1f^7\n\"", diff);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else {
            Balance_PrintStatus();
        }
        return qtrue;
    }

    if (_stricmp(cmd, "forcebalance") == 0 || _stricmp(cmd, "proxy_forcebalance") == 0) {
        if (clientNum < 0) {
            Balance_AutoBalance();
        } else {
            Proxy_SendServerCommand(clientNum, "print \"^3Only admins can force balance^7\n\"");
        }
        return qtrue;
    }

    if (_stricmp(cmd, "skill") == 0) {
        if (clientNum < 0) return qfalse;

        char arg[128] = {0};
        while (*p == ' ') p++;
        i = 0;
        while (*p && *p != ' ' && i < 127) {
            arg[i++] = *p++;
        }
        arg[i] = '\0';

        proxy_client_t* target = NULL;
        if (arg[0] == '\0') {
            target = Clients_Get(clientNum);
        } else {
            int cn = atoi(arg);
            if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
                target = Clients_Get(cn);
            }
            if (!target) {
                for (int j = 0; j < MAX_PROXY_CLIENTS; j++) {
                    if (g_clients[j].connected && strstr(g_clients[j].name, arg)) {
                        target = &g_clients[j];
                        break;
                    }
                }
            }
        }

        if (target && target->connected && target->guid[0]) {
            player_stats_t* stats = Stats_GetByGUID(target->guid);
            float skill = stats ? Stats_GetSkillRating(stats) : 0.0f;
            float kd = stats ? Stats_GetKDRatio(stats) : 0.0f;
            float acc = stats ? Stats_GetAccuracy(stats) : 0.0f;

            Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"  ^3Skill Rating: ^7%s\n\"", target->name);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"  ^7Rating: ^3%.1f^7 / 1000\n\"", skill);
            Proxy_SendServerCommand(clientNum, "print \"  ^7Tier: %s^7\n\"", Stats_GetSkillTier(skill));
            Proxy_SendServerCommand(clientNum, "print \"  ^7K/D: ^3%.2f ^7(40%% weight)\n\"", kd);
            Proxy_SendServerCommand(clientNum, "print \"  ^7Accuracy: ^3%.1f%% ^7(35%% weight)\n\"", acc);
            Proxy_SendServerCommand(clientNum, "print \"  ^7Kills: ^3%d ^7(15%% weight)\n\"", stats ? stats->kills : 0);
            Proxy_SendServerCommand(clientNum, "print \"  ^7Best Streak: ^3%d ^7(10%% weight)\n\"", stats ? stats->longestKillStreak : 0);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else {
            Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
        }
        return qtrue;
    }

    return qfalse;
}

// ====================================================================
// Client Management
// ====================================================================

void Clients_Init(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_bans, 0, sizeof(g_bans));

    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        g_clients[i].clientNum = i;
        g_clients[i].connected = qfalse;
    }

    Proxy_Print("^3Client tracking initialized^7\n");
}

proxy_client_t* Clients_Get(int clientNum) {
    if (clientNum < 0 || clientNum >= MAX_PROXY_CLIENTS) return NULL;
    return &g_clients[clientNum];
}

static void GetClientIP(int clientNum, char* ip, int ipLen) {
    char userinfo[1024];
    if (g_engine_syscall) {
        g_engine_syscall(G_GET_USERINFO, clientNum, userinfo, sizeof(userinfo));

        const char* ipStr = strstr(userinfo, "\\ip\\");
        if (ipStr) {
            ipStr += 4;
            const char* end = strchr(ipStr, '\\');
            if (end) {
                int len = (int)(end - ipStr);
                if (len >= ipLen) len = ipLen - 1;
                strncpy_s(ip, ipLen, ipStr, len);
                ip[len] = '\0';
            } else {
                strncpy_s(ip, ipLen, ipStr, ipLen - 1);
            }
        }
    }
}

static void GetClientName(int clientNum, char* name, int nameLen) {
    char userinfo[1024];
    if (g_engine_syscall) {
        g_engine_syscall(G_GET_USERINFO, clientNum, userinfo, sizeof(userinfo));

        const char* nameStr = strstr(userinfo, "\\name\\");
        if (nameStr) {
            nameStr += 6;
            const char* end = strchr(nameStr, '\\');
            if (end) {
                int len = (int)(end - nameStr);
                if (len >= nameLen) len = nameLen - 1;
                strncpy_s(name, nameLen, nameStr, len);
                name[len] = '\0';
            } else {
                strncpy_s(name, nameLen, nameStr, nameLen - 1);
            }
        }
    }
}

static void GetClientGUID(int clientNum, char* guid, int guidLen) {
    char userinfo[1024];
    if (g_engine_syscall) {
        g_engine_syscall(G_GET_USERINFO, clientNum, userinfo, sizeof(userinfo));

        const char* guidStr = strstr(userinfo, "\\cl_guid\\");
        if (guidStr) {
            guidStr += 9;
            const char* end = strchr(guidStr, '\\');
            if (end) {
                int len = (int)(end - guidStr);
                if (len >= guidLen) len = guidLen - 1;
                strncpy_s(guid, guidLen, guidStr, len);
                guid[len] = '\0';
            } else {
                strncpy_s(guid, guidLen, guidStr, guidLen - 1);
            }
        }
    }
}

const char* Clients_OnConnect(int clientNum, qboolean firstTime, qboolean isBot) {
    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl) return NULL;

    GetClientIP(clientNum, cl->ip, sizeof(cl->ip));
    GetClientName(clientNum, cl->name, sizeof(cl->name));
    GetClientGUID(clientNum, cl->guid, sizeof(cl->guid));

    cl->clientNum = clientNum;
    cl->isBot = isBot;
    cl->connected = qtrue;

    if (cl->guid[0]) {
        Stats_GetOrCreate(cl->guid, cl->name);
    }

    if (!isBot) {
        if (Clients_IsIPBanned(cl->ip)) {
            Proxy_Print("^1Banned client %s (%s) attempted to connect\n^7", cl->name, cl->ip);
            return "You are banned from this server";
        }

        Proxy_Print("^2Client connected: ^7%s (%d) - %s\n", cl->name, clientNum, cl->ip);
    }

    return NULL;
}

void Clients_OnBegin(int clientNum) {
    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl) return;

    GetClientName(clientNum, cl->name, sizeof(cl->name));

    if (!cl->isBot) {
        Proxy_Print("^2Client spawned: ^7%s (%d)\n", cl->name, clientNum);
    }
}

void Clients_OnDisconnect(int clientNum) {
    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl) return;

    if (cl->connected && !cl->isBot) {
        Proxy_Print("^3Client disconnected: ^7%s (%d)\n", cl->name, clientNum);
    }

    memset(cl, 0, sizeof(proxy_client_t));
    cl->clientNum = clientNum;
    cl->connected = qfalse;
}

void Clients_OnUserInfoChanged(int clientNum) {
    proxy_client_t* cl = Clients_Get(clientNum);
    if (!cl || !cl->connected) return;

    char oldName[64];
    strncpy_s(oldName, sizeof(oldName), cl->name, sizeof(oldName) - 1);

    GetClientName(clientNum, cl->name, sizeof(cl->name));

    if (strcmp(oldName, cl->name) != 0 && !cl->isBot) {
        Proxy_Print("^6Client renamed: ^7%s -> %s (%d)\n", oldName, cl->name, clientNum);

        if (cl->guid[0]) {
            player_stats_t* stats = Stats_GetByGUID(cl->guid);
            if (stats) {
                strncpy_s(stats->name, sizeof(stats->name), cl->name, sizeof(stats->name) - 1);
            }
        }
    }
}

void Clients_Kick(int clientNum, const char* reason) {
    if (g_engine_syscall) {
        g_engine_syscall(G_DROP_CLIENT, clientNum, reason);
    }
}

void Clients_BanIP(const char* ip, const char* reason) {
    for (int i = 0; i < MAX_BANS; i++) {
        if (!g_bans[i].active) {
            strncpy_s(g_bans[i].ip, sizeof(g_bans[i].ip), ip, sizeof(g_bans[i].ip) - 1);
            strncpy_s(g_bans[i].reason, sizeof(g_bans[i].reason), reason, sizeof(g_bans[i].reason) - 1);
            g_bans[i].active = qtrue;
            Proxy_Print("^1Banned IP: ^7%s - %s\n", ip, reason);
            return;
        }
    }
    Proxy_Print("^1Ban list full, cannot add %s\n", ip);
}

qboolean Clients_IsIPBanned(const char* ip) {
    for (int i = 0; i < MAX_BANS; i++) {
        if (g_bans[i].active && strcmp(g_bans[i].ip, ip) == 0) {
            return qtrue;
        }
    }
    return qfalse;
}

void Clients_PrintList(void) {
    Proxy_Print("^3Connected Clients:^7\n");
    int count = 0;
    for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
        if (g_clients[i].connected) {
            Proxy_Print("  [%d] %s - %s - GUID: %s\n",
                g_clients[i].clientNum,
                g_clients[i].name,
                g_clients[i].ip,
                g_clients[i].guid);
            count++;
        }
    }
    Proxy_Print("  Total: %d clients\n", count);
}

// ====================================================================
// Console Commands
// ====================================================================

static qboolean HandleClientCommand(int clientNum) {
    char cmd[256];
    if (!g_engine_syscall) return qfalse;

    int argc = g_engine_syscall(G_ARGC);
    if (argc < 1) return qfalse;

    g_engine_syscall(G_ARGV, 0, cmd, sizeof(cmd));

    // Stats commands
    if (_stricmp(cmd, "stats") == 0) {
        char arg[128] = {0};
        if (argc >= 2) {
            g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));
        }

        if (arg[0] == '\0') {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7stats <name or GUID>\n\"");
            return qtrue;
        }

        player_stats_t* stats = NULL;

        if (_stricmp(arg, "me") == 0) {
            proxy_client_t* cl = Clients_Get(clientNum);
            if (cl && cl->connected && cl->guid[0]) {
                stats = Stats_GetByGUID(cl->guid);
            }
        } else {
            stats = Stats_GetByGUID(arg);

            if (!stats) {
                int cn = atoi(arg);
                if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
                    proxy_client_t* cl = Clients_Get(cn);
                    if (cl && cl->connected && cl->guid[0]) {
                        stats = Stats_GetByGUID(cl->guid);
                    }
                }
            }

            if (!stats) {
                for (int j = 0; j < g_statsCount; j++) {
                    if (strstr(g_stats[j].name, arg) != NULL) {
                        stats = &g_stats[j];
                        break;
                    }
                }
            }
        }

        if (!stats) {
            Proxy_SendServerCommand(clientNum, "print \"^3No stats found for: %s\n\"", arg);
            return qtrue;
        }

        Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^3Player Stats: ^7%s\n\"", stats->name);
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        Proxy_SendServerCommand(clientNum, "print \"  ^7Kills: ^2%d^7  Deaths: ^1%d^7  K/D: ^3%.2f\n\"",
            stats->kills, stats->deaths, Stats_GetKDRatio(stats));
        Proxy_SendServerCommand(clientNum, "print \"  ^7Accuracy: ^3%.1f%%^7  Headshots: ^6%d^7  HS%%: ^6%.1f%%\n\"",
            Stats_GetAccuracy(stats), stats->headshots,
            stats->kills > 0 ? (float)stats->headshots / (float)stats->kills * 100.0f : 0.0f);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Shots: ^3%d^7  Hits: ^2%d^7  TK: ^1%d^7  Suicide: ^6%d\n\"",
            stats->shotsFired, stats->shotsHit, stats->teamKills, stats->suicides);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Longest Streak: ^3%d^7  Current: ^2%d\n\"",
            stats->longestKillStreak, stats->currentKillStreak);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Damage Dealt: ^3%.0f^7  Taken: ^1%.0f\n\"",
            stats->totalDamageDealt, stats->totalDamageTaken);
        Proxy_SendServerCommand(clientNum, "print \"  ^7Playtime: ^3%d min\n\"",
            stats->totalPlaytime / 60000);
        Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        return qtrue;
    }

    if (_stricmp(cmd, "top") == 0) {
        char category[32] = {0};
        if (argc >= 2) {
            g_engine_syscall(G_ARGV, 1, category, sizeof(category));
        }

        int count = 10;
        if (argc >= 3) {
            char countStr[16];
            g_engine_syscall(G_ARGV, 2, countStr, sizeof(countStr));
            count = atoi(countStr);
            if (count < 1 || count > 10) count = 10;
        }

        if (category[0] == '\0') {
            Proxy_SendServerCommand(clientNum, "print \"^3Usage: ^7top <kills|accuracy|kd|headshots>\n\"");
            return qtrue;
        }

        Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");

        if (_stricmp(category, "kills") == 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^3Top %d Players by Kills\n\"", count);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");

            player_stats_t sorted[MAX_STATS_ENTRIES];
            memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
            qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByKills);
            if (count > g_statsCount) count = g_statsCount;

            for (int j = 0; j < count; j++) {
                Proxy_SendServerCommand(clientNum, "print \"  ^7%2d. ^3%-20s ^2%-5d ^7K/D: ^3%.2f ^7HS: ^6%d\n\"",
                    j + 1, sorted[j].name, sorted[j].kills,
                    Stats_GetKDRatio(&sorted[j]), sorted[j].headshots);
            }
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else if (_stricmp(category, "accuracy") == 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^3Top %d Players by Accuracy\n\"", count);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");

            player_stats_t sorted[MAX_STATS_ENTRIES];
            memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
            qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByAccuracy);
            if (count > g_statsCount) count = g_statsCount;

            for (int j = 0; j < count; j++) {
                Proxy_SendServerCommand(clientNum, "print \"  ^7%2d. ^3%-20s ^3%.1f%% ^7(%d/%d)\n\"",
                    j + 1, sorted[j].name, Stats_GetAccuracy(&sorted[j]),
                    sorted[j].shotsHit, sorted[j].shotsFired);
            }
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else if (_stricmp(category, "kd") == 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^3Top %d Players by K/D Ratio\n\"", count);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");

            player_stats_t sorted[MAX_STATS_ENTRIES];
            memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
            qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByKD);
            if (count > g_statsCount) count = g_statsCount;

            for (int j = 0; j < count; j++) {
                Proxy_SendServerCommand(clientNum, "print \"  ^7%2d. ^3%-20s ^3%.2f ^7(%dK/%dD)\n\"",
                    j + 1, sorted[j].name, Stats_GetKDRatio(&sorted[j]),
                    sorted[j].kills, sorted[j].deaths);
            }
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else if (_stricmp(category, "headshots") == 0) {
            Proxy_SendServerCommand(clientNum, "print \"  ^3Top %d Players by Headshots\n\"", count);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");

            player_stats_t sorted[MAX_STATS_ENTRIES];
            memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
            qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByHeadshots);
            if (count > g_statsCount) count = g_statsCount;

            for (int j = 0; j < count; j++) {
                Proxy_SendServerCommand(clientNum, "print \"  ^7%2d. ^3%-20s ^6%-5d ^7HS Rate: ^3%.1f%%\n\"",
                    j + 1, sorted[j].name, sorted[j].headshots,
                    sorted[j].kills > 0 ? (float)sorted[j].headshots / (float)sorted[j].kills * 100.0f : 0.0f);
            }
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        } else {
            Proxy_SendServerCommand(clientNum, "print \"^3Unknown category: %s^7\n\"", category);
            Proxy_SendServerCommand(clientNum, "print \"^7Valid: kills, accuracy, kd, headshots\n\"");
        }
        return qtrue;
    }

    if (_stricmp(cmd, "rank") == 0) {
        proxy_client_t* cl = Clients_Get(clientNum);
        if (!cl || !cl->connected || !cl->guid[0]) {
            Proxy_SendServerCommand(clientNum, "print \"^3Could not determine your stats^7\n\"");
            return qtrue;
        }

        player_stats_t* myStats = Stats_GetByGUID(cl->guid);
        if (!myStats) {
            Proxy_SendServerCommand(clientNum, "print \"^3No stats recorded yet^7\n\"");
            return qtrue;
        }

        player_stats_t sorted[MAX_STATS_ENTRIES];
        memcpy(sorted, g_stats, sizeof(player_stats_t) * g_statsCount);
        qsort(sorted, g_statsCount, sizeof(player_stats_t), CompareByKills);

        int rank = -1;
        for (int j = 0; j < g_statsCount; j++) {
            if (strcmp(sorted[j].guid, cl->guid) == 0) {
                rank = j + 1;
                break;
            }
        }

        if (rank > 0) {
            Proxy_SendServerCommand(clientNum, "print \"\n^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"  ^3Your Server Rank\n\"");
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
            Proxy_SendServerCommand(clientNum, "print \"^7Rank: ^3#%d^7 out of ^3%d^7 players\n\"", rank, g_statsCount);
            Proxy_SendServerCommand(clientNum, "print \"^7Kills: ^2%d^7  Deaths: ^1%d^7  K/D: ^3%.2f\n\"",
                myStats->kills, myStats->deaths, Stats_GetKDRatio(myStats));
            Proxy_SendServerCommand(clientNum, "print \"^7Accuracy: ^3%.1f%%^7  Headshots: ^6%d\n\"",
                Stats_GetAccuracy(myStats), myStats->headshots);
            Proxy_SendServerCommand(clientNum, "print \"^7Longest Streak: ^3%d\n\"", myStats->longestKillStreak);
            Proxy_SendServerCommand(clientNum, "print \"^3========================================\n\"");
        }
        return qtrue;
    }

    // Awards commands
    if (_stricmp(cmd, "awards") == 0) {
        char arg[128] = {0};
        if (argc >= 2) {
            g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));
        }

        if (arg[0] == '\0') {
            proxy_client_t* cl = Clients_Get(clientNum);
            if (cl && cl->connected && cl->guid[0]) {
                Awards_PrintPlayerAwards(cl->guid, clientNum);
            } else {
                Proxy_SendServerCommand(clientNum, "print \"^3Could not determine your awards^7\n\"");
            }
        } else {
            int cn = atoi(arg);
            if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
                proxy_client_t* cl = Clients_Get(cn);
                if (cl && cl->connected && cl->guid[0]) {
                    Awards_PrintPlayerAwards(cl->guid, clientNum);
                } else {
                    Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
                }
            } else {
                qboolean found = qfalse;
                for (int i = 0; i < MAX_PROXY_CLIENTS; i++) {
                    if (g_clients[i].connected && strstr(g_clients[i].name, arg)) {
                        Awards_PrintPlayerAwards(g_clients[i].guid, clientNum);
                        found = qtrue;
                        break;
                    }
                }
                if (!found) {
                    Proxy_SendServerCommand(clientNum, "print \"^3Player not found^7\n\"");
                }
            }
        }
        return qtrue;
    }

    // Autobalance commands
    if (Balance_HandleCommand(cmd, clientNum)) {
        return qtrue;
    }

    // Profile commands
    if (Profiles_HandleCommand(clientNum, cmd, argc)) {
        return qtrue;
    }

    return qfalse;
}

qboolean Proxy_HandleCommand(void) {
    char cmd[256];
    if (!g_engine_syscall) return qfalse;

    int argc = g_engine_syscall(G_ARGC);
    if (argc < 1) return qfalse;

    g_engine_syscall(G_ARGV, 0, cmd, sizeof(cmd));

    if (_stricmp(cmd, "proxy_status") == 0) {
        Proxy_UpdateCvar(&proxy_version);
        Proxy_UpdateCvar(&proxy_debug);
        Proxy_UpdateCvar(&proxy_loglevel);

        Proxy_Print("^3Proxy Library Status^7\n");
        Proxy_Print("  Version: %s\n", proxy_version.string);
        Proxy_Print("  Debug: %d\n", proxy_debug.integer);
        Proxy_Print("  Log Level: %d\n", proxy_loglevel.integer);
        Proxy_Print("  Original DLL: %s\n", g_orig ? "loaded" : "not loaded");
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_set") == 0) {
        if (argc < 3) {
            Proxy_Print("Usage: proxy_set <cvar> <value>\n");
            return qtrue;
        }
        char cvarName[128], cvarValue[256];
        g_engine_syscall(G_ARGV, 1, cvarName, sizeof(cvarName));
        g_engine_syscall(G_ARGV, 2, cvarValue, sizeof(cvarValue));
        Proxy_SetCvar(cvarName, cvarValue);
        Proxy_Print("Set %s = %s\n", cvarName, cvarValue);
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_cvar") == 0) {
        if (argc < 2) {
            Proxy_Print("Usage: proxy_cvar <name>\n");
            return qtrue;
        }
        char cvarName[128];
        g_engine_syscall(G_ARGV, 1, cvarName, sizeof(cvarName));
        int val = Proxy_GetCvarInteger(cvarName);
        Proxy_Print("%s = %d\n", cvarName, val);
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_clients") == 0) {
        Clients_PrintList();
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_kick") == 0) {
        if (argc < 3) {
            Proxy_Print("Usage: proxy_kick <clientNum> <reason>\n");
            return qtrue;
        }
        char numStr[16], reason[256];
        g_engine_syscall(G_ARGV, 1, numStr, sizeof(numStr));
        g_engine_syscall(G_ARGV, 2, reason, sizeof(reason));

        int clientNum = atoi(numStr);
        if (clientNum < 0 || clientNum >= MAX_PROXY_CLIENTS) {
            Proxy_Print("Invalid client number: %d\n", clientNum);
            return qtrue;
        }

        Clients_Kick(clientNum, reason);
        Proxy_Print("Kicked client %d: %s\n", clientNum, reason);
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_ban") == 0) {
        if (argc < 3) {
            Proxy_Print("Usage: proxy_ban <clientNum> <reason>\n");
            return qtrue;
        }
        char numStr[16], reason[256];
        g_engine_syscall(G_ARGV, 1, numStr, sizeof(numStr));
        g_engine_syscall(G_ARGV, 2, reason, sizeof(reason));

        int clientNum = atoi(numStr);
        if (clientNum < 0 || clientNum >= MAX_PROXY_CLIENTS) {
            Proxy_Print("Invalid client number: %d\n", clientNum);
            return qtrue;
        }

        proxy_client_t* cl = Clients_Get(clientNum);
        if (cl && cl->connected && cl->ip[0]) {
            Clients_BanIP(cl->ip, reason);
            Clients_Kick(clientNum, reason);
        } else {
            Proxy_Print("Client %d not connected\n", clientNum);
        }
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_stats") == 0) {
        if (argc < 2) {
            Proxy_Print("^3Usage: proxy_stats <clientNum or GUID>^7\n");
            return qtrue;
        }
        char arg[128];
        g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));

        player_stats_t* stats = Stats_GetByGUID(arg);
        if (!stats) {
            int clientNum = atoi(arg);
            if (clientNum >= 0 && clientNum < MAX_PROXY_CLIENTS) {
                proxy_client_t* cl = Clients_Get(clientNum);
                if (cl && cl->connected && cl->guid[0]) {
                    stats = Stats_GetByGUID(cl->guid);
                }
            }
        }
        Stats_PrintPlayerStats(stats);
        return qtrue;
    }

    if (_stricmp(cmd, "proxy_top") == 0) {
        if (argc < 2) {
            Proxy_Print("^3Usage: proxy_top <kills|accuracy|kd|headshots> [count]^7\n");
            return qtrue;
        }
        char category[32];
        g_engine_syscall(G_ARGV, 1, category, sizeof(category));

        int count = 10;
        if (argc >= 3) {
            char countStr[16];
            g_engine_syscall(G_ARGV, 2, countStr, sizeof(countStr));
            count = atoi(countStr);
            if (count < 1 || count > 50) count = 10;
        }

        if (_stricmp(category, "kills") == 0) {
            Stats_PrintTopKills(count);
        } else if (_stricmp(category, "accuracy") == 0) {
            Stats_PrintTopAccuracy(count);
        } else if (_stricmp(category, "kd") == 0) {
            Stats_PrintTopKD(count);
        } else if (_stricmp(category, "headshots") == 0) {
            Stats_PrintTopHeadshots(count);
        } else {
            Proxy_Print("^3Unknown category: %s^7\n", category);
            Proxy_Print("  Valid: kills, accuracy, kd, headshots\n");
        }
        return qtrue;
    }

    if (_stricmp(cmd, "resetstats") == 0 || _stricmp(cmd, "proxy_resetstats") == 0) {
        if (argc >= 2) {
            char arg[128];
            g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));
            if (_stricmp(arg, "all") == 0) {
                Stats_ResetAll();
            } else {
                Stats_ResetPlayer(arg);
            }
        } else {
            Proxy_Print("^3Usage: resetstats <all|GUID>^7\n");
        }
        return qtrue;
    }

    if (_stricmp(cmd, "savestats") == 0 || _stricmp(cmd, "proxy_savestats") == 0) {
        Stats_Save();
        Awards_Save();
        Proxy_Print("^2Stats and awards saved to disk^7\n");
        return qtrue;
    }

    // Autobalance commands
    if (Balance_HandleCommand(cmd, -1)) {
        return qtrue;
    }

    // Awards commands
    if (_stricmp(cmd, "proxy_awards") == 0) {
        if (argc >= 2) {
            char arg[128];
            g_engine_syscall(G_ARGV, 1, arg, sizeof(arg));
            int cn = atoi(arg);
            if (cn >= 0 && cn < MAX_PROXY_CLIENTS) {
                proxy_client_t* cl = Clients_Get(cn);
                if (cl && cl->connected && cl->guid[0]) {
                    Awards_PrintPlayerAwards(cl->guid, -1);
                } else {
                    Proxy_Print("^3Player %d not found^7\n", cn);
                }
            } else {
                Awards_PrintPlayerAwards(arg, -1);
            }
        } else {
            Proxy_Print("^3Usage: proxy_awards <clientNum or GUID>^7\n");
        }
        return qtrue;
    }

    return qfalse;
}

// ====================================================================
// vmMain / dllEntry
// ====================================================================

__declspec(dllexport) int __cdecl vmMain(int cmd, int a0, int a1, int a2, const char* a3, const char* a4, const char* a5, const char* a6, const char* a7, const char* a8, const char* a9, const char* a10, const char* a11) {
    if (!g_vmMain) InitOriginal();
    if (!g_vmMain) return -1;

    switch (cmd) {
        case GAME_INIT:
            Proxy_InitCvars();
            Clients_Init();
            Stats_Init();
            Awards_Init();
            Profiles_Init();
            break;
        case GAME_SHUTDOWN:
            Stats_Save();
            Awards_Save();
            Profiles_Save();
            break;
        case GAME_CLIENT_CONNECT: {
            const char* reject = Clients_OnConnect(a0, a1, a2);
            if (reject) {
                return (int)reject;
            }
            break;
        }
        case GAME_CLIENT_BEGIN:
            Clients_OnBegin(a0);
            break;
        case GAME_CLIENT_DISCONNECT:
            Clients_OnDisconnect(a0);
            if (proxy_autobalance.integer) {
                Balance_AutoBalance();
            }
            break;
        case GAME_CLIENT_USERINFO_CHANGED:
            Clients_OnUserInfoChanged(a0);
            break;
        case GAME_CLIENT_COMMAND:
            if (a0 >= 0) {
                if (HandleClientCommand(a0)) {
                    return 1;
                }
            }
            break;
        case GAME_RUN_FRAME:
            if (proxy_autobalance.integer) {
                Balance_AutoBalance();
            }
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
