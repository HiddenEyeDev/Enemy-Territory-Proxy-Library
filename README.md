# 🐺 ET: Silent Mod Proxy Library

A feature-rich proxy DLL for **Wolfenstein: Enemy Territory (2.60b)** running **Silent Mod 0.9.0**. This library intercepts the game module to add advanced server features, player stats, achievements, and community tools without modifying the original mod files.

---

## ✨ Features

### 📊 Player Stats System
- **Lifetime Tracking**: Kills, Deaths, Headshots, Accuracy, Playtime.
- **Skill Rating**: 0-1000 scale based on K/D (40%), Accuracy (35%), Kills (15%), and Streaks (10%).
- **Leaderboards**: View top players by Kills, K/D, Accuracy, and Headshots.

### 🏆 Awards & Achievements
- **30+ Achievements**: From `FirstBlood` to `Godlike` streaks.
- **Repeatable Awards**: Track multi-kill milestones like `[5x] MultiSlayer`.
- **Broadcasts**: Announcements to all players when awards are earned.

### 👤 Player Profiles
- **Level & XP**: Progression system with unlocks (Chat Colors, Vote Weight).
- **Reputation**: Community-driven + / - rep system.
- **Customization**: Set custom signatures, titles, and clan tags.
- **Player Cards**: `!card` shows a summary of your identity, level, and recent performance.

### ⚖️ Smart Autobalance
- **Skill-Based**: Automatically balances teams based on calculated skill ratings.
- **Configurable Thresholds**: Set minimum players and max skill difference.
- **Auto-Move**: Gently moves the weakest player from the stronger team.

### 🛡️ Admin Tools
- **IP Bans**: Ban players by IP with persistent storage.
- **Kicks & Management**: Full control over connected clients.
- **Console Commands**: Extensive server-side management tools.

---

## 🚀 Installation

1. **Locate your Silent Mod folder**:
   Usually found at `etmain/silent/` or similar.
2. **Rename the original DLL**:
   Rename `qagame_mp_x86.dll` to `qagame_mp_x86_orig.dll`.
3. **Install the Proxy**:
   Place the compiled `qagame_mp_x86.dll` (from this project) into the same folder.
4. **Start the Server**:
   The proxy will automatically load the original DLL and inject its features.

> **Note**: The proxy requires the original DLL to be present as `qagame_mp_x86_orig.dll` in the same directory.

---

## 🎮 In-Game Commands

| Command | Description |
| :--- | :--- |
| `stats <name>` | View detailed stats for a player. |
| `top <type> [count]` | View leaderboards (`kills`, `accuracy`, `kd`, `headshots`). |
| `rank` | View your own server rank and stats summary. |
| `profile` / `card` | View your player card (Level, XP, Rep, Signature). |
| `sig <text>` | Set your custom signature (visible on profile). |
| `title <text>` | Set a custom display title. |
| `clan <tag>` | Set your clan tag (e.g., `[PRO]`). |
| `rep <+|-> <player>` | Give positive or negative reputation. |
| `lfg` | Toggle "Looking For Group" status. |
| `awards [player]` | View your awards or check another player's. |
| `balance` | View current team skill balance. |
| `skill [player]` | View detailed skill rating breakdown. |

---

## 💻 Server Console Commands

| Command | Description |
| :--- | :--- |
| `proxy_status` | Show proxy version and DLL load state. |
| `proxy_clients` | List all connected players with IP and GUID. |
| `proxy_kick <num> <reason>` | Kick a player by ClientNum. |
| `proxy_ban <num> <reason>` | Ban a player's IP and kick them. |
| `proxy_stats <num/GUID>` | Print detailed lifetime stats. |
| `proxy_top <type> [count]` | Print server-wide leaderboards. |
| `proxy_awards <num/GUID>` | Print all awards earned by a player. |
| `proxy_balance` | Print detailed team balance status. |
| `proxy_forcebalance` | Force the autobalance system to run. |
| `resetstats <all/GUID>` | Wipe stats for everyone or a specific player. |
| `savestats` | Force save all data to disk. |

---

## ⚙️ Configuration (Cvars)

| Cvar | Default | Description |
| :--- | :--- | :--- |
| `proxy_autobalance` | `1` | Enable/disable smart team balancing. |
| `proxy_balance_threshold` | `150` | Max skill difference before balancing triggers. |
| `proxy_balance_minplayers` | `4` | Minimum players required to trigger autobalance. |
| `proxy_debug` | `0` | Enable debug logging. |
| `proxy_loglevel` | `1` | Verbosity of server logs. |

---

## 🏗️ Building

This project is built for **Visual Studio 2026** using the **v145** toolset.

1. Open `proxy_lib.sln` in Visual Studio.
2. Select **Release | Win32**.
3. Build the solution (`Ctrl+Shift+B`).
4. The output DLL will be in `bin/Release/qagame_mp_x86.dll`.

### Project Structure
```text
proxy_lib/
├── proxy_lib.sln
├── proxy_lib.vcxproj
├── src/
│   ├── proxy_main.cpp      # Main proxy logic and features
│   ├── proxy_hooks.h       # Headers and definitions
│   └── proxy.def           # Export definitions
└── base/                   # Minimal "starter" version for custom builds
    ├── proxy_base.sln
    └── src/
        ├── proxy_main.cpp
        └── proxy_hooks.h
```

---

## ⚠️ Disclaimer

This project is a fan-made modification and is not affiliated with id Software, Splash Damage, or the Silent Mod development team. Use at your own risk. Always backup your original game files before installing.

## 📜 License

This project is open-source. Feel free to modify, extend, and use it on your own servers.
