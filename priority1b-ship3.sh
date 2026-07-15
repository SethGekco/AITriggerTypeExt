#!/bin/bash
#
# AITriggerTypeExt — Priority 1b Ship #3 (FINAL — behavior change)
#
# Wires up per-index detail into the emit path. After this ships and rebuilds:
#
#   Modder opts in per-gate with:
#     RequiredOwnerBuildings.Debug.Detail=passing,failing
#     RequiredOwnerBuildings.Debug.DetailTrigger=cancel,consider
#
#   When trigger evaluates, DLL walks all indices and emits one prose line
#   per entry, filtered by the modder's opt-in settings.
#
# Output format:
#   [AIExt Cancel] 73EB11E9-G FAIL RequiredOwnerBuildings NAHAND(0):1,-1
#   [AIExt Cancel] 73EB11E9-G PASS RequiredOwnerBuildings GAWEAP(3):1,-1
#
# Modifies Body.cpp:
#   - ReadLifecycleMask: accept "consider" as synonym for "start"
#   - Add ShouldEmitDetailLine helper (matches line prefix to per-gate config)
#   - Add EmitCheckDetailLines helper (filtered emit loop)
#   - Modify EmitDebugConsider to call EmitCheckDetailLines
#   - Modify EmitDebugCancel to call EmitCheckDetailLines
#
# Modifies Hooks.cpp:
#   - Add EvaluateAndReport call before ExtraPrerequisitesMet check
#     (so LastCheckReport is populated for both pass and fail paths)

set -euo pipefail

REPO=$(git rev-parse --show-toplevel)
cd "$REPO"

BODY_CPP="src/Ext/AITriggerType/Body.cpp"
HOOKS_CPP="src/Ext/AITriggerType/Hooks.cpp"

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
check_anchor "$BODY_CPP" "void AITriggerTypeExt::EmitDebugConsider"
check_anchor "$BODY_CPP" "void AITriggerTypeExt::EmitDebugCancel"
check_anchor "$BODY_CPP" "EvaluateAndReport"
check_anchor "$BODY_CPP" "ReadLifecycleMask"
check_anchor "$HOOKS_CPP" "ExtraPrerequisitesMet"
check_anchor "$HOOKS_CPP" "EmitDebugConsider"

# Idempotency check
if grep -q "ShouldEmitDetailLine\|EmitCheckDetailLines" "$BODY_CPP"; then
    echo "ERROR: Ship #3 already applied."
    exit 1
fi

# ─── Backups ─────────────────────────────────────────────────────────────
echo ""
echo "Backing up..."
cp "$BODY_CPP" "$BODY_CPP.bak"
cp "$HOOKS_CPP" "$HOOKS_CPP.bak"

# ─── Body.cpp modifications via Python for reliability ───────────────────
echo ""
echo "Modifying $BODY_CPP..."

python3 << 'PYEOF'
import re

path = "src/Ext/AITriggerType/Body.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# ─── Step 1: Add "consider" as synonym for "start" in ReadLifecycleMask ──
old_lifecycle_start = 'if (_stricmp(tok, "start") == 0) out.on_start = true;'
new_lifecycle_start = 'if (_stricmp(tok, "start") == 0 || _stricmp(tok, "consider") == 0) out.on_start = true;'

if old_lifecycle_start in content:
    content = content.replace(old_lifecycle_start, new_lifecycle_start, 1)
    print("  ✓ ReadLifecycleMask: added 'consider' synonym")
else:
    print("  ! ReadLifecycleMask: 'start' token not found — check manually")

