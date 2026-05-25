## ADDED Requirements

<!-- No new user-facing capabilities. This is an infrastructure change to the persistence layer. -->

### Requirement: Profile data persists to config INI
The system SHALL persist `userName` and `displayName` to the QSettings-based config INI file.

#### Scenario: Save button writes to ConfigManager
- **WHEN** user clicks Save in the Profile tab
- **THEN** `ConfigManager` SHALL persist `userName` and `displayName` to QSettings

#### Scenario: ConfigManager loads profile data on startup
- **WHEN** the application starts
- **THEN** `ConfigManager` SHALL load `userName` and `displayName` from QSettings into memory

#### Scenario: MemoryManager falls back to ConfigManager
- **WHEN** `MemoryManager::userName()` or `MemoryManager::displayName()` is called
- **THEN** it SHALL return the value from `ConfigManager` if available
- **AND** fall back to the SQLite value if `ConfigManager` has no value

## MODIFIED Requirements
<!-- No existing capability requirements are changing. -->

## REMOVED Requirements
<!-- No requirements removed. -->
