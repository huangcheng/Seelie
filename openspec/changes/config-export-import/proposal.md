## Why

Seelie's configuration is becoming increasingly complex with multiple AI providers (OpenAI, Anthropic, StepFun, MiniMax, Azure), per-provider settings (API keys, base URLs, models, voices), persona profiles, memory database, and user preferences. Users currently have no way to back up, migrate, or share their complete configuration. A single reinstall or machine switch means manually re-entering all settings. We need a portable export/import system so users can snapshot their entire config state.

## What Changes

- **Add ConfigExporter class** — serializes all configuration (QSettings, SQLite memory DB, provider configs, profiles) into a structured JSON manifest
- **Add ConfigImporter class** — reads a ZIP archive, validates manifest version/schema, and restores all configuration
- **Add Export/Import UI** — two new buttons in Settings panel (General tab) for export and import operations
- **ZIP archive format** — compressed archive containing `manifest.json` + individual config files, named `Seelie-YYYY-MM-DD-HH-MM-SS.zip`
- **Version validation** — manifest includes app version and schema version; import validates compatibility
- **Selective import** — optional: import only settings, only profiles, only memory (future enhancement)
- **Atomic import** — validates entire archive before making any changes; rollback on failure

## Capabilities

### New Capabilities
- `config-export-import`: Export and import complete Seelie configuration as portable ZIP archives

### Modified Capabilities
<!-- No existing spec-level requirement changes — this is a new feature that doesn't alter existing behavior -->

## Impact

- **UI**: SettingsPanelWidget (General tab) — add Export and Import buttons
- **Core**: New ConfigExporter/ConfigImporter classes in `src/config/` or `src/`
- **Dependencies**: Add QuaZIP or Qt's built-in QZipWriter (Qt 6.5+) for ZIP handling
- **Data**: Reads from `~/.config/Seelie/` (QSettings INI, SQLite DB, provider configs)
- **Risk**: Import modifies user data — must validate and backup before overwriting
