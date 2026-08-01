#pragma once

/*
 * AITriggerTypeExt - Body.h
 *
 * Extends AITriggerTypeClass with a comprehensive per-trigger prerequisite
 * system declared in a sidecar INI section:
 *
 *   [TriggerID.AIExt]
 *
 * All fields are optional.  Omitting a field entirely means that check is
 * skipped (no constraint).  Each field group covers one of four house roles:
 *   Owner   — the AI house that owns this trigger (CallingHouse)
 *   Enemy   — the selected target house (see TargetHouseMode)
 *   Allies  — houses allied with Owner but not Owner itself
 *   Neutral — the neutral/civilian house
 *
 * PARALLEL LIST CONVENTION
 * Building and unit checks use three parallel comma-separated lists:
 *   RequiredOwnerBuildings=GABARR,GAPILE,GAWEAP
 *   RequiredOwnerBuildingsMin=1,0,2       ; index-matched minimums (default 0)
 *   RequiredOwnerBuildingsMax=-1,3,-1     ; index-matched maximums (-1 = uncapped)
 *
 * Missing trailing Min values default to 0.
 * Missing trailing Max values default to -1 (uncapped).
 * Negative Min values are clamped to 0 at read time.
 * -1 on Max means no upper bound.
 *
 * SUPERWEAPON PARALLEL LIST CONVENTION
 * Superweapon checks use SW type IDs and parallel Ready window lists:
 *   RequiredOwnerSuperWeapons=NUKE,IRON
 *   RequiredOwnerSuperWeaponsReadyMin=0,0     ; frames remaining >= this
 *   RequiredOwnerSuperWeaponsReadyMax=-1,300  ; frames remaining <= this
 * ReadyMin=0 + ReadyMax=0 means "must be fully charged and ready to fire".
 * ReadyMin=0 + ReadyMax=300 means "ready or within 300 frames of ready".
 * -1 is uncapped on either end.
 *
 * SCALAR FIELDS (Nullable<int>)
 * Credits, Power, PowerOutput, TechLevel, ElapsedTime — single values.
 * Each has a Min and Max variant.  -1 = uncapped.  Omit key = no check.
 *
 * CONFIRMED ADDRESSES (from YRpp phobos-dev + DCoder reconstructed source)
 *   AITriggerTypeClass::CTOR            0x41E350
 *   AITriggerTypeClass::LoadFromINIList 0x41F2E0
 *   AITriggerTypeClass::ConditionMet    0x41E720
 *   AITriggerTypeClass::Array           0xA8B200
 *
 * ADDRESSES REQUIRING DISASSEMBLY (see DISASM_GUIDE.md)
 *   DTOR call site
 *   IPersistStream::Load prefix/suffix
 *   IPersistStream::Save prefix/suffix
 *   ConditionMet call site inside FindEligibleAITeams
 *   Per-item LoadFromINI call site inside LoadFromINIList
 */

#include <AITriggerTypeClass.h>
#include <BuildingTypeClass.h>
#include <InfantryTypeClass.h>
#include <UnitTypeClass.h>
#include <AircraftTypeClass.h>
#include <TechnoTypeClass.h>
#include <SuperWeaponTypeClass.h>
#include <SuperClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>
#include <string>
#include <vector>
#include <map>
#include <Utilities/Container.h>
#include <Utilities/TemplateDef.h>

// ============================================================================
// TargetHouseMode — controls which enemy house the enemy checks apply to
// ============================================================================
enum class AIExtTargetHouseMode : int
{
    Current        = 0, // Use engine-provided TargetHouse (default, vanilla behaviour)
    Any            = 1, // Any enemy house satisfies all enemy checks
    All            = 2, // All enemy houses must satisfy all enemy checks
    MostBuildings  = 3, // Enemy with most of the listed RequiredEnemyBuildings types
    LeastBuildings = 4, // Enemy with fewest of the listed RequiredEnemyBuildings types
    MostUnits      = 5, // Enemy with most of the listed RequiredEnemyUnits types
    LeastUnits     = 6, // Enemy with fewest of the listed RequiredEnemyUnits types
};

// ============================================================================
// AIExtAlliesMode — controls how the allies checks are evaluated
// ============================================================================
enum class AIExtAlliesMode : int
{
    Any = 0, // Any single allied house satisfying the checks is enough (default)
    All = 1, // All allied houses must satisfy the checks
};