# ─── Step 2: Insert helpers before EmitDebugConsider ─────────────────────
helpers = r'''
// ============================================================================
// Priority 1b Ship #3 — detail-emit helpers
// ============================================================================

// Match a detail line's gate prefix to determine which Debug_*_Detail /
// Debug_*_DetailTrigger fields govern its emission. Returns true if this
// specific line should be emitted for the given lifecycle event.
static bool ShouldEmitDetailLine(
    const std::string& line,
    AITriggerTypeExt::ExtData* pExt,
    bool is_passing,
    bool is_consider_event,
    bool is_cancel_event,
    bool is_finish_event)
{
    if (!pExt || line.empty()) return false;

    AIExtDetailMode const* mode = nullptr;
    AIExtLifecycleMask const* trig = nullptr;

    // Order matters: match more-specific prefixes first (Owner/Enemy variants
    // of same base). Buildings before Units before Credits etc.
    #define MATCH(prefix, mode_f, trig_f) \
        if (line.compare(0, strlen(prefix), prefix) == 0) { \
            mode = &pExt->mode_f; trig = &pExt->trig_f; \
        }

    MATCH("RequiredOwnerBuildings",   Debug_Owner_Buildings_Detail,   Debug_Owner_Buildings_DetailTrigger)
    else MATCH("RequiredEnemyBuildings",   Debug_Enemy_Buildings_Detail,   Debug_Enemy_Buildings_DetailTrigger)
    else MATCH("RequiredEnemyUnits",       Debug_Enemy_Units_Detail,       Debug_Enemy_Units_DetailTrigger)
    else MATCH("RequiredNeutralBuildings", Debug_Neutral_Buildings_Detail, Debug_Neutral_Buildings_DetailTrigger)
    else MATCH("RequiredOwnerCredits",     Debug_Owner_Credits_Detail,     Debug_Owner_Credits_DetailTrigger)
    else MATCH("RequiredEnemyCredits",     Debug_Enemy_Credits_Detail,     Debug_Enemy_Credits_DetailTrigger)
    else MATCH("RequiredOwnerPower",       Debug_Owner_Power_Detail,       Debug_Owner_Power_DetailTrigger)
    else MATCH("RequiredEnemyPower",       Debug_Enemy_Power_Detail,       Debug_Enemy_Power_DetailTrigger)
    else MATCH("RequiredElapsedTime",      Debug_ElapsedTime_Detail,       Debug_ElapsedTime_DetailTrigger)
    // RequiredOwnerUnits: no Debug field scaffolded — silently skipped
    // (add Debug_Owner_Units_Detail scaffolding in follow-up patch to enable)

    #undef MATCH

    if (!mode || !trig) return false;

    // Category filter: modder must have opted into showing passing/failing
    if (is_passing && !mode->show_passing) return false;
    if (!is_passing && !mode->show_failing) return false;

    // Lifecycle filter: modder must have opted into this event
    if (is_consider_event && !trig->on_start) return false;
    if (is_cancel_event   && !trig->on_cancel) return false;
    if (is_finish_event   && !trig->on_finish) return false;

    return true;
}

// Iterate LastCheckReport and emit per-gate detail lines to overlay+log,
// filtered by per-gate Debug.Detail= and Debug.DetailTrigger= settings.
// Called from EmitDebugConsider/Cancel/Finish after the base lifecycle
// message has been emitted.
static void EmitCheckDetailLines(
    AITriggerTypeExt::ExtData* pExt,
    AITriggerTypeClass* pThis,
    DebugDisplayMode mode,
    const char* event_prefix,
    bool is_consider_event,
    bool is_cancel_event,
    bool is_finish_event)
{
    if (!pExt || !pThis) return;
    if (mode == DebugDisplayMode::Off) return;

    auto emit_line = [&](const std::string& line, bool passing) {
        if (!ShouldEmitDetailLine(line, pExt, passing,
                is_consider_event, is_cancel_event, is_finish_event))
            return;

        const char* status = passing ? "PASS" : "FAIL";

        // Log line — full format
        if (mode == DebugDisplayMode::Log || mode == DebugDisplayMode::Both) {
            Debug::Log("[AIExt %s] %s %s %s\n",
                event_prefix, pThis->ID, status, line.c_str());
        }

        // Overlay — compressed single line
        if (mode == DebugDisplayMode::Overlay || mode == DebugDisplayMode::Both) {
            char buf[240];
            snprintf(buf, sizeof(buf), "%s %s %s",
                pThis->ID, status, line.c_str());
            const wchar_t* pMsg = ToWideStatic(buf);
            if (pMsg && *pMsg)
                MessageListClass::Instance.PrintMessage(pMsg);
        }
    };

    for (auto const& l : pExt->LastCheckReport.passing) emit_line(l, true);
    for (auto const& l : pExt->LastCheckReport.failing) emit_line(l, false);
}

'''

# Find EmitDebugConsider and insert helpers immediately before it
consider_marker = "void AITriggerTypeExt::EmitDebugConsider("
if consider_marker in content:
    content = content.replace(consider_marker, helpers + consider_marker, 1)
    print("  ✓ Inserted ShouldEmitDetailLine + EmitCheckDetailLines helpers")
else:
    print("  ERROR: EmitDebugConsider not found")
    raise SystemExit(1)

# ─── Step 3: Modify EmitDebugConsider to call EmitCheckDetailLines ───────
# Find the end of EmitDebugConsider's function body and insert the call.
# The function ends with the log block's closing brace, then the function's
# closing brace.

