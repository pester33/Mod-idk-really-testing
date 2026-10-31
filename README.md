# Session Stats (Geode mod)

Live in-level overlay for Geometry Dash 2.2081 (Geode 5.11.2): attempts, deaths, session best, last death %, play time and all-time deaths.

## Build it (easy way, no setup)
1. Make a new GitHub repo and upload everything in this folder (including the `.github` folder).
2. Go to the **Actions** tab. The "Build Geode Mod" workflow runs on its own (or click **Run workflow**).
3. When it's done, open the run and download **Build Output**. Inside is `pester.session-stats.geode`.
4. Put that file in your Geode mods folder (`Geometry Dash/geode/mods`) and restart GD.

## Build it locally
Install the Geode CLI + SDK (https://docs.geode-sdk.org), then:
```
geode build
```

## Files
- `mod.json`: mod info and settings
- `src/main.cpp`: all the mod code (hooks `PlayLayer`)
- `about.md` / `changelog.md`: shown on the mod's page in Geode