// ============================================================================
// TypeCountGate — parallel lists of types with per-index min/max counts
//
// Types[i] must be owned between Min[i] and Max[i] (inclusive).
// Min defaults to 0, Max defaults to -1 (uncapped).
// Negative min is clamped to 0 on read.
// ============================================================================
template <typename T>
struct TypeCountGate
{
    std::vector<T*>  Types;
    std::vector<int> Min;   // parallel with Types, default 0
    std::vector<int> Max;   // parallel with Types, default -1

    bool empty() const { return Types.empty(); }

    // Returns the effective min for index i (with bounds check + clamp)
    int GetMin(size_t i) const
    {
        if (i < Min.size())
            return std::max(0, Min[i]);
        return 0;
    }

    // Returns the effective max for index i (with bounds check)
    int GetMax(size_t i) const
    {
        if (i < Max.size())
            return Max[i];
        return -1; // uncapped
    }

    // Evaluate count against this gate entry.
    // Returns true if count satisfies [Min[i], Max[i]].
    bool CheckCount(size_t i, int count) const
    {
        int mn = GetMin(i);
        int mx = GetMax(i);
        if (count < mn)  return false;
        if (mx != -1 && count > mx) return false;
        return true;
    }
};

// ============================================================================
// SWReadyGate — parallel lists of SW types with per-index frames-remaining
//               window [ReadyMin, ReadyMax]
//
// ReadyMin=0 + ReadyMax=0  → must be fully charged (GetTimeLeft()==0)
// ReadyMin=0 + ReadyMax=N  → charged or within N frames of charging
// -1 is uncapped on either end.
// ============================================================================
struct SWReadyGate
{
    std::vector<SuperWeaponTypeClass*> Types;
    std::vector<int> ReadyMin; // frames remaining >= this (default 0)
    std::vector<int> ReadyMax; // frames remaining <= this (default -1, uncapped)

    bool empty() const { return Types.empty(); }

    int GetReadyMin(size_t i) const
    {
        if (i < ReadyMin.size())
            return std::max(0, ReadyMin[i]);
        return 0;
    }

    int GetReadyMax(size_t i) const
    {
        if (i < ReadyMax.size())
            return ReadyMax[i];
        return -1;
    }

    bool CheckFramesLeft(size_t i, int framesLeft) const
    {
        int mn = GetReadyMin(i);
        int mx = GetReadyMax(i);
        if (framesLeft < mn)  return false;
        if (mx != -1 && framesLeft > mx) return false;
        return true;
    }
};

// ============================================================================
// AITriggerTypeExt
// ============================================================================

// ============================================================================
// Debug detail collection (Priority 1 debug system)
// Filled by Check* functions when the caller wants per-index status detail.
// ============================================================================
// One evaluated gate entry: which gate root, which type (empty for scalar
// gates), the actual value, the [min,max] window (-1 max = uncapped), and
// whether it passed. Formatting (log columns, HUD value) is done at emit time.
struct AIExtCheckLine
{
    std::string root;     // e.g. "RequiredOwnerBuildings"
    std::string type_id;  // TechnoType/Building ID; empty for scalar gates
    int  actual = 0;
    int  min_v  = 0;
    int  max_v  = -1;
    bool passed = false;
};

struct AIExtCheckDetail
{
    std::vector<AIExtCheckLine> lines;
    void clear() { lines.clear(); }
};

// Parsed form of Debug.Detail=passing,failing settings.
struct AIExtDetailMode
{
    bool show_passing = false;
    bool show_failing = false;
    bool anything() const { return show_passing || show_failing; }
};

// Parsed form of Debug.DetailTrigger=start,cancel,finish.
struct AIExtLifecycleMask
{
    bool on_start  = false;
    bool on_cancel = false;
    bool on_finish = false;
    bool anything() const { return on_start || on_cancel || on_finish; }
};

// Parsed form of the wave-generator tool's per-gate debug "quad", e.g.
//   RequiredOwnerBuildings.Debug.MessageDisplay=NOSTR:Owner buildings:
//   RequiredOwnerBuildings.Debug.ValueDisplay=yes
//   RequiredOwnerBuildings.Debug.LogMessage=Owner buildings:
//   RequiredOwnerBuildings.Debug.LogWrite=yes
//   RequiredOwnerBuildings.Debug.DetailsDisplay=yes
//   RequiredOwnerBuildings.Debug.DetailsTypes=Type, Minimum, Maximum, Current
// Keyed in ExtData::GateDebug by the gate root (text before ".Debug.").
struct AIExtGateDebug
{
    std::string message_display;   // HUD text prefix (NOSTR:/STT:/bare CSF key)
    bool        value_display  = false;
    std::string log_message;       // debug.log text prefix
    bool        log_write      = false; // auto pass/fail detail line(s) to log
    bool        details_display = false;
    std::string details_types;     // e.g. "Type, Minimum, Maximum, Current"
};

