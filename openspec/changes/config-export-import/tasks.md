## 1. Core Classes

- [ ] 1.1 Create `ConfigExporter` class with `exportToZip(const QString &destinationPath)` method
- [ ] 1.2 Create `ConfigImporter` class with `importFromZip(const QString &zipPath)` method
- [ ] 1.3 Create `ExportManifest` struct with version, timestamp, platform fields
- [ ] 1.4 Add manifest serialization (`toJson()` / `fromJson()`) and validation

## 2. Export Implementation

- [ ] 2.1 Implement `ConfigExporter::collectFiles()` — gather config file paths (Seelie.ini, memory.db)
- [ ] 2.2 Implement ZIP creation using `QZipWriter` — write manifest + files with directory structure
- [ ] 2.3 Generate filename `Seelie-YYYY-MM-DD-HH-MM-SS.zip` using current datetime

## 3. Import Implementation

- [ ] 3.1 Implement `ConfigImporter::validateZip()` — check manifest exists, schema version compatible
- [ ] 3.2 Implement `ConfigImporter::extractToTemp()` — extract to `QTemporaryDir`
- [ ] 3.3 Implement `ConfigImporter::backupExisting()` — create `config-backup-YYYY-MM-DD-HH-MM-SS.zip`
- [ ] 3.4 Implement atomic replacement — validate all files, then rename temp to final
- [ ] 3.5 Handle version mismatch warning (major version diff) before import
- [ ] 3.6 Handle invalid archive error with clear user message

## 4. UI Integration

- [ ] 4.1 Add "Export Config" button to General tab of SettingsPanelWidget
- [ ] 4.2 Add "Import Config" button to General tab of SettingsPanelWidget
- [ ] 4.3 Open file save dialog for export (default filename with datetime)
- [ ] 4.4 Open file picker dialog for import (filter for `.zip`)
- [ ] 4.5 Show confirmation dialog before import (warn about overwrite)
- [ ] 4.6 Show success / error messages after export/import operations
- [ ] 4.7 Style buttons to match existing SettingsPanelWidget design language

## 5. Integration & Safety

- [ ] 5.1 Ensure app restart not required after import (or show restart prompt)
- [ ] 5.2 Handle case where Seelie is running during import (config may be locked)
- [ ] 5.3 Add error handling for disk full, permission denied, corrupted ZIP
- [ ] 5.4 Ensure cross-platform paths work (macOS, Windows, Linux)

## 6. Testing

- [ ] 6.1 Export config and verify ZIP structure matches spec
- [ ] 6.2 Import config and verify all settings restored correctly
- [ ] 6.3 Test version mismatch warning with older/newer version archives
- [ ] 6.4 Test invalid archive rejection (non-Seelie ZIP, missing manifest)
- [ ] 6.5 Test atomic rollback — corrupt ZIP leaves config untouched
- [ ] 6.6 Test backup creation — verify backup ZIP contains old config
- [ ] 6.7 Test cross-platform — export on macOS, import on Windows (if possible)

## 7. Build & Polish

- [ ] 7.1 Update `CMakeLists.txt` with new source files
- [ ] 7.2 Update `.gitignore` if needed for backup files
- [ ] 7.3 Build compiles successfully on all platforms
- [ ] 7.4 Run existing tests to ensure no regressions
