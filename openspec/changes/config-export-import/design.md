## Context

Seelie stores configuration across multiple locations:
- **QSettings INI** (`~/.config/Seelie/Seelie.ini`) — language, auto-start, port, display mode, TTS enable, provider fields, LLM profiles, user profile (name, displayName)
- **SQLite database** (`~/.config/Seelie/memory.db`) — persona memory, milestones, conversation history

Users currently cannot back up, migrate, or share their complete setup. A reinstall means losing all configuration, memory, and cached data.

## Goals / Non-Goals

**Goals:**
- Export complete Seelie configuration as a single portable ZIP archive
- Import configuration from a ZIP archive with validation and rollback
- Human-readable filename: `Seelie-YYYY-MM-DD-HH-MM-SS.zip`
- Support cross-platform portability (macOS, Windows, Linux)
- Atomic import: validate everything before making any changes

**Non-Goals:**
- Cloud sync or automatic backup (future feature)
- Selective export/import of individual components (v2 enhancement)
- Encryption of exported archives (v2 enhancement)
- Import from pre-ZIP formats or manual file copying

## Decisions

### ZIP over tar.gz or plain directory
**Decision:** Use ZIP format
**Rationale:** ZIP is natively supported on all platforms, doesn't require external tools, and Qt 6.5+ has `QZipWriter`/`QZipReader` built-in. No additional dependencies needed.

### Manifest-driven structure
**Decision:** Include `manifest.json` at root of ZIP with metadata
**Rationale:** Enables version validation, schema checking, and future extensibility. Manifest records app version, export timestamp, and list of contained files.

### Copy files vs. serialize to JSON
**Decision:** Copy raw config files into ZIP (INI, SQLite, JSON) rather than converting to unified JSON
**Rationale:** Preserves exact state, simpler implementation, less risk of data loss during round-trip. Each file type is stored in its native format under a logical directory structure.

### Atomic import with temp directory
**Decision:** Extract to temp directory first, validate, then atomic rename
**Rationale:** Prevents partial import corruption. If validation fails, user's existing config is untouched. Uses `QTemporaryDir` + `QFile::rename()` for atomicity.

### Backup before import
**Decision:** Always create `.backup-YYYY-MM-DD-HH-MM-SS` of existing config before overwriting
**Rationale:** Safety net for users. If import goes wrong, they can manually restore. Backups are kept locally, not in the ZIP.

## Risks / Trade-offs

**[Risk]** Import overwrites user's current config without undo → **Mitigation:** Automatic backup created before import; user warned with confirmation dialog

**[Risk]** Future app version can't import old archive format → **Mitigation:** Manifest includes `schemaVersion` field; import validates and rejects incompatible versions with clear error message

**[Risk]** Memory DB schema changes between versions → **Mitigation:** Store app version in manifest; if major version differs, warn user that memory DB may need migration

**[Risk]** Memory database grows large over time → **Mitigation:** Archive contains raw SQLite file; size depends on conversation history length. No additional mitigation needed since it's user's own data.
