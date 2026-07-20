## ADDED Requirements

### Requirement: Idle state triggers both animation and saying
The system SHALL allow the idle timer handler to trigger both an idle animation and a random saying in the same cycle.

#### Scenario: Idle cycle with saying
- **GIVEN** the idle timer fires
- **WHEN** the random saying probability succeeds
- **THEN** the pet plays an idle animation AND displays a saying bubble simultaneously

#### Scenario: Idle cycle without saying
- **GIVEN** the idle timer fires
- **WHEN** the random saying probability fails
- **THEN** only the idle animation plays and no saying bubble appears