class AITriggerTypeExt
{
public:
    using base_type = AITriggerTypeClass;

    // Canary: 0xA7A7A7A7 chosen to avoid conflict with:
    //   PR #2119 AITriggerTypeExt: 0x9B9B9B9B
    //   WarheadTypeExt:            0x22222222
    //   TechnoTypeExt:             0x11111111
    //   RadSiteExt:                0x88446622
    //   HouseTypeExt:              0xAFFEAFFE
    static constexpr DWORD Canary = 0xA7A7A7A7;

    // No ExtPointerOffset — AITriggerTypeClass has no spare pointer field.
    // Container<AITriggerTypeExt> will use the unordered_map path automatically.

    // =========================================================================
    // ExtData
    // =========================================================================
    class ExtData final : public Extension<AITriggerTypeClass>
    {
    public:

        // -----------------------------------------------------------------------
        // TARGET HOUSE MODE
        // -----------------------------------------------------------------------
        Valueable<AIExtTargetHouseMode> TargetHouseMode;

        // -----------------------------------------------------------------------
        // ALLIES MODE
        // -----------------------------------------------------------------------
        Valueable<AIExtAlliesMode> AlliesMode;

        // -----------------------------------------------------------------------
        // OWNER — buildings
        // [TriggerID.AIExt]
        // RequiredOwnerBuildings=GABARR,GAPILE
        // RequiredOwnerBuildingsMin=1,1
        // RequiredOwnerBuildingsMax=-1,-1
        // -----------------------------------------------------------------------
        TypeCountGate<BuildingTypeClass> OwnerBuildings;

        // -----------------------------------------------------------------------
        // OWNER — units (infantry, vehicles, aircraft — any TechnoType)
        // RequiredOwnerUnits=MTNK,E1
        // RequiredOwnerUnitsMin=3,5
        // RequiredOwnerUnitsMax=-1,20
        // -----------------------------------------------------------------------
        TypeCountGate<TechnoTypeClass> OwnerUnits;

        // -----------------------------------------------------------------------
        // OWNER — superweapons
        // RequiredOwnerSuperWeapons=NUKE,IRON
        // RequiredOwnerSuperWeaponsReadyMin=0,0
        // RequiredOwnerSuperWeaponsReadyMax=0,-1
        // -----------------------------------------------------------------------
        SWReadyGate OwnerSuperWeapons;

        // -----------------------------------------------------------------------
        // OWNER — scalar gates
        // RequiredOwnerCreditsMin=1000     ; Balance >= 1000
        // RequiredOwnerCreditsMax=-1       ; no upper cap
        // RequiredOwnerPowerMin=0          ; net (Output-Drain) >= 0
        // RequiredOwnerPowerMax=-1
        // RequiredOwnerPowerOutputMin=500  ; raw output >= 500
        // RequiredOwnerPowerOutputMax=-1
        // RequiredOwnerTechLevelMin=3
        // RequiredOwnerTechLevelMax=-1
        // -----------------------------------------------------------------------
        Nullable<int> OwnerCreditsMin;
        Nullable<int> OwnerCreditsMax;
        Nullable<int> OwnerPowerMin;
        Nullable<int> OwnerPowerMax;
        Nullable<int> OwnerPowerOutputMin;
        Nullable<int> OwnerPowerOutputMax;
        Nullable<int> OwnerTechLevelMin;
        Nullable<int> OwnerTechLevelMax;
        // Live combat DPS of everything this house currently owns (see
        // ComputeHouseDPS). Priority-2 "DPS Check". Lock = scope bitmask:
        // 1=AA (anti-air), 2=AG (anti-ground), 0=all. RequiredOwnerDPSLock=AA,AG
        Nullable<int> OwnerDPSMin;
        Nullable<int> OwnerDPSMax;
        int           OwnerDPSLock = 0;
        // Restrict the DPS sum to these unit types only (empty = all units).
        // RequiredOwnerDPSTypes=HTNK,MTNK  → "owner tank DPS"
        std::vector<TechnoTypeClass*> OwnerDPSTypes;
        // Weight each weapon by its warhead's Verses vs this armor type
        // (effective DPS vs that armor). -1 = raw (no weighting).
        // RequiredOwnerDPSArmor=heavy
        int OwnerDPSArmor = -1;

        // -----------------------------------------------------------------------
        // ENEMY — buildings
        // -----------------------------------------------------------------------
        TypeCountGate<BuildingTypeClass> EnemyBuildings;

