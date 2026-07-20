## ADDED Requirements

### Requirement: Idle timeout is variable
The system SHALL use a random idle timeout between 1000ms and 4000ms for each idle cycle instead of a fixed interval.

#### Scenario: Animation ends and idle timer starts
- **WHEN** the current animation finishes and the idle timer is started
- **THEN** the timer interval is set to a random value in the range [1000, 4000] milliseconds

### Requirement: Idle animations do not repeat consecutively
The system SHALL avoid playing the same idle animation twice in a row. If the random selection matches the previously played idle animation, the system SHALL perform one re-roll.

#### Scenario: Random pick matches last idle animation
- **GIVEN** the last idle animation played was `IdleAtom`
- **WHEN** the random selector picks `IdleAtom` again
- **THEN** the system re-rolls once and plays a different idle animation

### Requirement: Surprise non-idle looks occur occasionally
The system SHALL have a 10% probability of playing a "surprise" animation (directional look or subtle gesture) instead of a standard idle animation.

#### Scenario: Surprise roll succeeds
- **GIVEN** the idle timer fires
- **WHEN** the 10% surprise probability roll succeeds
- **THEN** the system plays a surprise animation (e.g., `LookRight`, `LookUpLeft`) instead of an idle animation

#### Scenario: Surprise roll fails
- **GIVEN** the idle timer fires
- **WHEN** the 10% surprise probability roll fails
- **THEN** the system plays a standard idle animation from the idle pool