# Pattern: match the Log block inside EmitDebugConsider ending with closing }
consider_pattern = re.compile(
    r'(void AITriggerTypeExt::EmitDebugConsider\([^)]*\)\s*\{[^}]*?'
    r'Debug::Log\("\[AIExt Consider\][^;]*;\s*\}\s*)\}',
    re.DOTALL
)

def add_consider_call(m):
    body = m.group(1)
    return body + '\n    // Priority 1b: emit per-gate detail lines\n    EmitCheckDetailLines(pExt, pThis, mode, "Consider",\n        /*is_consider_event=*/true, /*is_cancel_event=*/false, /*is_finish_event=*/false);\n}'

new_content, n = consider_pattern.subn(add_consider_call, content)
if n == 1:
    content = new_content
    print("  ✓ EmitDebugConsider: added detail emit call")
else:
    print(f"  ERROR: EmitDebugConsider pattern matched {n} times (expected 1)")
    raise SystemExit(1)

# ─── Step 4: Modify EmitDebugCancel similarly ────────────────────────────
cancel_pattern = re.compile(
    r'(void AITriggerTypeExt::EmitDebugCancel\([^)]*\)\s*\{[^}]*?'
    r'Debug::Log\("\[AIExt Cancel\][^;]*;\s*\}\s*)\}',
    re.DOTALL
)

def add_cancel_call(m):
    body = m.group(1)
    return body + '\n    // Priority 1b: emit per-gate detail lines\n    EmitCheckDetailLines(pExt, pThis, mode, "Cancel",\n        /*is_consider_event=*/false, /*is_cancel_event=*/true, /*is_finish_event=*/false);\n}'

new_content, n = cancel_pattern.subn(add_cancel_call, content)
if n == 1:
    content = new_content
    print("  ✓ EmitDebugCancel: added detail emit call")
else:
    print(f"  ERROR: EmitDebugCancel pattern matched {n} times (expected 1)")
    raise SystemExit(1)

# Write out
with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("Body.cpp updated.")
PYEOF

# ─── Hooks.cpp modification ──────────────────────────────────────────────
echo ""
echo "Modifying $HOOKS_CPP..."

python3 << 'PYEOF'
path = "src/Ext/AITriggerType/Hooks.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# Insert EvaluateAndReport call just BEFORE the ExtraPrerequisitesMet check
# The pattern is:
#    if (!pExt->ExtraPrerequisitesMet(pOwner, pEnemy))
#
# We want:
#    pExt->EvaluateAndReport(pOwner, pEnemy);   // Priority 1b: build LastCheckReport
#    if (!pExt->ExtraPrerequisitesMet(pOwner, pEnemy))

marker = "if (!pExt->ExtraPrerequisitesMet(pOwner, pEnemy))"
insertion = "pExt->EvaluateAndReport(pOwner, pEnemy);  // Priority 1b: populate LastCheckReport\n    "

if marker in content:
    content = content.replace(marker, insertion + marker, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("  ✓ Inserted EvaluateAndReport call before ExtraPrerequisitesMet")
else:
    print("  ERROR: ExtraPrerequisitesMet marker not found")
    raise SystemExit(1)
PYEOF

# ─── Verify ──────────────────────────────────────────────────────────────
echo ""
echo "─── Verification ─────────────────────────────────────────────"
echo ""
echo "Body.cpp changes:"
grep -c "ShouldEmitDetailLine\|EmitCheckDetailLines" "$BODY_CPP" | xargs echo "  Helper refs (expect ~4):"
grep -c '"consider"' "$BODY_CPP" | xargs echo "  'consider' synonym literal (expect ~1):"
echo ""
echo "Hooks.cpp changes:"
grep -c "EvaluateAndReport" "$HOOKS_CPP" | xargs echo "  EvaluateAndReport calls (expect 1):"

echo ""
echo "─── Done ─────────────────────────────────────────────────────"
echo ""
echo "Backups saved to $BODY_CPP.bak and $HOOKS_CPP.bak"
echo ""
echo "Next steps:"
echo "  git diff --stat"
echo "  git diff src/Ext/AITriggerType/Body.cpp | head -80"
echo "  git diff src/Ext/AITriggerType/Hooks.cpp"
echo "  git add -A"
echo "  git commit -m 'feat(debug): Priority 1b Ship #3 — per-index detail in emit path'"
echo "  git push origin feature/debug-messages"
echo "  gh workflow run 'Build AITriggerTypeExt' --ref feature/debug-messages"
echo "  sleep 5"
echo "  gh run list --limit 1 | awk '{print \$1, \$2}'"