        // -----------------------------------------------------------------------
        // ENEMY — units
        // -----------------------------------------------------------------------
        TypeCountGate<TechnoTypeClass> EnemyUnits;

        // -----------------------------------------------------------------------
        // ENEMY — superweapons
        // -----------------------------------------------------------------------
        SWReadyGate EnemySuperWeapons;

        // -----------------------------------------------------------------------
        // ENEMY — scalar gates
        // -----------------------------------------------------------------------
        Nullable<int> EnemyCreditsMin;
        Nullable<int> EnemyCreditsMax;
        Nullable<int> EnemyPowerMin;
        Nullable<int> EnemyPowerMax;
        Nullable<int> EnemyPowerOutputMin;
        Nullable<int> EnemyPowerOutputMax;
        Nullable<int> EnemyTechLevelMin;
        Nullable<int> EnemyTechLevelMax;
        Nullable<int> EnemyDPSMin;
        Nullable<int> EnemyDPSMax;
        int           EnemyDPSLock = 0;
        std::vector<TechnoTypeClass*> EnemyDPSTypes;
        int           EnemyDPSArmor = -1;

        // Enemy weapon-range threat: the longest weapon range (in cells) among
        // the enemy's owned damaging weapons, scoped by Lock (AA/AG) and Types.
        // "Don't rush when the enemy outranges me." First step toward the
        // spatial range-avoidance work (same per-weapon Range read).
        // RequiredEnemyMaxRangeMax=6  → enemy's longest weapon reaches <= 6 cells
        Nullable<int> EnemyMaxRangeMin;
        Nullable<int> EnemyMaxRangeMax;
        int           EnemyMaxRangeLock = 0;
        std::vector<TechnoTypeClass*> EnemyMaxRangeTypes;

        // "Outranged?" check — THIS trigger's own team (Team1/Team2 taskforce)
        // longest weapon range as a percentage of the enemy's longest
        // (100 = matched, <100 = my team is outranged, 150 = I outrange 1.5x).
        // Reads the trigger's taskforce, so it's team-accurate (a Dog team and
        // a Prism team in the same house get different answers).
        // RequiredTeamRangeRatioMin=100  → don't dispatch if my team is outranged.
        Nullable<int> TeamRangeRatioMin;
        Nullable<int> TeamRangeRatioMax;
        int           TeamRangeRatioLock = 0;

        // Comparative DPS: owner's DPS as a ratio of the (resolved) enemy's,
        // expressed as a percentage (200 = owner has 2.0x the enemy's DPS).
        // "Attack only when I out-gun them." -1 max = uncapped. Uses the shared
        // RequiredDPSRatioLock scope on both sides.
        Nullable<int> DPSRatioMin;
        Nullable<int> DPSRatioMax;
        int           DPSRatioLock = 0;

        // Base separation: straight-line distance (in cells) between the owner's
        // base center and the resolved enemy's base center. Lets a trigger gate
        // on how close the two bases are — "far apart (100 cells) → big set-piece
        // waves are fine; close (20 cells) → prefer fast guerilla harassment."
        // -1 max = uncapped. A zero/absent enemy base center yields distance 0.
        // RequiredBaseDistanceMin=60 → only fire when the enemy base is 60+ cells away.
        Nullable<int> BaseDistanceMin;
        Nullable<int> BaseDistanceMax;

        // -----------------------------------------------------------------------
        // ALLIES — buildings
        // -----------------------------------------------------------------------
        TypeCountGate<BuildingTypeClass> AlliesBuildings;

        // -----------------------------------------------------------------------
        // ALLIES — units
        // -----------------------------------------------------------------------
        TypeCountGate<TechnoTypeClass> AlliesUnits;

        // -----------------------------------------------------------------------
        // ALLIES — superweapons
        // -----------------------------------------------------------------------
        SWReadyGate AlliesSuperWeapons;

        // -----------------------------------------------------------------------
        // ALLIES — scalar gates
        // -----------------------------------------------------------------------
        Nullable<int> AlliesCreditsMin;
        Nullable<int> AlliesCreditsMax;
        Nullable<int> AlliesPowerMin;
        Nullable<int> AlliesPowerMax;
        Nullable<int> AlliesPowerOutputMin;
        Nullable<int> AlliesPowerOutputMax;
        Nullable<int> AlliesTechLevelMin;
        Nullable<int> AlliesTechLevelMax;

        // -----------------------------------------------------------------------
        // NEUTRAL — buildings
        // -----------------------------------------------------------------------
        TypeCountGate<BuildingTypeClass> NeutralBuildings;

