# AITriggerTypeExt — Complete INI Reference

All keys are declared in a sidecar section named `[TriggerID.AIExt]`
alongside the normal `[AITriggerTypes]` entry for that trigger.

```ini
[AITriggerTypes]
TT01=MyWave,<TeamID>,<all>,0,2,MTNK,...

[TT01.AIExt]
; ... keys documented below ...
```

Omitting the `[TT01.AIExt]` section entirely means no extra checks — vanilla
behaviour is preserved exactly.

---

## Parallel list convention

Building and unit checks use three index-matched lists:

```ini
RequiredOwnerBuildings=GABARR,GAPILE,GAWEAP
RequiredOwnerBuildingsMin=1,0,2
RequiredOwnerBuildingsMax=-1,3,-1
```

Index 0: GABARR must have count in [1, ∞]
Index 1: GAPILE must have count in [0, 3]
Index 2: GAWEAP must have count in [2, ∞]

Rules:
- Missing trailing Min values default to `0`.
- Missing trailing Max values default to `-1` (uncapped / no upper bound).
- Negative Min values are clamped to `0`.
- `-1` as a Max value means no upper bound.
- All entries in a list are AND-checked — every entry must pass.

---

## Global settings

### `TargetHouseMode`

Controls which enemy house the enemy checks (`RequiredEnemy*`) are applied to.

| Value             | Behaviour |
|-------------------|-----------|
| `Current`         | Use the engine-selected TargetHouse for this evaluation tick (default, vanilla) |
| `Any`             | Any enemy house satisfying all enemy checks is sufficient |
| `All`             | Every enemy house must satisfy all enemy checks |
| `Most_Buildings`  | Select the enemy with the most of the listed `RequiredEnemyBuildings` types |
| `Least_Buildings` | Select the enemy with the fewest of the listed `RequiredEnemyBuildings` types |
| `Most_Units`      | Select the enemy with the most of the listed `RequiredEnemyUnits` types |
| `Least_Units`     | Select the enemy with the fewest of the listed `RequiredEnemyUnits` types |

```ini
TargetHouseMode=Current
```

### `RequiredAlliesMode`

Controls how the ally checks are applied across all allied houses.

| Value | Behaviour |
|-------|-----------|
| `Any` | Any single allied house satisfying all ally checks is sufficient (default) |
| `All` | Every allied house must satisfy all ally checks |

```ini
RequiredAlliesMode=Any
```

---

## Owner checks (AI house itself)

### Buildings

```ini
RequiredOwnerBuildings=GABARR,GAPILE
RequiredOwnerBuildingsMin=1,1
RequiredOwnerBuildingsMax=-1,-1
```

The AI (CallingHouse) must own a count of each building within [Min, Max].
Uses `CountOwnedAndPresent` — buildings currently on the map, not under
construction.

**Rush trigger example** — only fire before the AI has built any defenses:
```ini
RequiredOwnerBuildings=GAWALL,GAPILE,GAACCA
RequiredOwnerBuildingsMin=0,0,0
RequiredOwnerBuildingsMax=0,0,0
```

### Units

```ini
RequiredOwnerUnits=MTNK,E1,HELI
RequiredOwnerUnitsMin=3,10,1
RequiredOwnerUnitsMax=-1,-1,4
```

Accepts any TechnoType ID (infantry, vehicle, aircraft, building).

### Superweapons

```ini
RequiredOwnerSuperWeapons=NUKE,IRON
RequiredOwnerSuperWeaponsReadyMin=0,0
RequiredOwnerSuperWeaponsReadyMax=0,-1
```

`ReadyMin` and `ReadyMax` define a window of **frames remaining** until the
SW fires.  `0` frames remaining = fully charged and ready to fire.

| Scenario | ReadyMin | ReadyMax |
|----------|----------|----------|
| Must be fully ready | `0` | `0` |
| Ready or nearly ready (within 5 sec) | `0` | `75` |
| Charging but not ready yet | `1` | `-1` |
| Don't care | omit | omit |

