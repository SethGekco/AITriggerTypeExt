#!/bin/bash
#
# AITriggerTypeExt — Priority 1b Ship #2
#
# Adds detail-building shadow Check methods. Walks all indices instead of
# short-circuiting. Populates AIExtCheckDetail with prose lines per entry.
#
# ZERO BEHAVIOR CHANGE. Nothing calls these methods yet — Ship #3 wires
# them into the emit path.
#
# Adds to Body.h (inside class ExtData):
#   - mutable AIExtCheckDetail LastCheckReport
#   - BuildBuildingsDetail, BuildUnitsDetail, BuildScalarDetail method decls
#   - EvaluateAndReport(pOwner, pEnemy) orchestrator decl
#
# Adds to Body.cpp:
#   - Implementations of all 4 methods above
#
# Coverage: Buildings, Units, Credits, Power, ElapsedTime — for Owner and
# Enemy scopes. SuperWeapons/PowerOutput/TechLevel/Allies/Neutral deferred.

set -euo pipefail

REPO=$(git rev-parse --show-toplevel)
cd "$REPO"

BODY_H="src/Ext/AITriggerType/Body.h"
BODY_CPP="src/Ext/AITriggerType/Body.cpp"

# ─── Verify anchors ──────────────────────────────────────────────────────
echo "Checking anchors..."

check_anchor() {
    if ! grep -qF "$2" "$1"; then
        echo "ERROR: Anchor not found in $1:"
        echo "  '$2'"
        exit 1
    fi
    echo "  ✓ $1"
}

# Ship #1 must have landed (these anchors are from Ship #1 output)
check_anchor "$BODY_H" "AIExtCheckDetail"
check_anchor "$BODY_H" "Debug_ElapsedTime_DetailTrigger"
check_anchor "$BODY_CPP" "ReadDetailMode"
check_anchor "$BODY_CPP" "ExtraPrerequisitesMet"

# Verify Ship #2 hasn't been applied yet (idempotency check)
if grep -q "EvaluateAndReport" "$BODY_H"; then
    echo "ERROR: Ship #2 appears to already be applied."
    echo "  Found 'EvaluateAndReport' in $BODY_H"
    echo "  Skipping to prevent double-apply."
    exit 1
fi

# ─── Backups ─────────────────────────────────────────────────────────────
echo ""
echo "Backing up..."
cp "$BODY_H" "$BODY_H.bak"
cp "$BODY_CPP" "$BODY_CPP.bak"

# ─── Body.h: insert declarations after last DetailTrigger field ──────────
echo ""
echo "Modifying $BODY_H..."

python3 << 'PYEOF'
import re

path = "src/Ext/AITriggerType/Body.h"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# The Ship #1 fields end with Debug_ElapsedTime_DetailTrigger { false, false, true };
# We insert AFTER that line.
anchor = "        AIExtLifecycleMask   Debug_ElapsedTime_DetailTrigger { false, false, true };"

if anchor not in content:
    print("ERROR: Anchor not found exactly. Ship #1 output may have different whitespace.")
    # Try to find similar pattern
    import re
    m = re.search(r'AIExtLifecycleMask\s+Debug_ElapsedTime_DetailTrigger\s*\{[^}]*\}\s*;', content)
    if m:
        anchor = m.group(0)
        print(f"  Found alternate: {anchor}")
    else:
        raise SystemExit(1)

insertion = '''

        // Priority 1b — mutable check report populated by EvaluateAndReport()
        // Reset at the start of each evaluation; consumed by EmitDebug* functions.
        mutable AIExtCheckDetail LastCheckReport;

        // Priority 1b — detail-building shadow methods (walk all indices, no short-circuit)
        // Populate `out` with one prose line per gate entry, categorized pass/fail.
        void BuildBuildingsDetail(
            HouseClass* pHouse,
            const TypeCountGate<BuildingTypeClass>& gate,
            const char* gate_name,
            AIExtCheckDetail& out) const;
        void BuildUnitsDetail(
            HouseClass* pHouse,
            const TypeCountGate<TechnoTypeClass>& gate,
            const char* gate_name,
            AIExtCheckDetail& out) const;
        void BuildScalarDetail(
            const char* gate_name,
            int actual,
            const Nullable<int>& min,
            const Nullable<int>& max,
            AIExtCheckDetail& out) const;

        // Priority 1b — orchestrator: walks all set gates, populates LastCheckReport
        void EvaluateAndReport(HouseClass* pOwner, HouseClass* pEnemy) const;
'''

