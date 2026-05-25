## ADDED Requirements

### Requirement: User can export complete configuration
The system SHALL allow users to export all configuration data as a ZIP archive with a human-readable filename.

#### Scenario: Successful export from Settings panel
- **WHEN** user clicks the "Export Config" button in the General tab of Settings
- **THEN** a ZIP archive SHALL be created at the user's chosen location
- **AND** the filename SHALL follow the pattern `Seelie-YYYY-MM-DD-HH-MM-SS.zip`
- **AND** the archive SHALL contain all configuration files

#### Scenario: Exported ZIP structure
- **WHEN** user opens the exported ZIP
- **THEN** it SHALL contain:
  - `manifest.json` — metadata (appVersion, schemaVersion, exportTimestamp, platform)
  - `config/` — QSettings INI file (Seelie.ini)
  - `memory/` — SQLite database file (memory.db)

### Requirement: User can import configuration from ZIP
The system SHALL allow users to restore configuration from a previously exported ZIP archive.

#### Scenario: Successful import from ZIP
- **WHEN** user clicks the "Import Config" button in the General tab of Settings
- **AND** user selects a valid Seelie ZIP archive
- **THEN** the system SHALL validate the archive
- **AND** backup the current configuration
- **AND** replace all configuration files with those from the archive
- **AND** show a success message

#### Scenario: Import with version mismatch warning
- **WHEN** user imports a ZIP created by a different app version
- **AND** the major version differs
- **THEN** the system SHALL show a warning dialog
- **AND** ask user to confirm before proceeding

#### Scenario: Import invalid archive rejected
- **WHEN** user selects a ZIP that is not a valid Seelie config export
- **THEN** the system SHALL reject the import
- **AND** show an error message: "Invalid configuration archive"

### Requirement: Import is atomic with rollback
The system SHALL ensure import operations are atomic — either fully succeed or leave existing config untouched.

#### Scenario: Failed import leaves config intact
- **WHEN** import validation fails (e.g., corrupted file, missing manifest)
- **THEN** the system SHALL abort the import
- **AND** the existing configuration SHALL remain unchanged

#### Scenario: Backup created before import
- **WHEN** user confirms an import operation
- **THEN** the system SHALL create a backup of the current config
- **AND** the backup SHALL be named `config-backup-YYYY-MM-DD-HH-MM-SS.zip`
- **AND** the backup SHALL be stored in the Seelie config directory