### Scalar fields

```ini
RequiredOwnerCreditsMin=1000    ; Balance >= 1000
RequiredOwnerCreditsMax=-1      ; no upper cap (-1 = uncapped)

RequiredOwnerPowerMin=0         ; net power (Output - Drain) >= 0
RequiredOwnerPowerMax=-1        ; no upper cap

RequiredOwnerPowerOutputMin=500 ; raw power output >= 500
RequiredOwnerPowerOutputMax=-1  ; no upper cap

RequiredOwnerTechLevelMin=3     ; TechLevel >= 3
RequiredOwnerTechLevelMax=-1    ; no upper cap
```

**Power field sign convention:**
- Positive = surplus (e.g. `100` means at least 100 units of surplus)
- `0` = must not be in deficit
- Negative = tolerated deficit (e.g. `-200` means deficit no worse than -200)

**Power window example** — only fire when severely power-starved (to trigger
emergency power-building behavior):
```ini
RequiredOwnerPowerMin=-1000   ; not completely dead
RequiredOwnerPowerMax=-300    ; but at deficit of at least 300
```

---

## Enemy checks

Same field structure as Owner, prefixed with `Enemy`.

```ini
RequiredEnemyBuildings=NAWEAP,NAPSIS
RequiredEnemyBuildingsMin=1,0
RequiredEnemyBuildingsMax=-1,0

RequiredEnemyUnits=DESO,YURI
RequiredEnemyUnitsMin=1,1
RequiredEnemyUnitsMax=5,-1

RequiredEnemySuperWeapons=NUKE
RequiredEnemySuperWeaponsReadyMin=0
RequiredEnemySuperWeaponsReadyMax=0

RequiredEnemyCreditsMin=0
RequiredEnemyCreditsMax=2000

RequiredEnemyPowerMin=-1
RequiredEnemyPowerMax=-300

RequiredEnemyPowerOutputMin=-1
RequiredEnemyPowerOutputMax=800

RequiredEnemyTechLevelMin=-1
RequiredEnemyTechLevelMax=3
```

**Emergency defense example** — enemy nuke is ready, trigger a defensive
wave with high weight:
```ini
RequiredEnemySuperWeapons=NUKE
RequiredEnemySuperWeaponsReadyMin=0
RequiredEnemySuperWeaponsReadyMax=0
TargetHouseMode=Any
```

**Harassment trigger** — only harass when enemy is poor and power-deficient:
```ini
RequiredEnemyCreditsMax=500
RequiredEnemyPowerMax=-100
TargetHouseMode=Any
```

**Rush trigger** — attack before enemy builds up defenses:
```ini
RequiredEnemyBuildings=NAWALL,NASAM,NATESLA
RequiredEnemyBuildingsMin=0,0,0
RequiredEnemyBuildingsMax=0,0,0
TargetHouseMode=Current
```

---

## Allies checks

Checks are evaluated across allied houses.  `RequiredAlliesMode` controls
whether Any or All allies must satisfy the checks.

```ini
RequiredAlliesBuildings=GAAIRC
RequiredAlliesBuildingsMin=1
RequiredAlliesBuildingsMax=-1

RequiredAlliesUnits=MTNK
RequiredAlliesUnitsMin=2
RequiredAlliesUnitsMax=-1

RequiredAlliesSuperWeapons=IRON
RequiredAlliesSuperWeaponsReadyMin=0
RequiredAlliesSuperWeaponsReadyMax=300

RequiredAlliesCreditsMin=500
RequiredAlliesCreditsMax=-1

RequiredAlliesPowerMin=0
RequiredAlliesPowerMax=-1

RequiredAlliesPowerOutputMin=-1
RequiredAlliesPowerOutputMax=-1

RequiredAlliesTechLevelMin=-1
RequiredAlliesTechLevelMax=-1

RequiredAlliesMode=Any
```

