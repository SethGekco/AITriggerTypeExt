#!/bin/bash
#
# AITriggerTypeExt — Rename Start → Consider (Priority 1a)
#
# Renames the "Start" lifecycle event to "Consider" throughout the DLL.
# The event fires when a trigger passes AIExt gates — but that's not
# when the team actually starts building. It's when the trigger is
# CONSIDERED for spawning by the AI weight system.
#
# Actual "Start" (team-spawn hook) is Priority 2, requires Ghidra work.
#
# Renames applied:
#   Field:      DebugMessageDisplay_Start   → DebugMessageDisplay_Consider
#   Field:      DebugLog_Start              → DebugLog_Consider
#   INI tag:    DebugMessageDisplay.Start   → DebugMessageDisplay.Consider
#   INI tag:    DebugLog.Start              → DebugLog.Consider
#   Function:   EmitDebugStart              → EmitDebugConsider
#   Log prefix: [AIExt Start]               → [AIExt Consider]
#
# Cancel and Finish are unchanged. Finish stays as no-op scaffolding
# awaiting the actual Finish hook (Priority 7).
#
# Usage from repo root:
#   bash rename-start-to-consider.sh

set -euo pipefail

REPO=$(git rev-parse --show-toplevel)
cd "$REPO"

BODY_H="src/Ext/AITriggerType/Body.h"
BODY_CPP="src/Ext/AITriggerType/Body.cpp"
HOOKS_CPP="src/Ext/AITriggerType/Hooks.cpp"

# ─── Verify anchors present before touching anything ─────────────────────
echo "Checking anchors..."
check_anchor() {
    if ! grep -qF "$2" "$1"; then
        echo "ERROR: Anchor not found in $1:"
        echo "  '$2'"
        exit 1
    fi
    echo "  ✓ $1"
}
check_anchor "$BODY_H" "DebugMessageDisplay_Start"
check_anchor "$BODY_CPP" "EmitDebugStart"
check_anchor "$BODY_CPP" '"DebugMessageDisplay.Start"'
check_anchor "$HOOKS_CPP" "EmitDebugStart"

# ─── Backups ─────────────────────────────────────────────────────────────
echo ""
echo "Backing up..."
cp "$BODY_H" "$BODY_H.bak"
cp "$BODY_CPP" "$BODY_CPP.bak"
cp "$HOOKS_CPP" "$HOOKS_CPP.bak"

# ─── Rename in Body.h ────────────────────────────────────────────────────
echo ""
echo "Renaming in $BODY_H..."
sed -i \
    -e 's/DebugMessageDisplay_Start/DebugMessageDisplay_Consider/g' \
    -e 's/DebugLog_Start/DebugLog_Consider/g' \
    -e 's/EmitDebugStart/EmitDebugConsider/g' \
    "$BODY_H"

# ─── Rename in Body.cpp ──────────────────────────────────────────────────
echo "Renaming in $BODY_CPP..."
sed -i \
    -e 's/DebugMessageDisplay_Start/DebugMessageDisplay_Consider/g' \
    -e 's/DebugLog_Start/DebugLog_Consider/g' \
    -e 's/EmitDebugStart/EmitDebugConsider/g' \
    -e 's/"DebugMessageDisplay\.Start"/"DebugMessageDisplay.Consider"/g' \
    -e 's/"DebugLog\.Start"/"DebugLog.Consider"/g' \
    -e 's/\[AIExt Start\]/[AIExt Consider]/g' \
    "$BODY_CPP"

# ─── Rename in Hooks.cpp ─────────────────────────────────────────────────
echo "Renaming in $HOOKS_CPP..."
sed -i \
    -e 's/EmitDebugStart/EmitDebugConsider/g' \
    "$HOOKS_CPP"

# ─── Verify ──────────────────────────────────────────────────────────────
echo ""
echo "─── Verification ─────────────────────────────────────────────"
echo ""
echo "Should be ZERO in all three files:"
echo "  DebugMessageDisplay_Start in Body.h:   $(grep -c 'DebugMessageDisplay_Start' $BODY_H)"
echo "  DebugMessageDisplay_Start in Body.cpp: $(grep -c 'DebugMessageDisplay_Start' $BODY_CPP)"
echo "  EmitDebugStart in Body.cpp:            $(grep -c 'EmitDebugStart' $BODY_CPP)"
echo "  EmitDebugStart in Hooks.cpp:           $(grep -c 'EmitDebugStart' $HOOKS_CPP)"
echo ""
echo "Should be NON-ZERO — Consider references exist:"
echo "  DebugMessageDisplay_Consider in Body.h:   $(grep -c 'DebugMessageDisplay_Consider' $BODY_H)"
echo "  DebugMessageDisplay_Consider in Body.cpp: $(grep -c 'DebugMessageDisplay_Consider' $BODY_CPP)"
echo "  EmitDebugConsider in Body.cpp:            $(grep -c 'EmitDebugConsider' $BODY_CPP)"
echo "  EmitDebugConsider in Hooks.cpp:           $(grep -c 'EmitDebugConsider' $HOOKS_CPP)"
echo ""
echo "─── Done ─────────────────────────────────────────────────────"
echo ""
echo "Backups: *.bak files in the same directories"
echo ""
echo "Next steps:"
echo "  git diff --stat"
echo "  git add -A"
echo "  git commit -m 'rename: Start → Consider (event fires on prereqs-pass, not team-spawn)'"
echo "  git push origin feature/debug-messages"
echo "  gh workflow run 'Build AITriggerTypeExt' --ref feature/debug-messages"
echo "  sleep 5"
echo "  gh run watch | tail -10"
echo ""
echo "After CI green:"
echo "  1. Download new DLL: gh run download \$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')"
echo "  2. Copy to game folder: cp AITriggerTypeExt*/AITriggerTypeExt.dll ~/snap/cncra2yr/common/.wine/drive_c/Westwood/RA2/"
echo "  3. Regenerate aimd.ini with tool v33 (emits Consider instead of Start)"
echo "  4. Test in-game"
