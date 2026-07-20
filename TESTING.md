# AITriggerTypeExt — Feature Test Kit

Drop-in `[Trigger.AIExt]` blocks for a minimal aimd.ini, each isolating a
feature. Attach a block to one of your test triggers (any trigger with a valid
`Team1` that the AI can evaluate/spawn). Every value below uses real vanilla
IDs — swap for your mod's as needed.

## Global setup (rulesmd.ini, once)

```ini
[Debug]
DisplayAIWaveMessages=both   ; off (default) | yes/overlay | log | both
```

## Reading the results

- **HUD**: overlay messages appear top-left in game.
- **Log**: `drive_c/Westwood/RA2/debug/debug.log`. One-shot summary:
  ```bash
  grep -oE '\[AIExt [A-Za-z]+\]' debug/debug.log | sort | uniq -c
  ```
  Per feature: `grep '\[AIExt Cascade\]' debug/debug.log`, etc.

## ⚠️ Tags that do NOTHING (don't bother testing)

- `DebugMessageDisplay.Finish` / `DebugLog.Finish` — no hook exists (dead scaffold).
- `Required<X>.DebugLog=yes` (the per-gate boolean) — parsed but never consumed;
  superseded by the `.Debug.*` quad below.

---

## Full tag inventory

**Gate conditions** (each has `Min`/`Max`; `-1` max = uncapped; omit = no check).
Prefix with `Owner` / `Enemy` / `Allies` / `Neutral`:
`RequiredOwnerBuildings(+Min/Max)`, `RequiredOwnerUnits(+Min/Max)`,
`RequiredOwnerSuperWeapons(+ReadyMin/ReadyMax)`, `RequiredOwnerCreditsMin/Max`,
`RequiredOwnerPowerMin/Max`, `RequiredOwnerPowerOutputMin/Max`,
`RequiredOwnerTechLevelMin/Max`, `RequiredOwnerDPSMin/Max`.
Global: `RequiredElapsedTimeMin/Max`. Modes: `TargetHouseMode`
(`Current`/`Any`/`All`/`Most_Buildings`/`Least_Buildings`/`Most_Units`/`Least_Units`),
`RequiredAlliesMode` (`Any`/`All`).

**Lifecycle** (`DebugMessageDisplay.<E>` = HUD, `DebugLog.<E>` = log), E ∈
`Consider`, `Cancel`, `Reject`, `Start`, `Destroyed`, `Deleted`.

**Per-gate quad** — `<GateRoot>.Debug.<sub>`, sub ∈ `MessageDisplay` (HUD prefix),
`ValueDisplay` (yes = append value on HUD), `LogMessage` (log header),
`LogWrite` (yes = PASS/FAIL lines to log), `DetailsDisplay` (yes),
`DetailsTypes` (`Type, Minimum, Maximum, Current` — pick any subset).

**Weight** — `SuccessWeightDelta`, `FailureWeightDelta`,
`SuccessCascadeTargets`(+`.Delta`), `FailureCascadeTargets`(+`.Delta`).

---

## Test A — Consider + per-gate PASS detail

Owner has a ConYard → passes → `Consider` fires; log shows the PASS breakdown.

```ini
[TEST_A-G.AIExt]
RequiredOwnerBuildings=NACNST
RequiredOwnerBuildingsMin=1
DebugMessageDisplay.Consider=NOSTR:TEST_A considered
DebugLog.Consider=TEST_A passed gates
RequiredOwnerBuildings.Debug.LogMessage=TEST_A owner ConYard:
RequiredOwnerBuildings.Debug.LogWrite=yes
RequiredOwnerBuildings.Debug.DetailsTypes=Type, Minimum, Maximum, Current
```
Expect (log): `[AIExt Consider] TEST_A-G: TEST_A passed gates` and
`[AIExt Consider] TEST_A-G   PASS RequiredOwnerBuildings NACNST cur=1 min=1 max=-1`.

## Test B — Cancel + per-gate FAIL detail + HUD-on-veto

Requires an impossible enemy building count → veto → `Cancel`.

```ini
[TEST_B-G.AIExt]
RequiredEnemyBuildings=GACNST
RequiredEnemyBuildingsMin=99
DebugMessageDisplay.Cancel=NOSTR:TEST_B cancelled
DebugLog.Cancel=TEST_B vetoed
RequiredEnemyBuildings.Debug.MessageDisplay=NOSTR:enemy ConYards:
RequiredEnemyBuildings.Debug.ValueDisplay=yes
RequiredEnemyBuildings.Debug.LogMessage=TEST_B enemy ConYard:
RequiredEnemyBuildings.Debug.LogWrite=yes
RequiredEnemyBuildings.Debug.DetailsTypes=Type, Current
```
Expect (HUD): `TEST_B cancelled`, then the failing gate `enemy ConYards: GACNST=1`.
Expect (log): `[AIExt Cancel] TEST_B-G   FAIL RequiredEnemyBuildings GACNST cur=1 min=99 max=-1`.

## Test C — Reject + Start

Passes gates, competes each cycle → wins (`Start`) or loses (`Reject`).
Needs other eligible triggers so it sometimes loses the draw.