**Coordinated attack example** — only send the big wave when an ally has
built an airfield (so they can provide air support):
```ini
RequiredAlliesBuildings=GAAIRC
RequiredAlliesBuildingsMin=1
RequiredAlliesMode=Any
```

---

## Neutral checks

The neutral/civilian house.  No mode selector — there is only one neutral
house per scenario.

```ini
RequiredNeutralBuildings=GATECH,HOSPITAL
RequiredNeutralBuildingsMin=1,1
RequiredNeutralBuildingsMax=-1,-1

RequiredNeutralUnits=TRUCKA
RequiredNeutralUnitsMin=1
RequiredNeutralUnitsMax=-1

RequiredNeutralSuperWeapons=
RequiredNeutralSuperWeaponsReadyMin=
RequiredNeutralSuperWeaponsReadyMax=

RequiredNeutralCreditsMin=-1
RequiredNeutralCreditsMax=-1

RequiredNeutralPowerMin=-1
RequiredNeutralPowerMax=-1

RequiredNeutralPowerOutputMin=-1
RequiredNeutralPowerOutputMax=-1

RequiredNeutralTechLevelMin=-1
RequiredNeutralTechLevelMax=-1
```

**Tech building capture priority** — trigger a capture mission only while
a neutral tech center still exists:
```ini
RequiredNeutralBuildings=GATECH
RequiredNeutralBuildingsMin=1
RequiredNeutralBuildingsMax=-1
```

---

## Elapsed time

Frame-based time gate since scenario start.
1 second ≈ 15 frames at normal game speed.

```ini
RequiredElapsedTimeMin=0       ; no earliest limit
RequiredElapsedTimeMax=-1      ; no latest limit (-1 = uncapped)
```

**Early game rush** — only viable during the opening minutes:
```ini
RequiredElapsedTimeMin=300     ; after 20 seconds (warmup)
RequiredElapsedTimeMax=4500    ; before 5 minutes
```

**Late game escalation** — only begin heavy assaults after 10 minutes:
```ini
RequiredElapsedTimeMin=9000
RequiredElapsedTimeMax=-1
```

---

## Complete annotated example

```ini
[AITriggerTypes]
TT_NUKE_RESPONSE=Nuclear Response,TeamNukeDefense,<all>,1,2,NAPSIS,...

[TT_NUKE_RESPONSE.AIExt]

; Only trigger this once we have a decent base
RequiredOwnerBuildings=GABARR,GAWEAP,GAAIRC
RequiredOwnerBuildingsMin=1,1,1
RequiredOwnerBuildingsMax=-1,-1,-1

; We need to be in reasonable financial shape
RequiredOwnerCreditsMin=1500

; We must not be power-deficient ourselves
RequiredOwnerPowerMin=0

; Only trigger when ANY enemy has a nuclear missile ready to fire
RequiredEnemySuperWeapons=NUKE
RequiredEnemySuperWeaponsReadyMin=0
RequiredEnemySuperWeaponsReadyMax=0
TargetHouseMode=Any

; Don't bother with this until the mid-game
RequiredElapsedTimeMin=4500
```

This trigger will only enter the eligible pool when:
1. The AI owns at least one barracks, war factory, and airfield
2. The AI has at least 1500 credits
3. The AI is not in a power deficit
4. Any enemy player has a nuclear missile fully charged
5. At least 5 minutes have passed since the match started

---

## Frame timing reference

| Time       | Frames (approx) |
|------------|-----------------|
| 10 seconds | 150             |
| 30 seconds | 450             |
| 1 minute   | 900             |
| 2 minutes  | 1800            |
| 5 minutes  | 4500            |
| 10 minutes | 9000            |
| 15 minutes | 13500           |
| 20 minutes | 18000           |

All values assume normal game speed (15 logical frames per second).
