#!/bin/bash
#
# AITriggerTypeExt — Priority 1 Debug Scaffolding
#
# Adds SCAFFOLDING ONLY for per-index veto detail. Does NOT wire the actual
# behavior yet (Check functions unchanged). Ships as a compilable base
# for the follow-up commit that adds the per-index logic.
#
# What this script does:
#   1. Adds #include <vector> to Body.h
#   2. Adds three file-scope structs (AIExtCheckDetail, AIExtDetailMode,
#      AIExtLifecycleMask) BEFORE class AITriggerTypeExt
#   3. Adds per-gate detail fields inside class ExtData
#   4. Adds ReadDetailMode and ReadLifecycleMask helper functions to Body.cpp
#   5. Adds INI reads for the new Debug.Detail / Debug.DetailTrigger tags
#   6. Fixes a real bug — EmitDebugCancel was reading _Start instead of _Cancel
#
# Usage from repo root:
#   bash apply-priority1-scaffolding.sh
#
# Safety:
#   - Backs up both files as .bak before modification
#   - Verifies anchor strings exist before making changes
#   - Aborts on any missing anchor
#   - Prints exactly what was changed

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
    echo "  ✓ $1 has: $2"
}
check_anchor "$BODY_H" "#include <string>"
check_anchor "$BODY_H" "class AITriggerTypeExt"
check_anchor "$BODY_H" "std::string DebugMessageDisplay_Finish;"
check_anchor "$BODY_CPP" "static void ReadRawString("
check_anchor "$BODY_CPP" 'ReadRawString(exINI, section, "DebugMessageDisplay.Finish"'

# ─── Backups ─────────────────────────────────────────────────────────────
echo "Backing up..."
cp "$BODY_H" "$BODY_H.bak"
cp "$BODY_CPP" "$BODY_CPP.bak"

# ─── Body.h modifications (single awk pass) ──────────────────────────────
echo "Modifying $BODY_H..."
awk '
BEGIN { added_vector = 0; added_structs = 0; added_fields = 0; }

# Before class AITriggerTypeExt line — insert the three structs
/^class AITriggerTypeExt/ && !added_structs {
    print ""
    print "// ============================================================================"
    print "// Debug detail collection (Priority 1 debug system)"
    print "// Filled by Check* functions when the caller wants per-index status detail."
    print "// ============================================================================"
    print "struct AIExtCheckDetail"
    print "{"
    print "    // Filled by the Check function per parallel-list index. Each string"
    print "    // is one line of prose like \"GABARR needs [1, -1], has 3 PASS\"."
    print "    std::vector<std::string> passing;"
    print "    std::vector<std::string> failing;"
    print "    std::string gate_name;"
    print "};"
    print ""
    print "// Parsed form of Debug.Detail=passing,failing settings."
    print "struct AIExtDetailMode"
    print "{"
    print "    bool show_passing = false;"
    print "    bool show_failing = false;"
    print "    bool anything() const { return show_passing || show_failing; }"
    print "};"
    print ""
    print "// Parsed form of Debug.DetailTrigger=start,cancel,finish."
    print "struct AIExtLifecycleMask"
    print "{"
    print "    bool on_start  = false;"
    print "    bool on_cancel = false;"
    print "    bool on_finish = false;"
    print "    bool anything() const { return on_start || on_cancel || on_finish; }"
    print "};"
    print ""
    added_structs = 1
}

# Emit the current line
{ print }

# After #include <string>, add #include <vector>
/^#include <string>$/ && !added_vector {
    print "#include <vector>"
    added_vector = 1
}

# After DebugMessageDisplay_Finish field, add per-gate detail fields
/std::string DebugMessageDisplay_Finish;/ && !added_fields {
    print ""
    print "        // Priority 1 debug — per-gate detail control (Detail= and DetailTrigger= sub-fields)"
    print "        AIExtDetailMode      Debug_Owner_Buildings_Detail;"
    print "        AIExtLifecycleMask   Debug_Owner_Buildings_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Enemy_Buildings_Detail;"
    print "        AIExtLifecycleMask   Debug_Enemy_Buildings_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Enemy_Units_Detail;"
    print "        AIExtLifecycleMask   Debug_Enemy_Units_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Neutral_Buildings_Detail;"
    print "        AIExtLifecycleMask   Debug_Neutral_Buildings_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Owner_Credits_Detail;"
    print "        AIExtLifecycleMask   Debug_Owner_Credits_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Enemy_Credits_Detail;"
    print "        AIExtLifecycleMask   Debug_Enemy_Credits_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Owner_Power_Detail;"
    print "        AIExtLifecycleMask   Debug_Owner_Power_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_Enemy_Power_Detail;"
    print "        AIExtLifecycleMask   Debug_Enemy_Power_DetailTrigger { false, false, true };"
    print "        AIExtDetailMode      Debug_ElapsedTime_Detail;"
    print "        AIExtLifecycleMask   Debug_ElapsedTime_DetailTrigger { false, false, true };"
    added_fields = 1
}

