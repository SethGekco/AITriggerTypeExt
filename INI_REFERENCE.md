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

RequiredOwnerDPSMin=0           ; live combat DPS of everything owned >= 0
RequiredOwnerDPSMax=-1          ; no upper cap (-1 = uncapped)
```

**DPS** = sum over every owned Infantry/Unit/Aircraft/Building of
`count * (Damage * Burst / (ROF / 10))` across weapons 0 and 1, counting only
weapons with positive damage (repair/support weapons excluded). Raw firepower
(no armor/warhead weighting yet). Computed live, cached per house per scope per
frame. Enemy variant exists too — e.g. "only commit a rush while the enemy's
firepower is low":
```ini
RequiredEnemyDPSMax=400         ; enemy's summed DPS must be <= 400
RequiredEnemyDPSLock=AG         ; ...counting only ANTI-GROUND weapons
```
**`Lock`** scope filter (`RequiredOwnerDPSLock` / `RequiredEnemyDPSLock`):
`AA` = count only weapons that can hit air, `AG` = only weapons that can hit
ground (by the projectile's `AA`/`AG` flags), `AA,AG` or omit = all. So
`RequiredEnemyDPSLock=AG` measures the enemy's anti-ground firepower — the
number that matters before a ground rush.

**`Types`** unit filter (`RequiredOwnerDPSTypes` / `RequiredEnemyDPSTypes`) —
a comma list of TechnoType IDs; the DPS sum counts only those unit types
(empty = all). Combines with `Lock` as an intersection:
```ini
RequiredEnemyDPSTypes=HTNK,APOC     ; only heavy/apoc tanks...
RequiredEnemyDPSLock=AG             ; ...their anti-ground firepower
```
A token may also be a **named group** from a global `[DPSGroupTypes]` section
(reusable across triggers; `RequiredEnemyMaxRangeTypes` accepts them too):
```ini
[DPSGroupTypes]
Tanks=HTNK,MTNK,APOC
; ...then anywhere:
RequiredEnemyDPSTypes=Tanks         ; expands to the group
```

**`Armor`** weighting (`RequiredOwnerDPSArmor` / `RequiredEnemyDPSArmor`) —
multiplies each weapon's DPS by its warhead's Verses vs the named armor, so the
value becomes *effective* damage dealt to that armor rather than raw firepower.
Armors: `none, flak, plate, light, medium, heavy, wood, steel, concrete,
special_1, special_2`. Omit = raw.
```ini
RequiredEnemyDPSArmor=heavy         ; effective enemy DPS vs HEAVY armor
```
All four filters compose: `Types` (which units) × `Lock` (which weapons) ×
`Armor` (vs which armor) → e.g. "enemy tanks' anti-ground effective DPS vs my
heavy armor" — the real "will my heavy tank push survive" number.

### Comparative — owner vs enemy DPS ratio

`RequiredDPSRatioMin` / `RequiredDPSRatioMax` gate on the owner's DPS as a
**percentage of the resolved enemy's** DPS (`200` = owner has 2.0× the enemy).
`RequiredDPSRatioLock` (AA/AG) applies to both sides. `-1` max = uncapped.
```ini
RequiredDPSRatioMin=200      ; only fire when I out-gun the enemy 2:1
RequiredDPSRatioLock=AG      ; ...comparing anti-ground firepower
```
A zero-DPS enemy makes `Min` pass trivially (you're dominant). The actual
ratio shows in the detail report as `RequiredDPSRatio(<pct>):min,max`.

### Threat — enemy weapon range

`RequiredEnemyMaxRangeMin` / `RequiredEnemyMaxRangeMax` gate on the **longest
weapon range** (in cells) among the enemy's owned damaging weapons, scoped by
`RequiredEnemyMaxRangeLock` (AA/AG) and `RequiredEnemyMaxRangeTypes`.
```ini
RequiredEnemyMaxRangeMax=6       ; only rush if the enemy's longest...
RequiredEnemyMaxRangeLock=AG     ; ...anti-ground weapon reaches <= 6 cells
```
"Don't commit a ground push when the enemy outranges me" (e.g. Prism Towers,
V3s). Shows in the detail report as `RequiredEnemyMaxRange(<cells>):min,max`.
This is the first threat-aware condition; the same per-weapon range data is the
groundwork for spatial range-avoidance.

### Threat — is my team outranged?

`RequiredTeamRangeRatioMin` / `RequiredTeamRangeRatioMax` compares **this
trigger's own team** (Team1/Team2 taskforce) longest weapon range to the
enemy's, as a percentage (`100` = matched, `<100` = my team is outranged).
`RequiredTeamRangeRatioLock` (AA/AG) scopes both sides.
```ini
RequiredTeamRangeRatioMin=100    ; don't dispatch if my team is outranged
```
Because it reads the trigger's actual taskforce, a Dog rush and a Prism push in
the same house get different answers. Shows as `RequiredTeamRangeRatio(<pct>)`.
A zero-range enemy makes `Min` pass trivially. This is the "decide" step; actual
mid-move steering around enemy ranges is a separate (pathfinding) effort.

### Proximity — how close are the two bases?

`RequiredBaseDistanceMin` / `RequiredBaseDistanceMax` gate on the straight-line
distance **in cells** between the owner's base center and the resolved enemy's
base center (`HouseClass::GetBaseCenter()` — the AI base center, or its spawn
cell before a base exists). `-1` max = uncapped.
```ini
RequiredBaseDistanceMin=60       ; only fire when the enemy base is 60+ cells away
RequiredBaseDistanceMax=-1
```
Lets a house pick its posture from map geometry: bases far apart → big set-piece
waves are safe to assemble and march; bases close together → skip this trigger in
favour of fast guerilla harassment.
```ini
[BigArmoredPush.AIExt]
RequiredBaseDistanceMin=50        ; long march is worth it — commit the deathball