        // -----------------------------------------------------------------------
        // NEUTRAL — units
        // -----------------------------------------------------------------------
        TypeCountGate<TechnoTypeClass> NeutralUnits;

        // -----------------------------------------------------------------------
        // NEUTRAL — superweapons
        // -----------------------------------------------------------------------
        SWReadyGate NeutralSuperWeapons;

        // -----------------------------------------------------------------------
        // NEUTRAL — scalar gates
        // -----------------------------------------------------------------------
        Nullable<int> NeutralCreditsMin;
        Nullable<int> NeutralCreditsMax;
        Nullable<int> NeutralPowerMin;
        Nullable<int> NeutralPowerMax;
        Nullable<int> NeutralPowerOutputMin;
        Nullable<int> NeutralPowerOutputMax;
        Nullable<int> NeutralTechLevelMin;
        Nullable<int> NeutralTechLevelMax;

        // -----------------------------------------------------------------------
        // GLOBAL — elapsed game time (frames since scenario start)
        // RequiredElapsedTimeMin=1500   ; don't fire before frame 1500
        // RequiredElapsedTimeMax=9000   ; stop being eligible after frame 9000
        // 1 second ≈ 15 frames at normal speed.
        // -----------------------------------------------------------------------
        Nullable<int> ElapsedTimeMin;
        Nullable<int> ElapsedTimeMax;
        // -----------------------------------------------------------------------
        // DEBUG — overlay (CSF-localized message shown in game HUD)
        // [MyTrigger.AIExt]
        //   DebugMessageDisplay.Start=STT:AI_RUSH_STARTED
        //   DebugMessageDisplay.Cancel=STT:AI_RUSH_CANCELLED
        //   DebugMessageDisplay.Finish=STT:AI_RUSH_DEPLOYED
        // Shown when [Debug].DisplayAIWaveMessages is 'yes' or 'both'.
        // -----------------------------------------------------------------------
        std::string DebugMessageDisplay_Consider;
        std::string DebugMessageDisplay_Cancel;
        std::string DebugMessageDisplay_Finish;
        // Lifecycle events with confirmed Ghidra hooks (see Hooks.cpp):
        //   Start     — trigger won the weighted draw and is dispatching its team
        //   Destroyed — team wiped out before completing its script (failure)
        //   Deleted   — team completed its script successfully
        std::string DebugMessageDisplay_Start;
        std::string DebugMessageDisplay_Destroyed;
        std::string DebugMessageDisplay_Deleted;
        // Reject — trigger passed ConditionMet but lost the weighted draw.
        // Fires once per losing trigger per selection, so it's opt-in only
        // (not auto-emitted by the tool) to avoid log spam.
        std::string DebugMessageDisplay_Reject;
        // Selected — WON the weighted draw this cycle. Upstream of Start:
        // the AI picked this trigger but the team isn't confirmed built yet
        // (can be skipped by team caps / production gating). Fires per draw win.
        std::string DebugMessageDisplay_Selected;

        // Per-gate debug quads from the wave-generator tool, keyed by gate root
        // (e.g. "RequiredOwnerBuildings"). Populated generically by scanning the
        // [Trigger.AIExt] section for "<root>.Debug.<subkey>" keys.
        std::map<std::string, AIExtGateDebug> GateDebug;

        // -----------------------------------------------------------------------
        // PER-TRIGGER WEIGHT DELTA (Priority 6) — replace vanilla's global
        // AITriggerSuccessWeightDelta / AITriggerFailureWeightDelta for THIS
        // trigger only. Omit = use the global default. The track-record scaling
        // and [Min,Max] clamp still apply (handled by vanilla).
        //   SuccessWeightDelta=30      ; this trigger gains 30 on success (not global 5)
        //   FailureWeightDelta=-40     ; loses 40 on failure (not global -20)
        // -----------------------------------------------------------------------
        Nullable<int> SuccessWeightDelta;
        Nullable<int> FailureWeightDelta;

        // -----------------------------------------------------------------------
        // WEIGHT CASCADES (Priority 6) — when THIS trigger's team succeeds or
        // fails, adjust the Weight_Current of OTHER named triggers. Lets modders
        // encode "if this aerial rush failed, penalize the other aerial rushes".
        //   SuccessCascadeTargets=TrigA,TrigB
        //   SuccessCascadeTargets.Delta=10,5   ; positional; single value = all
        //   FailureCascadeTargets=TrigC
        //   FailureCascadeTargets.Delta=-15
        // Target weights are clamped to each target's [Weight_Minimum, Maximum],
        // the same bounds vanilla RegisterSuccess/Failure use.
        // -----------------------------------------------------------------------
        std::vector<std::string> SuccessCascadeTargets;
        std::vector<int>         SuccessCascadeDeltas;
        std::vector<std::string> FailureCascadeTargets;
        std::vector<int>         FailureCascadeDeltas;