END {
    if (!added_vector)  { print "WARN: vector include anchor never matched" > "/dev/stderr" }
    if (!added_structs) { print "WARN: class anchor never matched" > "/dev/stderr" }
    if (!added_fields)  { print "WARN: field anchor never matched" > "/dev/stderr" }
}
' "$BODY_H" > "$BODY_H.new"
mv "$BODY_H.new" "$BODY_H"

# ─── Body.cpp modifications (single awk pass) ────────────────────────────
echo "Modifying $BODY_CPP..."
awk '
BEGIN { added_helpers = 0; added_reads = 0; }

# Insert helpers BEFORE ReadRawString function declaration
/^static void ReadRawString\(/ && !added_helpers {
    print "// Parse comma-list of passing/failing/none/never into a mode struct."
    print "static void ReadDetailMode("
    print "    INI_EX& exINI,"
    print "    const char* pSection,"
    print "    const char* pKey,"
    print "    AIExtDetailMode& out)"
    print "{"
    print "    if (!exINI.ReadString(pSection, pKey)) return;"
    print "    out.show_passing = false;"
    print "    out.show_failing = false;"
    print "    const char* v = exINI.value();"
    print "    if (!v || !*v) return;"
    print "    char buf[128];"
    print "    strncpy_s(buf, sizeof(buf), v, _TRUNCATE);"
    print "    char* ctx = nullptr;"
    print "    for (char* tok = strtok_s(buf, \",\", &ctx); tok; tok = strtok_s(nullptr, \",\", &ctx))"
    print "    {"
    print "        while (*tok == '\''  '\'' || *tok == '\''\\t'\'') tok++;"
    print "        char* end = tok + strlen(tok);"
    print "        while (end > tok && (end[-1] == '\'' '\'' || end[-1] == '\''\\t'\'')) *(--end) = 0;"
    print "        if (_stricmp(tok, \"passing\") == 0) out.show_passing = true;"
    print "        else if (_stricmp(tok, \"failing\") == 0) out.show_failing = true;"
    print "        else if (_stricmp(tok, \"none\") == 0 || _stricmp(tok, \"never\") == 0) {"
    print "            out.show_passing = false; out.show_failing = false; return;"
    print "        }"
    print "    }"
    print "}"
    print ""
    print "// Parse comma-list of start/cancel/finish/never into a lifecycle mask."
    print "static void ReadLifecycleMask("
    print "    INI_EX& exINI,"
    print "    const char* pSection,"
    print "    const char* pKey,"
    print "    AIExtLifecycleMask& out)"
    print "{"
    print "    if (!exINI.ReadString(pSection, pKey)) return;"
    print "    out.on_start = false; out.on_cancel = false; out.on_finish = false;"
    print "    const char* v = exINI.value();"
    print "    if (!v || !*v) return;"
    print "    char buf[128];"
    print "    strncpy_s(buf, sizeof(buf), v, _TRUNCATE);"
    print "    char* ctx = nullptr;"
    print "    for (char* tok = strtok_s(buf, \",\", &ctx); tok; tok = strtok_s(nullptr, \",\", &ctx))"
    print "    {"
    print "        while (*tok == '\'' '\'' || *tok == '\''\\t'\'') tok++;"
    print "        char* end = tok + strlen(tok);"
    print "        while (end > tok && (end[-1] == '\'' '\'' || end[-1] == '\''\\t'\'')) *(--end) = 0;"
    print "        if (_stricmp(tok, \"start\") == 0) out.on_start = true;"
    print "        else if (_stricmp(tok, \"cancel\") == 0) out.on_cancel = true;"
    print "        else if (_stricmp(tok, \"finish\") == 0) out.on_finish = true;"
    print "        else if (_stricmp(tok, \"never\") == 0 || _stricmp(tok, \"none\") == 0) {"
    print "            out.on_start = false; out.on_cancel = false; out.on_finish = false; return;"
    print "        }"
    print "    }"
    print "}"
    print ""
    added_helpers = 1
}

# Emit the current line
{ print }

# After the last existing DebugMessageDisplay.Finish read, add Detail reads
/ReadRawString\(exINI, section, "DebugMessageDisplay\.Finish"/ && !added_reads {
    print ""
    print "    // Priority 1 debug — Detail sub-fields (scaffolding only, behavior in follow-up)"
    print "    ReadDetailMode    (exINI, section, \"RequiredOwnerBuildings.Debug.Detail\",          Debug_Owner_Buildings_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredOwnerBuildings.Debug.DetailTrigger\",   Debug_Owner_Buildings_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredEnemyBuildings.Debug.Detail\",          Debug_Enemy_Buildings_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredEnemyBuildings.Debug.DetailTrigger\",   Debug_Enemy_Buildings_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredEnemyUnits.Debug.Detail\",              Debug_Enemy_Units_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredEnemyUnits.Debug.DetailTrigger\",       Debug_Enemy_Units_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredNeutralBuildings.Debug.Detail\",        Debug_Neutral_Buildings_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredNeutralBuildings.Debug.DetailTrigger\", Debug_Neutral_Buildings_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredOwnerCreditsMin.Debug.Detail\",         Debug_Owner_Credits_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredOwnerCreditsMin.Debug.DetailTrigger\",  Debug_Owner_Credits_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredEnemyCreditsMax.Debug.Detail\",         Debug_Enemy_Credits_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredEnemyCreditsMax.Debug.DetailTrigger\",  Debug_Enemy_Credits_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredOwnerPowerMin.Debug.Detail\",           Debug_Owner_Power_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredOwnerPowerMin.Debug.DetailTrigger\",    Debug_Owner_Power_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredEnemyPowerMax.Debug.Detail\",           Debug_Enemy_Power_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredEnemyPowerMax.Debug.DetailTrigger\",    Debug_Enemy_Power_DetailTrigger);"
    print "    ReadDetailMode    (exINI, section, \"RequiredElapsedTimeMin.Debug.Detail\",          Debug_ElapsedTime_Detail);"
    print "    ReadLifecycleMask (exINI, section, \"RequiredElapsedTimeMin.Debug.DetailTrigger\",   Debug_ElapsedTime_DetailTrigger);"
    added_reads = 1
}

END {
    if (!added_helpers) { print "WARN: helper anchor never matched" > "/dev/stderr" }
    if (!added_reads)   { print "WARN: reads anchor never matched" > "/dev/stderr" }
}
' "$BODY_CPP" > "$BODY_CPP.new"
mv "$BODY_CPP.new" "$BODY_CPP"

# ─── Fix Cancel path bug ─────────────────────────────────────────────────
# Look for the pattern where EmitDebugCancel checks _Cancel but resolves _Start.
# This is the sequence around line 1174-1176 in the current file.
# Find the SECOND occurrence of ResolveDebugText(pExt->DebugMessageDisplay_Start)
# and rewrite it as _Cancel — but only if the context confirms it's the Cancel func.
echo "Checking for Cancel-path bug..."
# Use sed to replace the SECOND occurrence
python3 << 'PYEOF'
import re
with open("src/Ext/AITriggerType/Body.cpp", "r", encoding="utf-8") as f:
    content = f.read()

# Find EmitDebugCancel function and fix the incorrect _Start reference
# Look for the function definition, then the incorrect ResolveDebugText call
pattern = re.compile(
    r'(EmitDebugCancel[^{]*\{[^}]*?ResolveDebugText\(pExt->DebugMessageDisplay_)Start(\))',
    re.DOTALL
)

new_content, n = pattern.subn(r'\1Cancel\2', content)
if n == 1:
    with open("src/Ext/AITriggerType/Body.cpp", "w", encoding="utf-8") as f:
        f.write(new_content)
    print(f"  ✓ Fixed Cancel path bug (1 replacement)")
elif n == 0:
    print(f"  (no Cancel-path bug found — may have been fixed already)")
else:
    print(f"  WARN: Unexpected match count {n} — investigate manually")
PYEOF

# ─── Verify ──────────────────────────────────────────────────────────────
echo ""
echo "─── Verification ─────────────────────────────────────────────"
echo "Body.h now contains:"
grep -c "AIExtCheckDetail\|AIExtDetailMode\|AIExtLifecycleMask" "$BODY_H"
echo "matches (should be > 20 — struct refs + field refs)"
echo ""
echo "Body.cpp now contains:"
grep -c "ReadDetailMode\|ReadLifecycleMask" "$BODY_CPP"
echo "matches (should be > 30 — helper defs + Read calls)"
echo ""
echo "Cancel path fix:"
grep "ResolveDebugText(pExt->DebugMessageDisplay_Cancel" "$BODY_CPP" || echo "  NOT PRESENT — manual check needed"

echo ""
echo "─── Done ─────────────────────────────────────────────────────"
echo "Backups saved to $BODY_H.bak and $BODY_CPP.bak"
echo ""
echo "Next steps:"
echo "  git diff src/Ext/AITriggerType/Body.h | head -40"
echo "  git diff src/Ext/AITriggerType/Body.cpp | head -60"
echo "  # If diffs look right:"
echo "  git add -A"
echo "  git commit -m 'wip(debug): Priority 1 scaffolding — structs, fields, helpers'"
echo "  git push origin feature/debug-messages"
echo "  gh workflow run 'Build AITriggerTypeExt' --ref feature/debug-messages"
echo "  gh run watch | tail -15"
