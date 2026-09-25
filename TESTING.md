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

## Test H — TeamType sidecar: team-scoped messages + TeamRetaliate

Tag a TeamType the AI reliably builds (any engineer-rush or harass team works).
Both features live on the TEAM section, not the trigger:

```ini
[SomeTeamID.AIExt]
DebugMessageDisplay.Destroyed=NOSTR:TEST_H team wiped
DebugLog.Destroyed=TEST_H team destroyed
DebugMessageDisplay.Deleted=NOSTR:TEST_H team finished
DebugLog.Deleted=TEST_H team completed script
TeamRetaliate=no
```

Expect (team-scoped messages):
- `[AIExt TeamDestroyed] <TeamID>: TEST_H team destroyed` when any team of
  this type dies before finishing its script — including teams NOT spawned by
  an AI trigger.
- `[AIExt TeamDeleted] …` if a team completes its script (action 49,0) —
  rare for suicide teams, same caveat as trigger-scoped Deleted.
- Counts should line up 1:1 with the trigger-scoped Destroyed/Deleted for
  trigger-spawned teams (same engine flag decides both).
- A burst at game end is normal (scenario teardown destroys all teams).

Expect (TeamRetaliate=no): shoot the tagged team's members while they travel —
they must KEEP MOVING to their scripted objective instead of turning on the
attacker (untagged teams still retaliate, since the Antares global is on).
`Annoyance=yes` regrouping still happens; only the retarget is suppressed.

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
- [ ] Team-scoped Destroyed/Deleted + TeamRetaliate=no (H)
- [ ] FormationKeep hold/resume on a mixed-speed team (I)
- [ ] MissileEvasive scatter vs V3/Dreadnought (J)

---

## Test I — FormationKeep

Tag a deliberately mixed-speed team (e.g. tanks + infantry, or dogs + a slow
tank) whose script has a long approach march (53 gather or 47 move):

```ini
[SomeMixedTeamID.AIExt]
FormationKeep=yes
FormationKeep.Radius=4
FormationKeep.Resume=2
```

Expect, while the team is on its move action:
- The fast units burst ahead ~4 cells, STOP, wait for the slow ones to close
  to ~2 cells, move again — a visible caterpillar march that keeps the blob
  together. Compare an untagged copy of the same taskforce: its fast units
  should string out and arrive piecemeal.
- The moment the script flips to an attack action, everyone fights at full
  speed — nobody may be frozen under fire.
- If the team dies mid-march, no survivor may remain frozen in place
  (destructor release).

---

## Test J — MissileEvasive

Tag a team the AI parks defensively (a guard/defense team is ideal — it sits
still long enough to shoot missiles at):

```ini
[SomeGuardTeamID.AIExt]
MissileEvasive=yes
MissileEvasive.Cells=4
MissileEvasive.WH.Calc=yes
```

Then fire V3s / Dreadnought missiles / Boris strikes at the tagged team's
position (or let a Soviet AI do it).

Expect:
- As the missile comes in, tagged members inside the blast footprint run
  OUT of it — roughly straight away from the aim point — and the missile
  hits (mostly) empty ground. Untagged teams stand and eat it.
- With WH.Calc=yes and a big-CellSpread warhead, the dodge distance grows to
  clear the whole footprint (larger of Cells vs CellSpread+1 wins).
- Small missiles (AA rockets, IFV rockets) must NOT trigger dancing — the
  default MinDamage=100 filters them. Set MissileEvasive.MinDamage=0 to
  watch the unfiltered behavior for comparison.
- Members mid-dodge resume their team's script once the threat is gone
  (gather/regroup pulls them back together).

---

## Test K — GuardMe / Escort

Pick one AITriggerType with both team slots: Team1 = a slow valuable team
(Kirovs are perfect), Team2 = a fast SAME-FACTION escort squadron — Flak
Tracks (HTK) are ideal under Kirovs; Rhinos (HTNK) also work. Both teams
must be buildable by the same house (Owner/prerequisites), or the pair can
never exist in one AI's hands:

```ini
[KirovTeamID.AIExt]
GuardMe=yes
GuardMe.Radius=6
GuardMe.Scope=trigger

[FlakTrackTeamID.AIExt]
Escort=yes
```

Expect:
- When the trigger fires, the escorts converge on the Kirovs and stay within
  ~6 cells as the Kirovs fly their route — a visible moving bubble.
- Shoot at the formation: escorts inside the bubble engage (Area Guard), then
  fall back into formation as the guardee moves on.
- Kill the Kirovs: the escorts stop shadowing and resume their own script
  (or pick another GuardMe team if one is alive and in scope).
- With GuardMe.Scope=trigger, an unrelated Escort team must NOT adopt the
  Kirovs — only the Team2 sibling squadron may.

---

## Test L — Steamroll

Tag a cheap, fast-build taskforce (infantry rush or a light-vehicle rush is
ideal — the effect is easiest to read when units queue quickly) on a trigger
with generous weight so it fires readily:

```ini
[SomeRushTeamID.AIExt]
Steamroll=yes
```

Do **not** also set `Reinforce=yes` in the TeamType's own vanilla section —
Steamroll sets it for you; adding it again is harmless but redundant.

Expect:
- The team is built once, sent off to fight/attrit, and — unlike a normal
  one-shot taskforce — the AI keeps queuing replacements into the SAME team
  indefinitely, as long as at least one instance is alive. Compare an
  untagged copy of the same taskforce/trigger: it builds once and never
  refills.
- Watch factory load: a Steamroll team competes for build slots like any
  other production, so on a factory-starved house it may refill slowly (this
  is expected — Steamroll doesn't grant priority, just removes the "full"
  stop condition).
- Disable or destroy the owning trigger (or let a script self-disable it) —
  refilling must stop immediately once the last team instance of that type
  is destroyed. There is no separate "off" switch; the team's own death is
  the stop condition.
- Good stress case: pair with a weak early-game rush trigger and watch it
  become a genuine flood instead of a one-and-done poke.