        // mutable check report populated by EvaluateAndReport()
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

        // Orchestrator: walks all set gates, populates LastCheckReport with
        // one "root TypeID(actual):min,max" (or "root(actual):min,max") line
        // per entry, categorized into passing / failing.
        void EvaluateAndReport(HouseClass* pOwner, HouseClass* pEnemy) const;

        // Scan the [Trigger.AIExt] section for "<root>.Debug.<subkey>" keys and
        // populate GateDebug. Called from LoadFromINIFile.
        void ParseGateDebug(CCINIClass* pINI, const char* section);

        // Emit the tool's per-gate debug quad for a lifecycle event.
        //   doLog   — write LogMessage headers + per-entry PASS/FAIL lines
        //             (columns per each gate's DetailsTypes).
        //   overlay — on Cancel only, show each FAILING gate's MessageDisplay
        //             on the HUD (+ its value when ValueDisplay=yes). Consider
        //             is skipped for HUD — it fires every tick and would flood.
        // label is the bracket tag ("Consider"/"Cancel").
        void EmitGateDebug(
            AITriggerTypeClass* pThis, HouseClass* pOwner, HouseClass* pEnemy,
            const char* label, bool overlay, bool doLog) const;


        // -----------------------------------------------------------------------
        // DEBUG — log file (raw ASCII string; written to debug.log)
        //   DebugLog.Start=TT_TANK_RUSH started building
        //   DebugLog.Cancel=TT_TANK_RUSH vetoed by AIExt
        //   DebugLog.Finish=TT_TANK_RUSH deployed team
        // Written when [Debug].DisplayAIWaveMessages is 'log' or 'both'.
        // -----------------------------------------------------------------------
        std::string DebugLog_Consider;
        std::string DebugLog_Cancel;
        std::string DebugLog_Finish;
        std::string DebugLog_Start;
        std::string DebugLog_Destroyed;
        std::string DebugLog_Deleted;
        std::string DebugLog_Reject;
        std::string DebugLog_Selected;

        // -----------------------------------------------------------------------
        // DEBUG — per-condition auto-verbose log flags
        //   RequiredOwnerBuildings.DebugLog=yes
        // Emits detailed log lines showing each entry, range, actual value,
        // and pass/fail status. See EmitConditionLog in Body.cpp.
        // -----------------------------------------------------------------------
        bool DebugLog_OwnerBuildings      = false;
        bool DebugLog_OwnerUnits          = false;
        bool DebugLog_OwnerSuperWeapons   = false;
        bool DebugLog_OwnerCredits        = false;
        bool DebugLog_OwnerPower          = false;
        bool DebugLog_OwnerPowerOutput    = false;
        bool DebugLog_OwnerTechLevel      = false;

        bool DebugLog_EnemyBuildings      = false;
        bool DebugLog_EnemyUnits          = false;
        bool DebugLog_EnemySuperWeapons   = false;
        bool DebugLog_EnemyCredits        = false;
        bool DebugLog_EnemyPower          = false;
        bool DebugLog_EnemyPowerOutput    = false;
        bool DebugLog_EnemyTechLevel      = false;

        bool DebugLog_AlliesBuildings     = false;
        bool DebugLog_AlliesUnits         = false;
        bool DebugLog_AlliesSuperWeapons  = false;
        bool DebugLog_AlliesCredits       = false;
        bool DebugLog_AlliesPower         = false;
        bool DebugLog_AlliesPowerOutput   = false;
        bool DebugLog_AlliesTechLevel     = false;

        bool DebugLog_NeutralBuildings    = false;
        bool DebugLog_NeutralUnits        = false;
        bool DebugLog_NeutralSuperWeapons = false;
        bool DebugLog_NeutralCredits      = false;
        bool DebugLog_NeutralPower        = false;
        bool DebugLog_NeutralPowerOutput  = false;
        bool DebugLog_NeutralTechLevel    = false;

        bool DebugLog_ElapsedTime         = false;