new_content = content.replace(anchor, anchor + insertion, 1)
if new_content == content:
    print("ERROR: Insertion produced no changes.")
    raise SystemExit(1)

with open(path, "w", encoding="utf-8") as f:
    f.write(new_content)
print(f"  ✓ Inserted method declarations after Ship #1 fields")
PYEOF

# ─── Body.cpp: append implementations at end of file ─────────────────────
echo ""
echo "Appending to $BODY_CPP..."

cat >> "$BODY_CPP" << 'CPPEOF'


// ============================================================================
// Priority 1b — detail-building shadow methods
// Walk all indices without short-circuit. Called by EmitDebug* functions
// when the modder has enabled Debug.Detail on any gate.
// Format per entry:
//   List gates:   "{GateName} {TypeID}({actual}):{min},{max}"
//   Scalar gates: "{GateName}({actual}):{min},{max}"
// Entries are categorized into out.passing or out.failing.
// ============================================================================

void AITriggerTypeExt::ExtData::BuildBuildingsDetail(
    HouseClass* pHouse,
    const TypeCountGate<BuildingTypeClass>& gate,
    const char* gate_name,
    AIExtCheckDetail& out) const
{
    if (!pHouse) return;
    for (size_t i = 0; i < gate.Types.size(); ++i)
    {
        if (!gate.Types[i]) continue;
        int count = pHouse->CountOwnedAndPresent(gate.Types[i]);
        int min_v = gate.GetMin(i);
        int max_v = gate.GetMax(i);
        bool ok = gate.CheckCount(i, count);

        char line[128];
        snprintf(line, sizeof(line), "%s %s(%d):%d,%d",
                 gate_name, gate.Types[i]->ID, count, min_v, max_v);
        (ok ? out.passing : out.failing).push_back(line);
    }
}

void AITriggerTypeExt::ExtData::BuildUnitsDetail(
    HouseClass* pHouse,
    const TypeCountGate<TechnoTypeClass>& gate,
    const char* gate_name,
    AIExtCheckDetail& out) const
{
    if (!pHouse) return;
    for (size_t i = 0; i < gate.Types.size(); ++i)
    {
        if (!gate.Types[i]) continue;
        int count = CountOwnedTechnoType(pHouse, gate.Types[i]);
        int min_v = gate.GetMin(i);
        int max_v = gate.GetMax(i);
        bool ok = gate.CheckCount(i, count);

        char line[128];
        snprintf(line, sizeof(line), "%s %s(%d):%d,%d",
                 gate_name, gate.Types[i]->ID, count, min_v, max_v);
        (ok ? out.passing : out.failing).push_back(line);
    }
}

void AITriggerTypeExt::ExtData::BuildScalarDetail(
    const char* gate_name,
    int actual,
    const Nullable<int>& min,
    const Nullable<int>& max,
    AIExtCheckDetail& out) const
{
    // Mirror the vanilla check logic: min fails if actual < min,
    // max fails if actual > max (and max != -1 which means uncapped).
    bool ok = true;
    if (min.isset() && actual < min.Get()) ok = false;
    if (max.isset() && max.Get() != -1 && actual > max.Get()) ok = false;

    int min_display = min.isset() ? min.Get() : 0;
    int max_display = max.isset() ? max.Get() : -1;

    char line[128];
    snprintf(line, sizeof(line), "%s(%d):%d,%d",
             gate_name, actual, min_display, max_display);
    (ok ? out.passing : out.failing).push_back(line);
}

