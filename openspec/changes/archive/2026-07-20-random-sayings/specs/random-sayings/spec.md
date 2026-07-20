## ADDED Requirements

### Requirement: Random sayings trigger during idle
The system SHALL display a random saying bubble when the idle timer fires, subject to the configured frequency probability and cooldown.

#### Scenario: Idle timer fires and probability roll succeeds
- **GIVEN** the idle timer fires and the saying cooldown has elapsed
- **WHEN** the frequency probability roll succeeds
- **THEN** a random saying is displayed in the tip bubble alongside the idle animation

#### Scenario: Idle timer fires but cooldown active
- **GIVEN** the idle timer fires but the saying cooldown has not elapsed
- **WHEN** the system evaluates whether to show a saying
- **THEN** no saying is displayed and the idle animation plays normally

#### Scenario: User sets frequency to Never
- **GIVEN** the saying frequency is set to "never"
- **WHEN** the idle timer fires
- **THEN** no random saying is displayed regardless of cooldown or probability

### Requirement: Sayings respect a minimum cooldown
The system SHALL enforce a 60-second cooldown between random sayings.

#### Scenario: Two idle cycles within 60 seconds
- **GIVEN** a saying was displayed at T=0
- **WHEN** the idle timer fires at T=30
- **THEN** no saying is displayed because the cooldown has not elapsed
