## ADDED Requirements

### Requirement: Sayings are organized into categories
The system SHALL maintain saying pools for at least four categories: `humor`, `encouragement`, `observation`, and `coding_wisdom`.

#### Scenario: Random saying selection
- **WHEN** the system selects a random saying
- **THEN** it first picks a category uniformly at random, then picks a saying from that category weighted by `weight`

### Requirement: Each saying has associated metadata
The system SHALL store for each saying: a title, body text, optional animation trigger, category, and selection weight.

#### Scenario: Saying is displayed
- **WHEN** a saying is selected and displayed
- **THEN** the title and body appear in the tip bubble, and the optional animation plays on the pet if specified

### Requirement: Minimum saying pool size
The system SHALL include at least 20 sayings across all categories at launch.

#### Scenario: Engine initializes
- **WHEN** `RandomSayingsEngine` is constructed
- **THEN** the saying pool contains at least 20 entries distributed across categories