void AITriggerTypeExt::ExtData::EvaluateAndReport(HouseClass* pOwner, HouseClass* pEnemy) const
{
    LastCheckReport.passing.clear();
    LastCheckReport.failing.clear();

    if (!pOwner) return;

    // ─── Owner scope ────────────────────────────────────────────────────
    if (!OwnerBuildings.empty())
        BuildBuildingsDetail(pOwner, OwnerBuildings,
            "RequiredOwnerBuildings", LastCheckReport);
    if (!OwnerUnits.empty())
        BuildUnitsDetail(pOwner, OwnerUnits,
            "RequiredOwnerUnits", LastCheckReport);
    if (OwnerCreditsMin.isset() || OwnerCreditsMax.isset())
        BuildScalarDetail("RequiredOwnerCredits", pOwner->Balance,
            OwnerCreditsMin, OwnerCreditsMax, LastCheckReport);
    if (OwnerPowerMin.isset() || OwnerPowerMax.isset())
    {
        int net = pOwner->PowerOutput - pOwner->PowerDrain;
        BuildScalarDetail("RequiredOwnerPower", net,
            OwnerPowerMin, OwnerPowerMax, LastCheckReport);
    }

    // ─── Enemy scope ────────────────────────────────────────────────────
    if (pEnemy)
    {
        if (!EnemyBuildings.empty())
            BuildBuildingsDetail(pEnemy, EnemyBuildings,
                "RequiredEnemyBuildings", LastCheckReport);
        if (!EnemyUnits.empty())
            BuildUnitsDetail(pEnemy, EnemyUnits,
                "RequiredEnemyUnits", LastCheckReport);
        if (EnemyCreditsMin.isset() || EnemyCreditsMax.isset())
            BuildScalarDetail("RequiredEnemyCredits", pEnemy->Balance,
                EnemyCreditsMin, EnemyCreditsMax, LastCheckReport);
        if (EnemyPowerMin.isset() || EnemyPowerMax.isset())
        {
            int net = pEnemy->PowerOutput - pEnemy->PowerDrain;
            BuildScalarDetail("RequiredEnemyPower", net,
                EnemyPowerMin, EnemyPowerMax, LastCheckReport);
        }
    }

    // ─── ElapsedTime (game-scope, no house needed) ──────────────────────
    if (ElapsedTimeMin.isset() || ElapsedTimeMax.isset())
    {
        int elapsed = Unsorted::CurrentFrame;
        BuildScalarDetail("RequiredElapsedTime", elapsed,
            ElapsedTimeMin, ElapsedTimeMax, LastCheckReport);
    }

    // ─── Deferred to follow-up ships ────────────────────────────────────
    // SuperWeapons: SWReadyGate has different API (ReadyMin/Max + frame math)
    // PowerOutput / TechLevel: verify field names first
    // Allies: multi-house walk (see CheckAllies for pattern)
    // Neutral: global neutral house lookup
}
CPPEOF

# ─── Verify ──────────────────────────────────────────────────────────────
echo ""
echo "─── Verification ─────────────────────────────────────────────"

body_h_matches=$(grep -c "BuildBuildingsDetail\|BuildUnitsDetail\|BuildScalarDetail\|EvaluateAndReport\|LastCheckReport" "$BODY_H")
body_cpp_matches=$(grep -c "BuildBuildingsDetail\|BuildUnitsDetail\|BuildScalarDetail\|EvaluateAndReport" "$BODY_CPP")

echo "  Body.h  matches (expect ~10): $body_h_matches"
echo "  Body.cpp matches (expect ~15): $body_cpp_matches"

if [ "$body_h_matches" -lt 5 ] || [ "$body_cpp_matches" -lt 8 ]; then
    echo "  WARN: match counts look wrong. Diff-check before committing."
fi

echo ""
echo "─── Done ─────────────────────────────────────────────────────"
echo ""
echo "Backups saved to $BODY_H.bak and $BODY_CPP.bak"
echo ""
echo "Next steps:"
echo "  git diff --stat"
echo "  git diff src/Ext/AITriggerType/Body.h | head -30"
echo "  git diff src/Ext/AITriggerType/Body.cpp | tail -60"
echo "  git add -A"
echo "  git commit -m 'wip(debug): Priority 1b Ship #2 — detail-building methods'"
echo "  git push origin feature/debug-messages"
echo "  gh workflow run 'Build AITriggerTypeExt' --ref feature/debug-messages"
echo "  sleep 5"
echo "  gh run list --limit 1 | awk '{print \$1, \$2}'"