[EarlyGuerillaRush.AIExt]
RequiredBaseDistanceMax=35        ; neighbours — harass instead of massing
```
Uses the same `TargetHouseMode`-resolved enemy as the other enemy checks. If no
single enemy resolves (`Any`/`All` modes) or a base center is unset, distance is
`0`, which trivially satisfies `Min`. Shows in the detail report as
`RequiredBaseDistance(<cells>):min,max`.

### Economy — credit momentum

`RequiredOwnerCreditsRate*` / `RequiredEnemyCreditsRate*` gate on the **net change
in a house's credit Balance over the last `RequiredCreditsRateWindow` frames**
(default 150 ≈ 10s). The value is *signed*: positive = the house is gaining
(harvesting / booming), negative = spending or bleeding.
```ini
RequiredEnemyCreditsRateMax=-800     ; enemy just sank 800+ into a big purchase
RequiredEnemyCreditsRateMin=-99999   ; (lower bound optional)
RequiredCreditsRateWindow=150        ; measured over ~10 seconds
```
Uses: "strike right after the enemy empties its bank on a superweapon/expansion"
(`EnemyMax` negative), or "harass a booming economy before it snowballs"
(`RequiredEnemyCreditsRateMin=1000`). Owner variants let a house wait until its
*own* income recovers. The rate is sampled live per house; it reads `0` for the
first window after a save/load (warmup) and is **not** serialized. Enemy uses the
`TargetHouseMode`-resolved house. Detail report: `RequiredOwnerCreditsRate(<n>)`,
`RequiredEnemyCreditsRate(<n>)`.

### Detection — a structure exists on the map

`RequiredStructureOnMap` gates on the **total count, across every house on the
map, of any listed BuildingType** — regardless of owner. `-1` max = uncapped.
```ini
RequiredStructureOnMap=NAMISL,GATECH   ; nuke silo OR tech center...
RequiredStructureOnMapMin=1            ; ...at least one exists anywhere
RequiredStructureOnMapMax=-1
```
Complements the per-house Owner/Enemy building gates: use this for
"something exists on the battlefield" logic ("react while any nuke silo stands",
"only run this while a neutral tech building is still capturable"). Detail
report: `RequiredStructureOnMap(<count>):min,max`.

### Pacing — per-trigger dispatch cooldown

`RequiredCooldown` blocks a trigger from passing again until at least `Cooldown`
frames have elapsed since it **last actually created a team** (stamped on the
`Start` lifecycle event, so it counts real dispatches, not draw wins). 15 frames
≈ 1 second.
```ini
RequiredCooldown=1800    ; this trigger can re-fire at most once every ~2 minutes
```
Stops a high-weight trigger from spamming the same wave back-to-back and gives
other triggers room in the rotation. `LastStartFrame` persists across save/load.
Before the first dispatch there is no cooldown (passes freely). Detail report:
`RequiredCooldown(<frames-since>)` — compared against the cooldown as a floor.

### Difficulty — restrict to specific AI difficulties

`RequiredOwnerDifficultyMin` / `RequiredOwnerDifficultyMax` gate on the owning AI
house's difficulty **index**. The engine index is *reversed*:

| Difficulty | Index |
|---|---|
| Hard   | 0 |
| Normal | 1 |
| Easy   | 2 |

```ini
RequiredOwnerDifficultyMin=0
RequiredOwnerDifficultyMax=0     ; Hard AI only (the scary waves)
```
So "Hard only" is `Min=0,Max=0`; "Normal or harder" is `Max=1`; "Easy only" is
`Min=2`. Lets a mod give tougher AI exclusive access to certain waves/tactics.
`-1` max = uncapped. Detail report: `RequiredOwnerDifficulty(<index>):min,max`.

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

---

## Debug / observability system

Global toggle in **rulesmd.ini** (not the trigger section):
```ini
[Debug]
DisplayAIWaveMessages=both   ; off (default) | yes/overlay | log | both
```
`overlay` = in-game HUD messages only, `log` = debug.log only, `both` = both.

### Trigger lifecycle events

Each fires once at a distinct point in a trigger/team's life. Overlay uses a
CSF key or `NOSTR:literal text`; the log variant is raw text.

| Event | Fires when | Hook |
|---|---|---|
| `Consider` | passed all AIExt gates, entering the weighted draw | ConditionMet epilogue |
| `Cancel` | vetoed by an AIExt gate | ConditionMet epilogue |
| `Reject` | passed gates but LOST the weighted draw | FindEligibleAITeams |
| `Selected` | WON the weighted draw (team not built yet) | FindEligibleAITeams |
| `Start` | a team was actually created for this trigger | CreateTeam return |
| `Destroyed` | team wiped out before finishing its script (failure) | RegisterFailure |
| `Deleted` | team completed its script successfully | RegisterSuccess |

```ini
[MyTrigger.AIExt]
DebugMessageDisplay.Consider=NOSTR:considering rush   ; HUD (spammy — opt-in)
DebugMessageDisplay.Cancel=STT:AI_RUSH_CANCELLED
DebugMessageDisplay.Reject=NOSTR:rush lost the draw
DebugMessageDisplay.Start=NOSTR:rush dispatched
DebugMessageDisplay.Destroyed=NOSTR:rush wiped out
DebugMessageDisplay.Deleted=NOSTR:rush succeeded
DebugLog.Consider=...    ; log-text variants (DebugLog.Cancel/Reject/Start/...)
```
Notes: `Consider`/`Reject` fire very frequently (per evaluation / per losing
trigger per selection) — prefer the log variant and tag only specific triggers.
`Start` fires once per team the AI *actually builds* (not per draw win), so it
lines up 1:1 with the eventual `Destroyed`/`Deleted`.
`Finish` exists as a tag name but is currently a no-op (no hook).

### Per-gate detail

Any gate root (e.g. `RequiredOwnerBuildings`, `RequiredEnemyDPS`,
`RequiredOwnerCredits`) can carry a debug "quad" that reports its actual value
and pass/fail on `Consider`/`Cancel`:

```ini
RequiredEnemyBuildings.Debug.MessageDisplay=NOSTR:enemy defenses:  ; HUD prefix
RequiredEnemyBuildings.Debug.ValueDisplay=yes    ; append the value (HUD, on veto)
RequiredEnemyBuildings.Debug.LogMessage=enemy defenses:            ; log header
RequiredEnemyBuildings.Debug.LogWrite=yes        ; write PASS/FAIL lines to log
RequiredEnemyBuildings.Debug.DetailsDisplay=yes  ; include per-entry breakdown
RequiredEnemyBuildings.Debug.DetailsTypes=Type, Minimum, Maximum, Current
```
Log output example (on a veto):
```
[AIExt Cancel] MyTrigger   FAIL RequiredEnemyBuildings NAPILL cur=3 min=0 max=0
```
Per-gate HUD `MessageDisplay` shows **only on Cancel, only for the failing
gate** (Consider would flood the screen). `DetailsTypes` picks which columns
appear (`Type`, `Minimum`, `Maximum`, `Current`).

---

## Weight adjustment system

Controls how a trigger's selection weight changes after its team succeeds or
fails. Extends vanilla's global `AITriggerSuccessWeightDelta` /
`AITriggerFailureWeightDelta`. Persisted across save/load.

### Per-trigger delta override

Replace the global delta for **this** trigger only (track-record scaling and
the `[Weight_Minimum, Weight_Maximum]` clamp still apply):
```ini
[MyTrigger.AIExt]
SuccessWeightDelta=30     ; +30 on success instead of the global default
FailureWeightDelta=-40    ; -40 on failure instead of the global default
```

### Cross-trigger cascades

When this trigger's team succeeds/fails, also nudge OTHER named triggers'
weights — e.g. "if this aerial rush failed, penalize the other aerial rushes":
```ini
[MyAerialRush.AIExt]
FailureCascadeTargets=OtherAerialRush1,OtherAerialRush2
FailureCascadeTargets.Delta=-15        ; positional; single value = all targets
SuccessCascadeTargets=RelatedGroundPush
SuccessCascadeTargets.Delta=10
```
Target weights are clamped to each target's own `[Weight_Minimum,
Weight_Maximum]`. With debug logging on, each adjustment logs:
```
[AIExt Cascade] Failure MyAerialRush -> OtherAerialRush1 delta -15 weight 80.0 -> 65.0
```
