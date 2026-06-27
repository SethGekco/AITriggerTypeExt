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
};