        // -----------------------------------------------------------------------
        // Constructor
        // -----------------------------------------------------------------------
        ExtData(AITriggerTypeClass* OwnerObject)
            : Extension<AITriggerTypeClass>(OwnerObject)
            , TargetHouseMode { AIExtTargetHouseMode::Current }
            , AlliesMode      { AIExtAlliesMode::Any }
            , OwnerBuildings  {}
            , OwnerUnits      {}
            , OwnerSuperWeapons {}
            , OwnerCreditsMin {}
            , OwnerCreditsMax {}
            , OwnerPowerMin   {}
            , OwnerPowerMax   {}
            , OwnerPowerOutputMin {}
            , OwnerPowerOutputMax {}
            , OwnerTechLevelMin {}
            , OwnerTechLevelMax {}
            , EnemyBuildings  {}
            , EnemyUnits      {}
            , EnemySuperWeapons {}
            , EnemyCreditsMin {}
            , EnemyCreditsMax {}
            , EnemyPowerMin   {}
            , EnemyPowerMax   {}
            , EnemyPowerOutputMin {}
            , EnemyPowerOutputMax {}
            , EnemyTechLevelMin {}
            , EnemyTechLevelMax {}
            , AlliesBuildings {}
            , AlliesUnits     {}
            , AlliesSuperWeapons {}
            , AlliesCreditsMin {}
            , AlliesCreditsMax {}
            , AlliesPowerMin  {}
            , AlliesPowerMax  {}
            , AlliesPowerOutputMin {}
            , AlliesPowerOutputMax {}
            , AlliesTechLevelMin {}
            , AlliesTechLevelMax {}
            , NeutralBuildings {}
            , NeutralUnits    {}
            , NeutralSuperWeapons {}
            , NeutralCreditsMin {}
            , NeutralCreditsMax {}
            , NeutralPowerMin {}
            , NeutralPowerMax {}
            , NeutralPowerOutputMin {}
            , NeutralPowerOutputMax {}
            , NeutralTechLevelMin {}
            , NeutralTechLevelMax {}
            , ElapsedTimeMin  {}
            , ElapsedTimeMax  {}
            , DebugMessageDisplay_Consider  {}
            , DebugMessageDisplay_Cancel {}
            , DebugMessageDisplay_Finish {}
            , DebugMessageDisplay_Start {}
            , DebugMessageDisplay_Destroyed {}
            , DebugMessageDisplay_Deleted {}
            , DebugMessageDisplay_Reject {}
            , DebugMessageDisplay_Selected {}
            , DebugLog_Consider  {}
            , DebugLog_Cancel {}
            , DebugLog_Finish {}
            , DebugLog_Start {}
            , DebugLog_Destroyed {}
            , DebugLog_Deleted {}
            , DebugLog_Reject {}
            , DebugLog_Selected {}
        { }

        virtual ~ExtData() = default;

        // -----------------------------------------------------------------------
        // Main gate — call this after engine ConditionMet returns true.
        // Returns false to veto the trigger.
        // -----------------------------------------------------------------------
        bool ExtraPrerequisitesMet(
            HouseClass* pCallingHouse,
            HouseClass* pTargetHouse) const;

        // -----------------------------------------------------------------------
        // Phobos Extension interface
        // -----------------------------------------------------------------------
        virtual void LoadFromINIFile(CCINIClass* pINI) override;
        virtual void Initialize() override;
        virtual void InvalidatePointer(void* ptr, bool bRemoved) override;
        virtual void LoadFromStream(PhobosStreamReader& Stm) override;
        virtual void SaveToStream(PhobosStreamWriter& Stm) override;

    private:
        template <typename T>
        void Serialize(T& Stm);

        // Internal check helpers
        bool CheckOwner (HouseClass* pHouse) const;
        bool CheckEnemy (HouseClass* pCallingHouse, HouseClass* pTargetHouse) const;
        bool CheckDPSRatio(HouseClass* pCallingHouse, HouseClass* pTargetHouse) const;
        bool CheckTeamRangeRatio(HouseClass* pCallingHouse, HouseClass* pTargetHouse) const;
        bool CheckBaseDistance(HouseClass* pCallingHouse, HouseClass* pTargetHouse) const;
        bool CheckAllies(HouseClass* pCallingHouse) const;
        bool CheckNeutral() const;
        bool CheckElapsedTime() const;

        // Enemy house resolution per TargetHouseMode
        HouseClass* ResolveTargetHouse(
            HouseClass* pCallingHouse,
            HouseClass* pEngineTarget) const;

        // Per-house scalar / list checkers
        static bool CheckHouseBuildings(
            HouseClass* pHouse,
            const TypeCountGate<BuildingTypeClass>& gate);

        static bool CheckHouseUnits(
            HouseClass* pHouse,
            const TypeCountGate<TechnoTypeClass>& gate);