```ini
[TEST_C-G.AIExt]
RequiredOwnerBuildings=NACNST
RequiredOwnerBuildingsMin=1
DebugLog.Start=TEST_C won the draw
DebugMessageDisplay.Reject=NOSTR:TEST_C lost the draw
DebugLog.Reject=TEST_C rejected
```
Expect (log): a mix of `[AIExt Start] TEST_C-G` and `[AIExt Reject] TEST_C-G`.

## Test D — Destroyed + Deleted

Team spawns, then dies (`Destroyed`) or completes its script (`Deleted`).
**Deleted only fires if the team's script reaches action `49,0` (Register
Success)** — use a completeable script (e.g. patrol/guard → 49,0), not a suicide team.

```ini
[TEST_D-G.AIExt]
RequiredOwnerBuildings=NAHAND
RequiredOwnerBuildingsMin=1
DebugMessageDisplay.Destroyed=NOSTR:TEST_D wiped out
DebugLog.Destroyed=TEST_D team died
DebugMessageDisplay.Deleted=NOSTR:TEST_D succeeded
DebugLog.Deleted=TEST_D team completed
```
Expect: `[AIExt Destroyed] TEST_D-G` or `[AIExt Deleted] TEST_D-G`.

## Test E — Self weight-delta + cross-trigger cascade

Overrides the global weight deltas and, on failure, also penalizes TEST_D.

```ini
[TEST_E-G.AIExt]
RequiredOwnerBuildings=NAHAND
RequiredOwnerBuildingsMin=1
SuccessWeightDelta=40
FailureWeightDelta=-50
FailureCascadeTargets=TEST_D-G
FailureCascadeTargets.Delta=-25
DebugLog.Start=TEST_E started
DebugLog.Destroyed=TEST_E died
DebugLog.Deleted=TEST_E completed
```
Expect (log): `[AIExt WeightDelta] Failure TEST_E-G: override -50 (vanilla global -20)`
and `[AIExt Cascade] Failure TEST_E-G -> TEST_D-G delta -25 weight 80.0 -> 55.0`.

## Test F — Enemy DPS gate + value readout

Only fires while the enemy's summed firepower is low; the log prints the actual
computed DPS so you can confirm the math.

```ini
[TEST_F-G.AIExt]
RequiredEnemyDPSMax=500
DebugMessageDisplay.Cancel=NOSTR:TEST_F blocked (enemy too strong)
DebugLog.Cancel=TEST_F enemy DPS too high
RequiredEnemyDPS.Debug.LogMessage=TEST_F enemy DPS:
RequiredEnemyDPS.Debug.LogWrite=yes
RequiredEnemyDPS.Debug.DetailsTypes=Current, Maximum
```
Expect (log): `[AIExt Cancel] TEST_F-G   FAIL RequiredEnemyDPS cur=1234 max=500`
(or a PASS line on Consider once the enemy is weak enough). The `cur=` number is
the live computed DPS — sanity-check it grows as the enemy builds an army.

## Test G — the other scalar gates (value readouts)

Unreachable credits keeps it vetoed; the detail walk evaluates **every** set
gate, so on each Cancel you see the current value of each (the failing credits
gate AND the passing power gate).

```ini
[TEST_G-G.AIExt]
RequiredOwnerCreditsMin=100000     ; unreachable → always vetoed
RequiredOwnerCredits.Debug.LogMessage=owner credits:
RequiredOwnerCredits.Debug.LogWrite=yes
RequiredOwnerPowerMin=0            ; passes when not in deficit
RequiredOwnerPower.Debug.LogMessage=owner power:
RequiredOwnerPower.Debug.LogWrite=yes
DebugLog.Cancel=TEST_G vetoed (credits gate)
```
Expect (log, on Cancel): `FAIL RequiredOwnerCredits cur=5000 min=100000 max=-1`
**and** `PASS RequiredOwnerPower cur=120 min=0 max=-1` — both gates reported.

### Elapsed-time flip (separate, single-gate trigger)

To watch a gate flip a trigger from Cancel → Consider over time, give it *only*
this gate (nothing else to keep it vetoed):

```ini
[TEST_G2-G.AIExt]
RequiredElapsedTimeMin=1500        ; ~100s at 15fps
DebugLog.Cancel=TEST_G2 too early
DebugLog.Consider=TEST_G2 time reached
```
Expect: `[AIExt Cancel] TEST_G2-G` for the first ~100s, then `[AIExt Consider]`
once game time passes frame 1500.

---

## Coverage checklist

- [ ] Consider / Cancel (A, B)
- [ ] Per-gate detail: LogWrite lines, DetailsTypes columns, HUD-on-veto (A, B, F, G)
- [ ] Reject / Start (C)
- [ ] Destroyed / Deleted (D)
- [ ] SuccessWeightDelta / FailureWeightDelta (E)
- [ ] Success/FailureCascadeTargets (E)
- [ ] RequiredEnemyDPS + value (F)
- [ ] Credits / Power / TechLevel / ElapsedTime gates (G)