        static bool CheckHouseSuperWeapons(
            HouseClass* pHouse,
            const SWReadyGate& gate);

        static bool CheckHouseCredits(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max);

        static bool CheckHousePower(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max);

        static bool CheckHousePowerOutput(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max);

        static bool CheckHouseTechLevel(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max);

        // Sum the raw DPS of every combat object the house currently owns
        // (weapons 0 and 1, damage>0). lockMask filters by scope: 1=AA, 2=AG,
        // 0=all. `types` (if non-null and non-empty) restricts to those unit
        // types. Raw DPS per weapon = Damage * Burst / (ROF / 10).
        // The unfiltered case is cached per-house-per-scope per-frame.
        //   armorIndex (0-10, -1 = raw) multiplies each weapon's DPS by its
        //   warhead's Verses vs that armor (effective DPS vs the armor).
        static double ComputeHouseDPS(HouseClass* pHouse, int lockMask,
            const std::vector<TechnoTypeClass*>* types, int armorIndex);
        static bool CheckHouseDPS(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max,
            int lockMask,
            const std::vector<TechnoTypeClass*>* types,
            int armorIndex);

        // Longest weapon range (in cells) among the house's owned damaging
        // weapons, scoped by lockMask + types. Cached per-house-per-scope/frame.
        static int ComputeHouseMaxRange(HouseClass* pHouse, int lockMask,
            const std::vector<TechnoTypeClass*>* types);
        static bool CheckHouseMaxRange(
            HouseClass* pHouse,
            const Nullable<int>& min,
            const Nullable<int>& max,
            int lockMask,
            const std::vector<TechnoTypeClass*>* types);

        // Longest weapon range (cells) among the trigger's own Team1/Team2
        // taskforce unit types, scoped by lockMask.
        static int ComputeTriggerTeamMaxRange(AITriggerTypeClass* pTrigger, int lockMask);

        // Count total of a TechnoType across all class arrays
        static int CountOwnedTechnoType(
            HouseClass* pHouse,
            TechnoTypeClass* pType);

        // Sum the count of listed RequiredEnemyBuildings types on a house
        // (used for Most/Least selection)
        int SumBuildingCount(HouseClass* pHouse) const;
        int SumUnitCount(HouseClass* pHouse) const;
    };

    // =========================================================================
    // ExtContainer
    // =========================================================================
    class ExtContainer final : public Container<AITriggerTypeExt>
    {
    public:
        ExtContainer();
        ~ExtContainer();
    };

    static ExtContainer ExtMap;
    static bool LoadGlobals(PhobosStreamReader& Stm);
    static bool SaveGlobals(PhobosStreamWriter& Stm);

    // ============================================================================
    // DEBUG DISPLAY MODE
    // Global toggle read from [Debug].DisplayAIWaveMessages in rulesmd.ini.
    // Off     = no debug output
    // Overlay = show CSF messages in game HUD only
    // Log     = write to debug.log only
    // Both    = both
    // ============================================================================
    enum class DebugDisplayMode
    {
        Off     = 0,
        Overlay = 1,
        Log     = 2,
        Both    = 3,
    };

    static DebugDisplayMode GetDebugMode();

    // Lifecycle debug emitters — called from Hooks.cpp gate hook
    static void EmitDebugConsider (ExtData* pExt, AITriggerTypeClass* pThis,
        HouseClass* pOwner, HouseClass* pEnemy);
    static void EmitDebugCancel(ExtData* pExt, AITriggerTypeClass* pThis,
        HouseClass* pOwner, HouseClass* pEnemy);
    static void EmitDebugFinish(ExtData* pExt, AITriggerTypeClass* pThis);
    static void EmitDebugStart(ExtData* pExt, AITriggerTypeClass* pThis);
    static void EmitDebugDestroyed(ExtData* pExt, AITriggerTypeClass* pThis);
    static void EmitDebugDeleted(ExtData* pExt, AITriggerTypeClass* pThis);
    static void EmitDebugReject(ExtData* pExt, AITriggerTypeClass* pThis);
    static void EmitDebugSelected(ExtData* pExt, AITriggerTypeClass* pThis);

    // Weight adjustments — called from the RegisterSuccess (success=true) /
    // RegisterFailure (success=false) hooks, BEFORE vanilla runs.
    // Self-delta replaces vanilla's global delta for this trigger; cascades
    // adjust other named triggers' weights.
    static void ApplyWeightSelfDelta(ExtData* pExt, AITriggerTypeClass* pThis, bool success);
    static void ApplyWeightCascades(ExtData* pExt, AITriggerTypeClass* pThis, bool success);
};
