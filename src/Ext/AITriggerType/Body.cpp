/*
 * AITriggerTypeExt - Body.cpp
 */

#include "Body.h"

#include <BuildingClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <AircraftClass.h>
#include <Utilities/Macro.h>
#include <Utilities/Debug.h>
#include <MessageListClass.h>
#include <StringTable.h>
#include <RulesClass.h>
#include <WeaponTypeClass.h>
#include <BulletTypeClass.h>
#include <WarheadTypeClass.h>

#include <deque>

// ============================================================================
// Static member definitions
// ============================================================================

AITriggerTypeExt::ExtContainer AITriggerTypeExt::ExtMap;

// ============================================================================
// Helpers: read a parallel int list into a vector
// ============================================================================

static void ReadIntList(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::vector<int>& out,
    int defaultValue)
{
    if (exINI.ReadString(pSection, pKey))
    {
        out.clear();
        char* raw = exINI.value();
        char* ctx = nullptr;
        for (char* tok = strtok_s(raw, ",", &ctx);
             tok;
             tok = strtok_s(nullptr, ",", &ctx))
        {
            char* end = nullptr;
            long v = strtol(tok, &end, 10);
            if (end != tok)
                out.push_back(static_cast<int>(v));
            else
                out.push_back(defaultValue);
        }
    }
}

// Read a DPS Lock scope ("AA", "AG", "AA,AG") into a bitmask: 1=AA, 2=AG.
static void ReadDPSLock(
    INI_EX& exINI, const char* pSection, const char* pKey, int& out)
{
    if (!exINI.ReadString(pSection, pKey)) return;
    out = 0;
    char buf[64];
    strncpy_s(buf, sizeof(buf), exINI.value(), _TRUNCATE);
    char* ctx = nullptr;
    for (char* tok = strtok_s(buf, ",", &ctx); tok; tok = strtok_s(nullptr, ",", &ctx))
    {
        while (*tok == ' ' || *tok == '\t') ++tok;
        if      (_strnicmp(tok, "AA", 2) == 0) out |= 1;
        else if (_strnicmp(tok, "AG", 2) == 0) out |= 2;
    }
}

// Read a DPS armor name into a Verses index (0-10). Unknown/unset leaves -1.
static void ReadDPSArmor(
    INI_EX& exINI, const char* pSection, const char* pKey, int& out)
{
    if (!exINI.ReadString(pSection, pKey)) return;
    const char* v = exINI.value();
    static const char* const names[] = {
        "none", "flak", "plate", "light", "medium", "heavy",
        "wood", "steel", "concrete", "special_1", "special_2"
    };
    for (int i = 0; i < 11; ++i)
        if (_stricmp(v, names[i]) == 0) { out = i; return; }
}

// Read a comma-separated list of raw strings (trimmed) into out.
static void ReadStringList(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::vector<std::string>& out)
{
    if (exINI.ReadString(pSection, pKey))
    {
        out.clear();
        char* raw = exINI.value();
        char* ctx = nullptr;
        for (char* tok = strtok_s(raw, ",", &ctx);
             tok;
             tok = strtok_s(nullptr, ",", &ctx))
        {
            while (*tok == ' ' || *tok == '\t') ++tok;
            char* end = tok + strlen(tok);
            while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = 0;
            if (*tok)
                out.emplace_back(tok);
        }
    }
}

// Read a list of TechnoTypeClass* (looks up infantry, vehicle, aircraft in order)
static void ReadTechnoTypeList(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::vector<TechnoTypeClass*>& out)
{
    if (exINI.ReadString(pSection, pKey))
    {
        out.clear();
        char* raw = exINI.value();
        char* ctx = nullptr;
        for (char* tok = strtok_s(raw, ",", &ctx);
             tok;
             tok = strtok_s(nullptr, ",", &ctx))
        {
            TechnoTypeClass* pType = nullptr;

            // Try each TechnoType array in order of likelihood
            if (!pType) pType = InfantryTypeClass::Find(tok);
            if (!pType) pType = UnitTypeClass::Find(tok);
            if (!pType) pType = AircraftTypeClass::Find(tok);
            if (!pType) pType = BuildingTypeClass::Find(tok); // fallback

            if (pType)
                out.push_back(pType);
            else
                Debug::INIParseFailed(pSection, pKey, tok);
        }
    }
}

// ---------------------------------------------------------------------------
// [DPSGroupTypes] — named unit groups, shorthand for the DPS/range Types
// filters. Declared once globally:
//   [DPSGroupTypes]
//   Tanks=HTNK,MTNK,APOC
// then RequiredEnemyDPSTypes=Tanks expands to those types. A token that isn't a
// group name is treated as an inline TechnoType ID (so inline lists still work).
// ---------------------------------------------------------------------------
static std::map<std::string, std::vector<TechnoTypeClass*>> g_DPSGroups;
static bool g_DPSGroupsParsed = false;

static TechnoTypeClass* FindTechnoTypeByID(const char* id)
{
    TechnoTypeClass* p = InfantryTypeClass::Find(id);
    if (!p) p = UnitTypeClass::Find(id);
    if (!p) p = AircraftTypeClass::Find(id);
    if (!p) p = BuildingTypeClass::Find(id);
    return p;
}

static void ParseDPSGroups(CCINIClass* pINI)
{
    g_DPSGroups.clear();
    if (!pINI) return;
    const char* const kSection = "DPSGroupTypes";
    int const n = pINI->GetKeyCount(kSection);
    for (int i = 0; i < n; ++i)
    {
        const char* key = pINI->GetKeyName(kSection, i);
        if (!key || !*key) continue;
        char buf[256];
        pINI->ReadString(kSection, key, "", buf, sizeof(buf));
        std::vector<TechnoTypeClass*>& grp = g_DPSGroups[key];
        char* ctx = nullptr;
        for (char* tok = strtok_s(buf, ",", &ctx); tok; tok = strtok_s(nullptr, ",", &ctx))
        {
            while (*tok == ' ' || *tok == '\t') ++tok;
            char* end = tok + strlen(tok);
            while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = 0;
            if (auto p = FindTechnoTypeByID(tok)) grp.push_back(p);
        }
    }
}

// Like ReadTechnoTypeList, but a token may be a [DPSGroupTypes] group name
// (expands to its members) or an inline TechnoType ID.
static void ReadDPSTypeList(INI_EX& exINI, const char* pSection, const char* pKey,
    std::vector<TechnoTypeClass*>& out)
{
    if (!exINI.ReadString(pSection, pKey)) return;
    out.clear();
    char* raw = exINI.value();
    char* ctx = nullptr;
    for (char* tok = strtok_s(raw, ",", &ctx); tok; tok = strtok_s(nullptr, ",", &ctx))
    {
        while (*tok == ' ' || *tok == '\t') ++tok;
        char* end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = 0;
        auto const git = g_DPSGroups.find(tok);
        if (git != g_DPSGroups.end())
            out.insert(out.end(), git->second.begin(), git->second.end());
        else if (auto p = FindTechnoTypeByID(tok))
            out.push_back(p);
    }
}

// Read a list of BuildingTypeClass*
static void ReadBuildingTypeList(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::vector<BuildingTypeClass*>& out)
{
    if (exINI.ReadString(pSection, pKey))
    {
        out.clear();
        char* raw = exINI.value();
        char* ctx = nullptr;
        for (char* tok = strtok_s(raw, ",", &ctx);
             tok;
             tok = strtok_s(nullptr, ",", &ctx))
        {
            if (auto* p = BuildingTypeClass::Find(tok))
                out.push_back(p);
            else
                Debug::INIParseFailed(pSection, pKey, tok);
        }
    }
}

// Read a list of SuperWeaponTypeClass*
static void ReadSWTypeList(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::vector<SuperWeaponTypeClass*>& out)
{
    if (exINI.ReadString(pSection, pKey))
    {
        out.clear();
        char* raw = exINI.value();
        char* ctx = nullptr;
        for (char* tok = strtok_s(raw, ",", &ctx);
             tok;
             tok = strtok_s(nullptr, ",", &ctx))
        {
            if (auto* p = SuperWeaponTypeClass::Find(tok))
                out.push_back(p);
            else
                Debug::INIParseFailed(pSection, pKey, tok);
        }
    }
}

// Read a TypeCountGate<BuildingTypeClass>
static void ReadBuildingGate(
    INI_EX& exINI,
    const char* pSection,
    const char* keyTypes,
    const char* keyMin,
    const char* keyMax,
    TypeCountGate<BuildingTypeClass>& gate)
{
    ReadBuildingTypeList(exINI, pSection, keyTypes, gate.Types);
    ReadIntList(exINI, pSection, keyMin, gate.Min, 0);
    ReadIntList(exINI, pSection, keyMax, gate.Max, -1);
}

// Read a TypeCountGate<TechnoTypeClass>
static void ReadTechnoGate(
    INI_EX& exINI,
    const char* pSection,
    const char* keyTypes,
    const char* keyMin,
    const char* keyMax,
    TypeCountGate<TechnoTypeClass>& gate)
{
    ReadTechnoTypeList(exINI, pSection, keyTypes, gate.Types);
    ReadIntList(exINI, pSection, keyMin, gate.Min, 0);
    ReadIntList(exINI, pSection, keyMax, gate.Max, -1);
}

// Read a SWReadyGate
static void ReadSWGate(
    INI_EX& exINI,
    const char* pSection,
    const char* keyTypes,
    const char* keyReadyMin,
    const char* keyReadyMax,
    SWReadyGate& gate)
{
    ReadSWTypeList(exINI, pSection, keyTypes, gate.Types);
    ReadIntList(exINI, pSection, keyReadyMin, gate.ReadyMin, 0);
    ReadIntList(exINI, pSection, keyReadyMax, gate.ReadyMax, -1);
}

// Read a Nullable<int> min/max pair
static void ReadNullableInt(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    Nullable<int>& out)
{
    int buf = 0;
    if (exINI.ReadInteger(pSection, pKey, &buf))
        out = buf;
}

// Read a bool from yes/no/true/false/1/0 into a plain bool ref
static void ReadBoolFlag(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    bool& out)
{
    if (exINI.ReadString(pSection, pKey))
    {
        const char* v = exINI.value();
        out = (_stricmp(v, "yes") == 0
            || _stricmp(v, "true") == 0
            || _stricmp(v, "1") == 0);
    }
}
static void ReadRawString(
    INI_EX& exINI,
    const char* pSection,
    const char* pKey,
    std::string& out)
{
    if (exINI.ReadString(pSection, pKey))
        out = exINI.value();
}

// ============================================================================
// Initialize
// ============================================================================

void AITriggerTypeExt::ExtData::Initialize()
{
    // All defaults are set in the constructor member initialiser list.
    // TargetHouseMode = Current, AlliesMode = Any, all gates empty,
    // all Nullables unset.
}

// ============================================================================
// LoadFromINIFile
// ============================================================================

void AITriggerTypeExt::ExtData::LoadFromINIFile(CCINIClass* const pINI)
{
    auto const pThis = this->OwnerObject();

    // Build sidecar section name: "<TriggerID>.AIExt"
    char section[0x40];
    _snprintf_s(section, sizeof(section), _TRUNCATE, "%s.AIExt", pThis->ID);

    if (!pINI->GetSection(section))
        return;

    INI_EX exINI(pINI);

    // -----------------------------------------------------------------------
    // Target / Allies mode
    // -----------------------------------------------------------------------
    {
        if (exINI.ReadString(section, "TargetHouseMode"))
        {
            if      (_stricmp(exINI.value(), "Any")            == 0) TargetHouseMode = AIExtTargetHouseMode::Any;
            else if (_stricmp(exINI.value(), "All")            == 0) TargetHouseMode = AIExtTargetHouseMode::All;
            else if (_stricmp(exINI.value(), "Most_Buildings") == 0) TargetHouseMode = AIExtTargetHouseMode::MostBuildings;
            else if (_stricmp(exINI.value(), "Least_Buildings")== 0) TargetHouseMode = AIExtTargetHouseMode::LeastBuildings;
            else if (_stricmp(exINI.value(), "Most_Units")     == 0) TargetHouseMode = AIExtTargetHouseMode::MostUnits;
            else if (_stricmp(exINI.value(), "Least_Units")    == 0) TargetHouseMode = AIExtTargetHouseMode::LeastUnits;
            else                                           TargetHouseMode = AIExtTargetHouseMode::Current;
        }

        if (exINI.ReadString(section, "RequiredAlliesMode"))
        {
            if (_stricmp(exINI.value(), "All") == 0) AlliesMode = AIExtAlliesMode::All;
            else                           AlliesMode = AIExtAlliesMode::Any;
        }
    }

    // -----------------------------------------------------------------------
    // Owner
    // -----------------------------------------------------------------------
    ReadBuildingGate(exINI, section,
        "RequiredOwnerBuildings",
        "RequiredOwnerBuildingsMin",
        "RequiredOwnerBuildingsMax",
        OwnerBuildings);

    ReadTechnoGate(exINI, section,
        "RequiredOwnerUnits",
        "RequiredOwnerUnitsMin",
        "RequiredOwnerUnitsMax",
        OwnerUnits);

    ReadSWGate(exINI, section,
        "RequiredOwnerSuperWeapons",
        "RequiredOwnerSuperWeaponsReadyMin",
        "RequiredOwnerSuperWeaponsReadyMax",
        OwnerSuperWeapons);

    ReadNullableInt(exINI, section, "RequiredOwnerCreditsMin",      OwnerCreditsMin);
    ReadNullableInt(exINI, section, "RequiredOwnerCreditsMax",      OwnerCreditsMax);
    ReadNullableInt(exINI, section, "RequiredOwnerPowerMin",        OwnerPowerMin);
    ReadNullableInt(exINI, section, "RequiredOwnerPowerMax",        OwnerPowerMax);
    ReadNullableInt(exINI, section, "RequiredOwnerPowerOutputMin",  OwnerPowerOutputMin);
    ReadNullableInt(exINI, section, "RequiredOwnerPowerOutputMax",  OwnerPowerOutputMax);
    ReadNullableInt(exINI, section, "RequiredOwnerTechLevelMin",    OwnerTechLevelMin);
    ReadNullableInt(exINI, section, "RequiredOwnerTechLevelMax",    OwnerTechLevelMax);
    ReadNullableInt(exINI, section, "RequiredOwnerDPSMin",          OwnerDPSMin);
    ReadNullableInt(exINI, section, "RequiredOwnerDPSMax",          OwnerDPSMax);
    ReadDPSLock    (exINI, section, "RequiredOwnerDPSLock",         OwnerDPSLock);
    if (!g_DPSGroupsParsed) { ParseDPSGroups(pINI); g_DPSGroupsParsed = true; }
    ReadDPSTypeList(exINI, section, "RequiredOwnerDPSTypes",     OwnerDPSTypes);
    ReadDPSArmor   (exINI, section, "RequiredOwnerDPSArmor",        OwnerDPSArmor);

    // -----------------------------------------------------------------------
    // Enemy
    // -----------------------------------------------------------------------
    ReadBuildingGate(exINI, section,
        "RequiredEnemyBuildings",
        "RequiredEnemyBuildingsMin",
        "RequiredEnemyBuildingsMax",
        EnemyBuildings);

    ReadTechnoGate(exINI, section,
        "RequiredEnemyUnits",
        "RequiredEnemyUnitsMin",
        "RequiredEnemyUnitsMax",
        EnemyUnits);

    ReadSWGate(exINI, section,
        "RequiredEnemySuperWeapons",
        "RequiredEnemySuperWeaponsReadyMin",
        "RequiredEnemySuperWeaponsReadyMax",
        EnemySuperWeapons);

    ReadNullableInt(exINI, section, "RequiredEnemyCreditsMin",      EnemyCreditsMin);
    ReadNullableInt(exINI, section, "RequiredEnemyCreditsMax",      EnemyCreditsMax);
    ReadNullableInt(exINI, section, "RequiredEnemyPowerMin",        EnemyPowerMin);
    ReadNullableInt(exINI, section, "RequiredEnemyPowerMax",        EnemyPowerMax);
    ReadNullableInt(exINI, section, "RequiredEnemyPowerOutputMin",  EnemyPowerOutputMin);
    ReadNullableInt(exINI, section, "RequiredEnemyPowerOutputMax",  EnemyPowerOutputMax);
    ReadNullableInt(exINI, section, "RequiredEnemyTechLevelMin",    EnemyTechLevelMin);
    ReadNullableInt(exINI, section, "RequiredEnemyTechLevelMax",    EnemyTechLevelMax);
    ReadNullableInt(exINI, section, "RequiredEnemyDPSMin",          EnemyDPSMin);
    ReadNullableInt(exINI, section, "RequiredEnemyDPSMax",          EnemyDPSMax);
    ReadDPSLock    (exINI, section, "RequiredEnemyDPSLock",         EnemyDPSLock);
    ReadDPSTypeList(exINI, section, "RequiredEnemyDPSTypes",     EnemyDPSTypes);
    ReadDPSArmor   (exINI, section, "RequiredEnemyDPSArmor",        EnemyDPSArmor);
    ReadNullableInt(exINI, section, "RequiredEnemyMaxRangeMin",     EnemyMaxRangeMin);
    ReadNullableInt(exINI, section, "RequiredEnemyMaxRangeMax",     EnemyMaxRangeMax);
    ReadDPSLock    (exINI, section, "RequiredEnemyMaxRangeLock",    EnemyMaxRangeLock);
    ReadDPSTypeList(exINI, section, "RequiredEnemyMaxRangeTypes", EnemyMaxRangeTypes);
    ReadNullableInt(exINI, section, "RequiredDPSRatioMin",          DPSRatioMin);
    ReadNullableInt(exINI, section, "RequiredDPSRatioMax",          DPSRatioMax);
    ReadDPSLock    (exINI, section, "RequiredDPSRatioLock",         DPSRatioLock);
    ReadNullableInt(exINI, section, "RequiredTeamRangeRatioMin",    TeamRangeRatioMin);
    ReadNullableInt(exINI, section, "RequiredTeamRangeRatioMax",    TeamRangeRatioMax);
    ReadDPSLock    (exINI, section, "RequiredTeamRangeRatioLock",   TeamRangeRatioLock);
    ReadNullableInt(exINI, section, "RequiredBaseDistanceMin",      BaseDistanceMin);
    ReadNullableInt(exINI, section, "RequiredBaseDistanceMax",      BaseDistanceMax);
    ReadNullableInt(exINI, section, "RequiredOwnerCreditsRateMin",  OwnerCreditsRateMin);
    ReadNullableInt(exINI, section, "RequiredOwnerCreditsRateMax",  OwnerCreditsRateMax);
    ReadNullableInt(exINI, section, "RequiredEnemyCreditsRateMin",  EnemyCreditsRateMin);
    ReadNullableInt(exINI, section, "RequiredEnemyCreditsRateMax",  EnemyCreditsRateMax);
    ReadNullableInt(exINI, section, "RequiredCreditsRateWindow",    CreditsRateWindow);
    ReadBuildingTypeList(exINI, section, "RequiredStructureOnMap",  StructureOnMapTypes);
    ReadNullableInt(exINI, section, "RequiredStructureOnMapMin",    StructureOnMapMin);
    ReadNullableInt(exINI, section, "RequiredStructureOnMapMax",    StructureOnMapMax);
    ReadNullableInt(exINI, section, "RequiredCooldown",             Cooldown);
    ReadNullableInt(exINI, section, "RequiredOwnerDifficultyMin",   OwnerDifficultyMin);
    ReadNullableInt(exINI, section, "RequiredOwnerDifficultyMax",   OwnerDifficultyMax);

    // -----------------------------------------------------------------------
    // Allies
    // -----------------------------------------------------------------------
    ReadBuildingGate(exINI, section,
        "RequiredAlliesBuildings",
        "RequiredAlliesBuildingsMin",
        "RequiredAlliesBuildingsMax",
        AlliesBuildings);

    ReadTechnoGate(exINI, section,
        "RequiredAlliesUnits",
        "RequiredAlliesUnitsMin",
        "RequiredAlliesUnitsMax",
        AlliesUnits);

    ReadSWGate(exINI, section,
        "RequiredAlliesSuperWeapons",
        "RequiredAlliesSuperWeaponsReadyMin",
        "RequiredAlliesSuperWeaponsReadyMax",
        AlliesSuperWeapons);

    ReadNullableInt(exINI, section, "RequiredAlliesCreditsMin",     AlliesCreditsMin);
    ReadNullableInt(exINI, section, "RequiredAlliesCreditsMax",     AlliesCreditsMax);
    ReadNullableInt(exINI, section, "RequiredAlliesPowerMin",       AlliesPowerMin);
    ReadNullableInt(exINI, section, "RequiredAlliesPowerMax",       AlliesPowerMax);
    ReadNullableInt(exINI, section, "RequiredAlliesPowerOutputMin", AlliesPowerOutputMin);
    ReadNullableInt(exINI, section, "RequiredAlliesPowerOutputMax", AlliesPowerOutputMax);
    ReadNullableInt(exINI, section, "RequiredAlliesTechLevelMin",   AlliesTechLevelMin);
    ReadNullableInt(exINI, section, "RequiredAlliesTechLevelMax",   AlliesTechLevelMax);

    // -----------------------------------------------------------------------
    // Neutral
    // -----------------------------------------------------------------------
    ReadBuildingGate(exINI, section,
        "RequiredNeutralBuildings",
        "RequiredNeutralBuildingsMin",
        "RequiredNeutralBuildingsMax",
        NeutralBuildings);

    ReadTechnoGate(exINI, section,
        "RequiredNeutralUnits",
        "RequiredNeutralUnitsMin",
        "RequiredNeutralUnitsMax",
        NeutralUnits);

    ReadSWGate(exINI, section,
        "RequiredNeutralSuperWeapons",
        "RequiredNeutralSuperWeaponsReadyMin",
        "RequiredNeutralSuperWeaponsReadyMax",
        NeutralSuperWeapons);

    ReadNullableInt(exINI, section, "RequiredNeutralCreditsMin",     NeutralCreditsMin);
    ReadNullableInt(exINI, section, "RequiredNeutralCreditsMax",     NeutralCreditsMax);
    ReadNullableInt(exINI, section, "RequiredNeutralPowerMin",       NeutralPowerMin);
    ReadNullableInt(exINI, section, "RequiredNeutralPowerMax",       NeutralPowerMax);
    ReadNullableInt(exINI, section, "RequiredNeutralPowerOutputMin", NeutralPowerOutputMin);
    ReadNullableInt(exINI, section, "RequiredNeutralPowerOutputMax", NeutralPowerOutputMax);
    ReadNullableInt(exINI, section, "RequiredNeutralTechLevelMin",   NeutralTechLevelMin);
    ReadNullableInt(exINI, section, "RequiredNeutralTechLevelMax",   NeutralTechLevelMax);

    // -----------------------------------------------------------------------
    // Elapsed time
    // -----------------------------------------------------------------------
    ReadNullableInt(exINI, section, "RequiredElapsedTimeMin", ElapsedTimeMin);
    ReadNullableInt(exINI, section, "RequiredElapsedTimeMax", ElapsedTimeMax);
    // -----------------------------------------------------------------------
    // Debug — overlay CSF strings
    // -----------------------------------------------------------------------
    ReadRawString(exINI, section, "DebugMessageDisplay.Consider",  DebugMessageDisplay_Consider);
    ReadRawString(exINI, section, "DebugMessageDisplay.Cancel", DebugMessageDisplay_Cancel);
    ReadRawString(exINI, section, "DebugMessageDisplay.Finish", DebugMessageDisplay_Finish);
    ReadRawString(exINI, section, "DebugMessageDisplay.Start", DebugMessageDisplay_Start);
    ReadRawString(exINI, section, "DebugMessageDisplay.Destroyed", DebugMessageDisplay_Destroyed);
    ReadRawString(exINI, section, "DebugMessageDisplay.Deleted", DebugMessageDisplay_Deleted);
    ReadRawString(exINI, section, "DebugMessageDisplay.Reject", DebugMessageDisplay_Reject);
    ReadRawString(exINI, section, "DebugMessageDisplay.Selected", DebugMessageDisplay_Selected);

    // Per-gate debug quads emitted by the wave-generator tool
    // (<root>.Debug.MessageDisplay/ValueDisplay/LogMessage/LogWrite/
    //  DetailsDisplay/DetailsTypes). Scanned generically from section keys.
    ParseGateDebug(pINI, section);

    // Per-trigger weight delta overrides (Priority 6)
    ReadNullableInt(exINI, section, "SuccessWeightDelta", SuccessWeightDelta);
    ReadNullableInt(exINI, section, "FailureWeightDelta", FailureWeightDelta);

    // Weight cascades (Priority 6)
    ReadStringList(exINI, section, "SuccessCascadeTargets",       SuccessCascadeTargets);
    ReadIntList   (exINI, section, "SuccessCascadeTargets.Delta", SuccessCascadeDeltas, 0);
    ReadStringList(exINI, section, "FailureCascadeTargets",       FailureCascadeTargets);
    ReadIntList   (exINI, section, "FailureCascadeTargets.Delta", FailureCascadeDeltas, 0);

    // -----------------------------------------------------------------------
    // Debug — raw log strings
    // -----------------------------------------------------------------------
    if (exINI.ReadString(section, "DebugLog.Consider"))
        DebugLog_Consider = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Cancel"))
        DebugLog_Cancel = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Finish"))
        DebugLog_Finish = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Start"))
        DebugLog_Start = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Destroyed"))
        DebugLog_Destroyed = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Deleted"))
        DebugLog_Deleted = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Reject"))
        DebugLog_Reject = exINI.value();
    if (exINI.ReadString(section, "DebugLog.Selected"))
        DebugLog_Selected = exINI.value();

    // -----------------------------------------------------------------------
    // Debug — per-condition auto-verbose flags
    // -----------------------------------------------------------------------
    ReadBoolFlag(exINI, section, "RequiredOwnerBuildings.DebugLog",     DebugLog_OwnerBuildings);
    ReadBoolFlag(exINI, section, "RequiredOwnerUnits.DebugLog",         DebugLog_OwnerUnits);
    ReadBoolFlag(exINI, section, "RequiredOwnerSuperWeapons.DebugLog",  DebugLog_OwnerSuperWeapons);
    ReadBoolFlag(exINI, section, "RequiredOwnerCredits.DebugLog",       DebugLog_OwnerCredits);
    ReadBoolFlag(exINI, section, "RequiredOwnerPower.DebugLog",         DebugLog_OwnerPower);
    ReadBoolFlag(exINI, section, "RequiredOwnerPowerOutput.DebugLog",   DebugLog_OwnerPowerOutput);
    ReadBoolFlag(exINI, section, "RequiredOwnerTechLevel.DebugLog",     DebugLog_OwnerTechLevel);

    ReadBoolFlag(exINI, section, "RequiredEnemyBuildings.DebugLog",     DebugLog_EnemyBuildings);
    ReadBoolFlag(exINI, section, "RequiredEnemyUnits.DebugLog",         DebugLog_EnemyUnits);
    ReadBoolFlag(exINI, section, "RequiredEnemySuperWeapons.DebugLog",  DebugLog_EnemySuperWeapons);
    ReadBoolFlag(exINI, section, "RequiredEnemyCredits.DebugLog",       DebugLog_EnemyCredits);
    ReadBoolFlag(exINI, section, "RequiredEnemyPower.DebugLog",         DebugLog_EnemyPower);
    ReadBoolFlag(exINI, section, "RequiredEnemyPowerOutput.DebugLog",   DebugLog_EnemyPowerOutput);
    ReadBoolFlag(exINI, section, "RequiredEnemyTechLevel.DebugLog",     DebugLog_EnemyTechLevel);

    ReadBoolFlag(exINI, section, "RequiredAlliesBuildings.DebugLog",    DebugLog_AlliesBuildings);
    ReadBoolFlag(exINI, section, "RequiredAlliesUnits.DebugLog",        DebugLog_AlliesUnits);
    ReadBoolFlag(exINI, section, "RequiredAlliesSuperWeapons.DebugLog", DebugLog_AlliesSuperWeapons);
    ReadBoolFlag(exINI, section, "RequiredAlliesCredits.DebugLog",      DebugLog_AlliesCredits);
    ReadBoolFlag(exINI, section, "RequiredAlliesPower.DebugLog",        DebugLog_AlliesPower);
    ReadBoolFlag(exINI, section, "RequiredAlliesPowerOutput.DebugLog",  DebugLog_AlliesPowerOutput);
    ReadBoolFlag(exINI, section, "RequiredAlliesTechLevel.DebugLog",    DebugLog_AlliesTechLevel);

    ReadBoolFlag(exINI, section, "RequiredNeutralBuildings.DebugLog",   DebugLog_NeutralBuildings);
    ReadBoolFlag(exINI, section, "RequiredNeutralUnits.DebugLog",       DebugLog_NeutralUnits);
    ReadBoolFlag(exINI, section, "RequiredNeutralSuperWeapons.DebugLog",DebugLog_NeutralSuperWeapons);
    ReadBoolFlag(exINI, section, "RequiredNeutralCredits.DebugLog",     DebugLog_NeutralCredits);
    ReadBoolFlag(exINI, section, "RequiredNeutralPower.DebugLog",       DebugLog_NeutralPower);
    ReadBoolFlag(exINI, section, "RequiredNeutralPowerOutput.DebugLog", DebugLog_NeutralPowerOutput);
    ReadBoolFlag(exINI, section, "RequiredNeutralTechLevel.DebugLog",   DebugLog_NeutralTechLevel);

    ReadBoolFlag(exINI, section, "RequiredElapsedTime.DebugLog",        DebugLog_ElapsedTime);
}

// ============================================================================
// InvalidatePointer
// ============================================================================

void AITriggerTypeExt::ExtData::InvalidatePointer(void* const ptr, bool const bRemoved)
{
    // We hold only TypeClass* (type objects never individually invalidated)
    // and SuperWeaponTypeClass* (same).  No instance pointers.
    (void)ptr;
    (void)bRemoved;
}

// ============================================================================
// Static per-house check helpers
// ============================================================================

// Count how many of this TechnoType the house currently owns on the map.
// Covers Infantry, Unit (vehicle), Aircraft.  Buildings are handled separately.
int AITriggerTypeExt::ExtData::CountOwnedTechnoType(
    HouseClass* const pHouse,
    TechnoTypeClass* const pType)
{
    if (!pHouse || !pType)
        return 0;

    switch (pType->WhatAmI())
    {
    case AbstractType::InfantryType:
        return pHouse->CountOwnedAndPresent(static_cast<InfantryTypeClass*>(pType));
    case AbstractType::UnitType:
        return pHouse->CountOwnedAndPresent(static_cast<UnitTypeClass*>(pType));
    case AbstractType::AircraftType:
        return pHouse->CountOwnedAndPresent(static_cast<AircraftTypeClass*>(pType));
    case AbstractType::BuildingType:
        return pHouse->CountOwnedAndPresent(static_cast<BuildingTypeClass*>(pType));
    default:
        return 0;
    }
}

bool AITriggerTypeExt::ExtData::CheckHouseBuildings(
    HouseClass* const pHouse,
    const TypeCountGate<BuildingTypeClass>& gate)
{
    if (gate.empty() || !pHouse)
        return true;

    for (size_t i = 0; i < gate.Types.size(); ++i)
    {
        if (!gate.Types[i]) continue;
        int count = pHouse->CountOwnedAndPresent(gate.Types[i]);
        if (!gate.CheckCount(i, count))
            return false;
    }
    return true;
}

bool AITriggerTypeExt::ExtData::CheckHouseUnits(
    HouseClass* const pHouse,
    const TypeCountGate<TechnoTypeClass>& gate)
{
    if (gate.empty() || !pHouse)
        return true;

    for (size_t i = 0; i < gate.Types.size(); ++i)
    {
        if (!gate.Types[i]) continue;
        int count = CountOwnedTechnoType(pHouse, gate.Types[i]);
        if (!gate.CheckCount(i, count))
            return false;
    }
    return true;
}

bool AITriggerTypeExt::ExtData::CheckHouseSuperWeapons(
    HouseClass* const pHouse,
    const SWReadyGate& gate)
{
    if (gate.empty() || !pHouse)
        return true;

    for (size_t i = 0; i < gate.Types.size(); ++i)
    {
        auto const pSWType = gate.Types[i];
        if (!pSWType) continue;

        // Find at least one instance of this SW type on the house.
        // If any instance satisfies the frames-remaining window, the entry passes.
        bool found = false;
        bool satisfied = false;

        for (int j = 0; j < pHouse->Supers.Count; ++j)
        {
            auto const pSW = pHouse->Supers.Items[j];
            if (!pSW || pSW->Type != pSWType)
                continue;

            found = true;
            int framesLeft = pSW->RechargeTimer.GetTimeLeft();
            // If fully charged, IsCharged==true and GetTimeLeft()==0.
            // If still charging, GetTimeLeft() > 0.
            if (gate.CheckFramesLeft(i, framesLeft))
            {
                satisfied = true;
                break; // any instance satisfying is enough
            }
        }

        if (!found)
        {
            // House doesn't have this SW type at all.
            // Only passes if ReadyMin==0 and ReadyMax==-1 (no constraint effectively).
            if (gate.GetReadyMin(i) > 0)
                return false; // requires some readiness but SW doesn't exist
            // If min is 0 and max is uncapped, it trivially passes.
            // Otherwise (max is set), it also passes since framesLeft would be
            // conceptually infinite — but that's ambiguous. Treat missing SW as failing
            // when ReadyMax is set, since the check can't be satisfied.
            if (gate.GetReadyMax(i) != -1)
                return false;
        }
        else if (!satisfied)
        {
            return false;
        }
    }
    return true;
}

// Scalar helpers — all follow the same pattern:
// if min is set and value < min → fail
// if max is set and max != -1 and value > max → fail

bool AITriggerTypeExt::ExtData::CheckHouseCredits(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max)
{
    if (!pHouse) return true;
    int val = pHouse->Balance;
    if (min.isset() && val < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && val > max.Get()) return false;
    return true;
}

bool AITriggerTypeExt::ExtData::CheckHousePower(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max)
{
    if (!pHouse) return true;
    int net = pHouse->PowerOutput - pHouse->PowerDrain;
    if (min.isset() && net < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && net > max.Get()) return false;
    return true;
}

bool AITriggerTypeExt::ExtData::CheckHousePowerOutput(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max)
{
    if (!pHouse) return true;
    int val = pHouse->PowerOutput;
    if (min.isset() && val < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && val > max.Get()) return false;
    return true;
}

bool AITriggerTypeExt::ExtData::CheckHouseTechLevel(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max)
{
    if (!pHouse) return true;
    int val = pHouse->TechLevel;
    if (min.isset() && val < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && val > max.Get()) return false;
    return true;
}

// Sum the raw combat DPS of every object the house currently owns. Scans
// weapons 0 and 1, damage>0 (so repair/support weapons are excluded).
//   lockMask: 1=AA (projectile can hit air), 2=AG (can hit ground), 0=all.
//             A weapon counts if the mask is 0, or it matches a requested scope.
// Cached per-house-per-scope per frame — ConditionMet runs hot and several
// triggers may query the same house/scope on one frame.
double AITriggerTypeExt::ExtData::ComputeHouseDPS(HouseClass* const pHouse,
    int const lockMask, const std::vector<TechnoTypeClass*>* const types,
    int const armorIndex)
{
    if (!pHouse) return 0.0;

    double total = 0.0;
    auto accumulate = [&](TechnoTypeClass* pType)
    {
        if (!pType) return;
        int const count = CountOwnedTechnoType(pHouse, pType);
        if (count <= 0) return;

        for (int wi = 0; wi < 2; ++wi)
        {
            auto const pWS = pType->GetWeapon(wi);
            if (!pWS || !pWS->WeaponType) continue;
            auto const w = pWS->WeaponType;
            if (w->ROF <= 0 || w->Damage <= 1) continue; // skip detector/utility weapons (e.g. VirtualScanner, Damage=1)

            if (lockMask != 0)
            {
                auto const proj = w->Projectile;
                bool const matchAA = (lockMask & 1) && proj && proj->AA;
                bool const matchAG = (lockMask & 2) && proj && proj->AG;
                if (!matchAA && !matchAG) continue; // outside requested scope
            }

            int const burst = w->Burst > 0 ? w->Burst : 1;
            double dps = static_cast<double>(w->Damage) * burst / (w->ROF / 10.0);

            // Effective DPS vs a chosen armor = raw * warhead Verses[armor].
            if (armorIndex >= 0 && armorIndex < 11 && w->Warhead)
                dps *= w->Warhead->Verses[armorIndex];

            total += dps * count;
        }
    };

    // Typed: only the listed unit types. Not cached (few types, opt-in, rare).
    if (types && !types->empty())
    {
        for (auto const pType : *types) accumulate(pType);
        return total;
    }

    // Unfiltered: all types, cached per house+scope+armor per frame.
    static std::map<int, std::pair<int, double>> cache; // key -> (frame, dps)
    int const frame = Unsorted::CurrentFrame;
    int const key   = pHouse->ArrayIndex * 256 + (armorIndex + 1) * 8 + (lockMask & 7);
    auto const it = cache.find(key);
    if (it != cache.end() && it->second.first == frame)
        return it->second.second;

    for (auto const p : InfantryTypeClass::Array) accumulate(p);
    for (auto const p : UnitTypeClass::Array)     accumulate(p);
    for (auto const p : AircraftTypeClass::Array) accumulate(p);
    for (auto const p : BuildingTypeClass::Array) accumulate(p);

    cache[key] = { frame, total };
    return total;
}

bool AITriggerTypeExt::ExtData::CheckHouseDPS(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max,
    int const lockMask,
    const std::vector<TechnoTypeClass*>* const types,
    int const armorIndex)
{
    if (!pHouse) return true;
    if (!min.isset() && !max.isset()) return true; // no DPS gate on this house
    int const val = static_cast<int>(ComputeHouseDPS(pHouse, lockMask, types, armorIndex));
    if (min.isset() && val < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && val > max.Get()) return false;
    return true;
}

// Longest weapon range (cells) among the house's owned damaging weapons.
// Weapon Range is in leptons (256 per cell). Same iteration/scope as DPS.
int AITriggerTypeExt::ExtData::ComputeHouseMaxRange(HouseClass* const pHouse,
    int const lockMask, const std::vector<TechnoTypeClass*>* const types)
{
    if (!pHouse) return 0;

    int maxRange = 0;
    auto consider = [&](TechnoTypeClass* pType)
    {
        if (!pType) return;
        if (CountOwnedTechnoType(pHouse, pType) <= 0) return;

        for (int wi = 0; wi < 2; ++wi)
        {
            auto const pWS = pType->GetWeapon(wi);
            if (!pWS || !pWS->WeaponType) continue;
            auto const w = pWS->WeaponType;
            if (w->Damage <= 1) continue; // real weapons only (skip detectors like VirtualScanner, Damage=1)

            if (lockMask != 0)
            {
                auto const proj = w->Projectile;
                bool const matchAA = (lockMask & 1) && proj && proj->AA;
                bool const matchAG = (lockMask & 2) && proj && proj->AG;
                if (!matchAA && !matchAG) continue;
            }

            int const cells = w->Range / 256;
            if (cells > maxRange) maxRange = cells;
        }
    };

    if (types && !types->empty())
    {
        for (auto const pType : *types) consider(pType);
        return maxRange;
    }

    static std::map<int, std::pair<int, int>> cache; // key -> (frame, maxRange)
    int const frame = Unsorted::CurrentFrame;
    int const key   = pHouse->ArrayIndex * 8 + (lockMask & 7);
    auto const it = cache.find(key);
    if (it != cache.end() && it->second.first == frame)
        return it->second.second;

    for (auto const p : InfantryTypeClass::Array) consider(p);
    for (auto const p : UnitTypeClass::Array)     consider(p);
    for (auto const p : AircraftTypeClass::Array) consider(p);
    for (auto const p : BuildingTypeClass::Array) consider(p);

    cache[key] = { frame, maxRange };
    return maxRange;
}

bool AITriggerTypeExt::ExtData::CheckHouseMaxRange(
    HouseClass* const pHouse,
    const Nullable<int>& min,
    const Nullable<int>& max,
    int const lockMask,
    const std::vector<TechnoTypeClass*>* const types)
{
    if (!pHouse) return true;
    if (!min.isset() && !max.isset()) return true;
    int const val = ComputeHouseMaxRange(pHouse, lockMask, types);
    if (min.isset() && val < min.Get()) return false;
    if (max.isset() && max.Get() != -1 && val > max.Get()) return false;
    return true;
}

// ============================================================================
// Sum helpers for Most/Least house selection
// ============================================================================

int AITriggerTypeExt::ExtData::SumBuildingCount(HouseClass* const pHouse) const
{
    if (!pHouse) return 0;
    int total = 0;
    for (auto const pType : EnemyBuildings.Types)
    {
        if (pType)
            total += pHouse->CountOwnedAndPresent(pType);
    }
    return total;
}

int AITriggerTypeExt::ExtData::SumUnitCount(HouseClass* const pHouse) const
{
    if (!pHouse) return 0;
    int total = 0;
    for (auto const pType : EnemyUnits.Types)
    {
        if (pType)
            total += CountOwnedTechnoType(pHouse, pType);
    }
    return total;
}

// ============================================================================
// ResolveTargetHouse
//
// Returns the single enemy house to run enemy checks against, based on
// TargetHouseMode.  Returns nullptr for Any/All modes (those are handled
// in CheckEnemy directly).
// ============================================================================

HouseClass* AITriggerTypeExt::ExtData::ResolveTargetHouse(
    HouseClass* const pCallingHouse,
    HouseClass* const pEngineTarget) const
{
    auto const mode = TargetHouseMode.Get();

    switch (mode)
    {
    case AIExtTargetHouseMode::Current:
        return pEngineTarget;

    case AIExtTargetHouseMode::Any:
    case AIExtTargetHouseMode::All:
        // Handled in CheckEnemy; return nullptr as sentinel.
        return nullptr;

    case AIExtTargetHouseMode::MostBuildings:
    case AIExtTargetHouseMode::LeastBuildings:
    case AIExtTargetHouseMode::MostUnits:
    case AIExtTargetHouseMode::LeastUnits:
    {
        HouseClass* best = nullptr;
        int bestScore = -1;
        bool wantMost = (mode == AIExtTargetHouseMode::MostBuildings ||
                         mode == AIExtTargetHouseMode::MostUnits);

        for (int i = 0; i < HouseClass::Array.Count; ++i)
        {
            auto const pH = HouseClass::Array.Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->Defeated) continue;
            if (pCallingHouse->IsAlliedWith(pH)) continue; // skip allies

            int score = (mode == AIExtTargetHouseMode::MostBuildings ||
                         mode == AIExtTargetHouseMode::LeastBuildings)
                            ? SumBuildingCount(pH)
                            : SumUnitCount(pH);

            if (best == nullptr ||
                (wantMost  && score > bestScore) ||
                (!wantMost && score < bestScore))
            {
                best      = pH;
                bestScore = score;
            }
        }
        return best; // may be nullptr if no enemies found
    }

    default:
        return pEngineTarget;
    }
}

// ============================================================================
// CheckOwner
// ============================================================================

bool AITriggerTypeExt::ExtData::CheckOwner(HouseClass* const pHouse) const
{
    if (!pHouse) return true;

    if (!CheckHouseBuildings(pHouse, OwnerBuildings))     return false;
    if (!CheckHouseUnits(pHouse, OwnerUnits))             return false;
    if (!CheckHouseSuperWeapons(pHouse, OwnerSuperWeapons)) return false;
    if (!CheckHouseCredits(pHouse, OwnerCreditsMin, OwnerCreditsMax)) return false;
    if (!CheckHousePower(pHouse, OwnerPowerMin, OwnerPowerMax)) return false;
    if (!CheckHousePowerOutput(pHouse, OwnerPowerOutputMin, OwnerPowerOutputMax)) return false;
    if (!CheckHouseTechLevel(pHouse, OwnerTechLevelMin, OwnerTechLevelMax)) return false;
    if (!CheckHouseDPS(pHouse, OwnerDPSMin, OwnerDPSMax, OwnerDPSLock, &OwnerDPSTypes, OwnerDPSArmor)) return false;
    return true;
}

// ============================================================================
// CheckEnemy
//
// Handles all TargetHouseMode variants.
// ============================================================================

bool AITriggerTypeExt::ExtData::CheckEnemy(
    HouseClass* const pCallingHouse,
    HouseClass* const pEngineTarget) const
{
    // Quick exit: if no enemy checks are defined, skip entirely.
    bool hasAnyEnemyCheck =
        !EnemyBuildings.empty()    ||
        !EnemyUnits.empty()        ||
        !EnemySuperWeapons.empty() ||
        EnemyCreditsMin.isset()    || EnemyCreditsMax.isset()    ||
        EnemyPowerMin.isset()      || EnemyPowerMax.isset()      ||
        EnemyPowerOutputMin.isset()|| EnemyPowerOutputMax.isset()||
        EnemyTechLevelMin.isset()  || EnemyTechLevelMax.isset()  ||
        EnemyDPSMin.isset()        || EnemyDPSMax.isset()        ||
        EnemyMaxRangeMin.isset()   || EnemyMaxRangeMax.isset();

    if (!hasAnyEnemyCheck)
        return true;

    auto const mode = TargetHouseMode.Get();

    auto CheckOneEnemy = [&](HouseClass* pH) -> bool
    {
        if (!pH) return false;
        if (!CheckHouseBuildings(pH, EnemyBuildings))       return false;
        if (!CheckHouseUnits(pH, EnemyUnits))               return false;
        if (!CheckHouseSuperWeapons(pH, EnemySuperWeapons)) return false;
        if (!CheckHouseCredits(pH, EnemyCreditsMin, EnemyCreditsMax)) return false;
        if (!CheckHousePower(pH, EnemyPowerMin, EnemyPowerMax)) return false;
        if (!CheckHousePowerOutput(pH, EnemyPowerOutputMin, EnemyPowerOutputMax)) return false;
        if (!CheckHouseTechLevel(pH, EnemyTechLevelMin, EnemyTechLevelMax)) return false;
        if (!CheckHouseDPS(pH, EnemyDPSMin, EnemyDPSMax, EnemyDPSLock, &EnemyDPSTypes, EnemyDPSArmor)) return false;
        if (!CheckHouseMaxRange(pH, EnemyMaxRangeMin, EnemyMaxRangeMax, EnemyMaxRangeLock, &EnemyMaxRangeTypes)) return false;
        return true;
    };

    if (mode == AIExtTargetHouseMode::Any)
    {
        // Any enemy house satisfying all checks is enough.
        for (int i = 0; i < HouseClass::Array.Count; ++i)
        {
            auto const pH = HouseClass::Array.Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->Defeated) continue;
            if (pCallingHouse->IsAlliedWith(pH)) continue;
            if (CheckOneEnemy(pH)) return true;
        }
        return false; // No enemy satisfied
    }

    if (mode == AIExtTargetHouseMode::All)
    {
        // All enemy houses must satisfy all checks.
        bool foundAny = false;
        for (int i = 0; i < HouseClass::Array.Count; ++i)
        {
            auto const pH = HouseClass::Array.Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->Defeated) continue;
            if (pCallingHouse->IsAlliedWith(pH)) continue;
            foundAny = true;
            if (!CheckOneEnemy(pH)) return false;
        }
        return foundAny; // true only if at least one enemy existed and all passed
    }

    // Current / Most* / Least* — single resolved house
    auto const pTarget = ResolveTargetHouse(pCallingHouse, pEngineTarget);
    if (!pTarget) return false;
    return CheckOneEnemy(pTarget);
}

// ============================================================================
// CheckAllies
// ============================================================================

bool AITriggerTypeExt::ExtData::CheckAllies(HouseClass* const pCallingHouse) const
{
    bool hasAnyAllyCheck =
        !AlliesBuildings.empty()    ||
        !AlliesUnits.empty()        ||
        !AlliesSuperWeapons.empty() ||
        AlliesCreditsMin.isset()    || AlliesCreditsMax.isset()    ||
        AlliesPowerMin.isset()      || AlliesPowerMax.isset()      ||
        AlliesPowerOutputMin.isset()|| AlliesPowerOutputMax.isset()||
        AlliesTechLevelMin.isset()  || AlliesTechLevelMax.isset();

    if (!hasAnyAllyCheck || !pCallingHouse)
        return true;

    auto CheckOneAlly = [&](HouseClass* pH) -> bool
    {
        if (!CheckHouseBuildings(pH, AlliesBuildings))       return false;
        if (!CheckHouseUnits(pH, AlliesUnits))               return false;
        if (!CheckHouseSuperWeapons(pH, AlliesSuperWeapons)) return false;
        if (!CheckHouseCredits(pH, AlliesCreditsMin, AlliesCreditsMax)) return false;
        if (!CheckHousePower(pH, AlliesPowerMin, AlliesPowerMax)) return false;
        if (!CheckHousePowerOutput(pH, AlliesPowerOutputMin, AlliesPowerOutputMax)) return false;
        if (!CheckHouseTechLevel(pH, AlliesTechLevelMin, AlliesTechLevelMax)) return false;
        return true;
    };

    bool wantAll = (AlliesMode.Get() == AIExtAlliesMode::All);
    bool foundAny = false;

    for (int i = 0; i < HouseClass::Array.Count; ++i)
    {
        auto const pH = HouseClass::Array.Items[i];
        if (!pH || pH == pCallingHouse) continue;
        if (pH->IsNeutral()) continue;
        if (pH->Defeated) continue;
        if (!pCallingHouse->IsAlliedWith(pH)) continue; // must be allied

        foundAny = true;
        bool passes = CheckOneAlly(pH);

        if (wantAll && !passes) return false; // All mode: any failure = overall fail
        if (!wantAll && passes) return true;  // Any mode: one pass = overall pass
    }

    if (!foundAny) return true; // No allies in game; skip check
    if (wantAll)   return true; // All mode: survived the loop = all passed
    return false;               // Any mode: loop finished without a passing ally
}

// ============================================================================
// CheckNeutral
// ============================================================================

bool AITriggerTypeExt::ExtData::CheckNeutral() const
{
    bool hasAnyNeutralCheck =
        !NeutralBuildings.empty()    ||
        !NeutralUnits.empty()        ||
        !NeutralSuperWeapons.empty() ||
        NeutralCreditsMin.isset()    || NeutralCreditsMax.isset()    ||
        NeutralPowerMin.isset()      || NeutralPowerMax.isset()      ||
        NeutralPowerOutputMin.isset()|| NeutralPowerOutputMax.isset()||
        NeutralTechLevelMin.isset()  || NeutralTechLevelMax.isset();

    if (!hasAnyNeutralCheck)
        return true;

    // Find the neutral house
    HouseClass* pNeutral = nullptr;
    for (int i = 0; i < HouseClass::Array.Count; ++i)
    {
        auto const pH = HouseClass::Array.Items[i];
        if (pH && pH->IsNeutral())
        {
            pNeutral = pH;
            break;
        }
    }

    if (!pNeutral)
        return true; // No neutral house in this scenario; skip check

    if (!CheckHouseBuildings(pNeutral, NeutralBuildings))       return false;
    if (!CheckHouseUnits(pNeutral, NeutralUnits))               return false;
    if (!CheckHouseSuperWeapons(pNeutral, NeutralSuperWeapons)) return false;
    if (!CheckHouseCredits(pNeutral, NeutralCreditsMin, NeutralCreditsMax)) return false;
    if (!CheckHousePower(pNeutral, NeutralPowerMin, NeutralPowerMax)) return false;
    if (!CheckHousePowerOutput(pNeutral, NeutralPowerOutputMin, NeutralPowerOutputMax)) return false;
    if (!CheckHouseTechLevel(pNeutral, NeutralTechLevelMin, NeutralTechLevelMax)) return false;
    return true;
}

// ============================================================================
// CheckElapsedTime
// ============================================================================

bool AITriggerTypeExt::ExtData::CheckElapsedTime() const
{
    if (!ElapsedTimeMin.isset() && !ElapsedTimeMax.isset())
        return true;

    // Unsorted::CurrentFrame is the global game frame counter.
    // ScenarioClass::Instance->ElapsedTimer.StartTime is the frame the
    // scenario timer was started (typically 0 or close to it on scenario init).
    // Elapsed frames = CurrentFrame - ElapsedTimer.StartTime
    // However, since ElapsedTimer counts frames since game start, we can also
    // use -ElapsedTimer.GetTimeLeft() (which is negative of remaining time,
    // i.e. frames elapsed) if the timer was started at scenario begin.
    //
    // The safest approach: use Unsorted::CurrentFrame directly as the
    // reference point. Frame 0 = scenario start.
    int currentFrame = Unsorted::CurrentFrame;

    if (ElapsedTimeMin.isset() && currentFrame < ElapsedTimeMin.Get())
        return false;
    if (ElapsedTimeMax.isset() && ElapsedTimeMax.Get() != -1 &&
        currentFrame > ElapsedTimeMax.Get())
        return false;

    return true;
}

// ============================================================================
// ExtraPrerequisitesMet — the main gate
// ============================================================================

bool AITriggerTypeExt::ExtData::ExtraPrerequisitesMet(
    HouseClass* const pCallingHouse,
    HouseClass* const pTargetHouse) const
{
    if (!CheckElapsedTime())                      return false;
    if (!CheckOwner(pCallingHouse))               return false;
    if (!CheckEnemy(pCallingHouse, pTargetHouse)) return false;
    if (!CheckDPSRatio(pCallingHouse, pTargetHouse)) return false;
    if (!CheckTeamRangeRatio(pCallingHouse, pTargetHouse)) return false;
    if (!CheckBaseDistance(pCallingHouse, pTargetHouse)) return false;
    if (!CheckCreditsRate(pCallingHouse, pTargetHouse)) return false;
    if (!CheckStructureOnMap())                   return false;
    if (!CheckCooldown())                         return false;
    if (!CheckDifficulty(pCallingHouse))          return false;
    if (!CheckAllies(pCallingHouse))              return false;
    if (!CheckNeutral())                          return false;
    return true;
}

// Longest weapon range (cells) among the trigger's own Team1/Team2 taskforce
// unit types. Reads AITriggerType->Team[N]->TaskForce->Entries[i].Type.
int AITriggerTypeExt::ExtData::ComputeTriggerTeamMaxRange(
    AITriggerTypeClass* const pTrigger, int const lockMask)
{
    if (!pTrigger) return 0;
    int maxRange = 0;

    auto scanTeam = [&](TeamTypeClass* pTeam)
    {
        if (!pTeam || !pTeam->TaskForce) return;
        auto const tf = pTeam->TaskForce;
        int const n = tf->CountEntries < 6 ? tf->CountEntries : 6;
        for (int i = 0; i < n; ++i)
        {
            auto const pType = tf->Entries[i].Type;
            if (!pType) continue;
            for (int wi = 0; wi < 2; ++wi)
            {
                auto const pWS = pType->GetWeapon(wi);
                if (!pWS || !pWS->WeaponType) continue;
                auto const w = pWS->WeaponType;
                if (w->Damage <= 1) continue; // skip detector/utility secondaries
                if (lockMask != 0)
                {
                    auto const proj = w->Projectile;
                    bool const matchAA = (lockMask & 1) && proj && proj->AA;
                    bool const matchAG = (lockMask & 2) && proj && proj->AG;
                    if (!matchAA && !matchAG) continue;
                }
                int const cells = w->Range / 256;
                if (cells > maxRange) maxRange = cells;
            }
        }
    };

    scanTeam(pTrigger->Team1);
    scanTeam(pTrigger->Team2);
    return maxRange;
}

// Veto if this trigger's own team is outranged by the enemy. Team range as a
// percentage of enemy range; cross-multiplied for a zero-range enemy (Min then
// passes: the enemy has no ranged threat).
bool AITriggerTypeExt::ExtData::CheckTeamRangeRatio(
    HouseClass* const pCallingHouse, HouseClass* const pTargetHouse) const
{
    if (!TeamRangeRatioMin.isset() && !TeamRangeRatioMax.isset()) return true;

    int const teamRange = ComputeTriggerTeamMaxRange(this->OwnerObject(), TeamRangeRatioLock);
    auto const pEnemy = ResolveTargetHouse(pCallingHouse, pTargetHouse);
    int const enemyRange = pEnemy
        ? ComputeHouseMaxRange(pEnemy, TeamRangeRatioLock, nullptr) : 0;

    if (TeamRangeRatioMin.isset() && teamRange * 100 < TeamRangeRatioMin.Get() * enemyRange)
        return false;
    if (TeamRangeRatioMax.isset() && TeamRangeRatioMax.Get() != -1
        && teamRange * 100 > TeamRangeRatioMax.Get() * enemyRange)
        return false;
    return true;
}

// Distance (in cells) between the owner's base center and the resolved enemy's
// base center. Gate on a min/max window so a trigger can react to base
// proximity ("bases far apart → big waves; close together → guerilla"). Uses
// HouseClass::GetBaseCenter() (BaseCenter, or BaseSpawnCell if unset). If no
// single enemy resolves (Any/All modes) or a house lacks a base, distance is 0,
// which trivially satisfies Min (no meaningful separation to gate on).
bool AITriggerTypeExt::ExtData::CheckBaseDistance(
    HouseClass* const pCallingHouse, HouseClass* const pTargetHouse) const
{
    if (!BaseDistanceMin.isset() && !BaseDistanceMax.isset()) return true;
    if (!pCallingHouse) return true;

    auto const pEnemy = ResolveTargetHouse(pCallingHouse, pTargetHouse);
    int dist = 0;
    if (pEnemy)
    {
        auto const ownCenter   = pCallingHouse->GetBaseCenter();
        auto const enemyCenter = pEnemy->GetBaseCenter();
        if (ownCenter != CellStruct::Empty && enemyCenter != CellStruct::Empty)
            dist = static_cast<int>(ownCenter.DistanceFrom(enemyCenter));
    }

    if (BaseDistanceMin.isset() && dist < BaseDistanceMin.Get())
        return false;
    if (BaseDistanceMax.isset() && BaseDistanceMax.Get() != -1
        && dist > BaseDistanceMax.Get())
        return false;
    return true;
}

// Net change in a house's Balance over the last `window` frames (signed).
// Keeps a small per-house rolling sample of (frame, balance), one sample per
// frame, pruned to the window. Static/transient — rebuilt after save/load, so
// the rate reads 0 for the first `window` frames of a fresh session (warmup).
static int ComputeCreditsRate(HouseClass* const pHouse, int const window)
{
    if (!pHouse || window <= 0) return 0;

    static std::map<HouseClass*, std::deque<std::pair<int, int>>> samples;

    int const frame = Unsorted::CurrentFrame;
    int const bal   = pHouse->Balance;
    auto& dq = samples[pHouse];

    if (dq.empty() || dq.back().first != frame)
        dq.emplace_back(frame, bal);
    while (dq.size() > 1 && dq.front().first < frame - window)
        dq.pop_front();

    return bal - dq.front().second;   // current − oldest-in-window
}

// Gate on credit momentum for the owner and/or the resolved enemy.
bool AITriggerTypeExt::ExtData::CheckCreditsRate(
    HouseClass* const pCallingHouse, HouseClass* const pTargetHouse) const
{
    bool const wantOwner = OwnerCreditsRateMin.isset() || OwnerCreditsRateMax.isset();
    bool const wantEnemy = EnemyCreditsRateMin.isset() || EnemyCreditsRateMax.isset();
    if (!wantOwner && !wantEnemy) return true;

    int const window = CreditsRateWindow.Get(150);

    if (wantOwner && pCallingHouse)
    {
        int const rate = ComputeCreditsRate(pCallingHouse, window);
        if (OwnerCreditsRateMin.isset() && rate < OwnerCreditsRateMin.Get()) return false;
        if (OwnerCreditsRateMax.isset() && rate > OwnerCreditsRateMax.Get()) return false;
    }
    if (wantEnemy)
    {
        auto const pEnemy = ResolveTargetHouse(pCallingHouse, pTargetHouse);
        int const rate = pEnemy ? ComputeCreditsRate(pEnemy, window) : 0;
        if (EnemyCreditsRateMin.isset() && rate < EnemyCreditsRateMin.Get()) return false;
        if (EnemyCreditsRateMax.isset() && rate > EnemyCreditsRateMax.Get()) return false;
    }
    return true;
}

// Count all live buildings on the map (any house) whose type is in the list.
static int CountStructuresOnMap(const std::vector<BuildingTypeClass*>& types)
{
    if (types.empty()) return 0;
    int count = 0;
    for (auto const pBld : BuildingClass::Array)
    {
        if (!pBld) continue;
        for (auto const pType : types)
            if (pBld->Type == pType) { ++count; break; }
    }
    return count;
}

// Gate on the global count of the listed structure types existing on the map.
bool AITriggerTypeExt::ExtData::CheckStructureOnMap() const
{
    if (StructureOnMapTypes.empty()) return true;
    if (!StructureOnMapMin.isset() && !StructureOnMapMax.isset()) return true;

    int const count = CountStructuresOnMap(StructureOnMapTypes);
    if (StructureOnMapMin.isset() && count < StructureOnMapMin.Get()) return false;
    if (StructureOnMapMax.isset() && StructureOnMapMax.Get() != -1
        && count > StructureOnMapMax.Get()) return false;
    return true;
}

// Veto until at least Cooldown frames have passed since this trigger last
// created a team (LastStartFrame, stamped in the Start lifecycle event).
bool AITriggerTypeExt::ExtData::CheckCooldown() const
{
    if (!Cooldown.isset()) return true;
    if (LastStartFrame < 0) return true;    // never dispatched → no cooldown yet
    int const since = Unsorted::CurrentFrame - LastStartFrame;
    return since >= Cooldown.Get();
}

// Gate on the owning AI house's difficulty index (Hard=0, Normal=1, Easy=2).
bool AITriggerTypeExt::ExtData::CheckDifficulty(HouseClass* const pHouse) const
{
    if (!OwnerDifficultyMin.isset() && !OwnerDifficultyMax.isset()) return true;
    if (!pHouse) return true;
    int const d = static_cast<int>(pHouse->GetAIDifficultyIndex());
    if (OwnerDifficultyMin.isset() && d < OwnerDifficultyMin.Get()) return false;
    if (OwnerDifficultyMax.isset() && OwnerDifficultyMax.Get() != -1
        && d > OwnerDifficultyMax.Get()) return false;
    return true;
}

// Owner-vs-enemy DPS ratio (percentage; 200 = owner has 2.0x the enemy DPS).
// Cross-multiplied to avoid division and handle a zero-DPS enemy cleanly:
//   Min: ownerDPS*100 >= Min*enemyDPS      (enemy 0 → always passes: dominant)
//   Max: ownerDPS*100 <= Max*enemyDPS
bool AITriggerTypeExt::ExtData::CheckDPSRatio(
    HouseClass* const pCallingHouse, HouseClass* const pTargetHouse) const
{
    if (!DPSRatioMin.isset() && !DPSRatioMax.isset()) return true;
    if (!pCallingHouse) return true;

    auto const pEnemy = ResolveTargetHouse(pCallingHouse, pTargetHouse);
    double const ownerDPS = ComputeHouseDPS(pCallingHouse, DPSRatioLock, nullptr, -1);
    double const enemyDPS = pEnemy ? ComputeHouseDPS(pEnemy, DPSRatioLock, nullptr, -1) : 0.0;

    if (DPSRatioMin.isset() && ownerDPS * 100.0 < DPSRatioMin.Get() * enemyDPS)
        return false;
    if (DPSRatioMax.isset() && DPSRatioMax.Get() != -1
        && ownerDPS * 100.0 > DPSRatioMax.Get() * enemyDPS)
        return false;
    return true;
}

// ============================================================================
// Serialization
// ============================================================================

// Helper: serialize a TypeCountGate
template <typename TStm, typename T>
static void SerializeGate(TStm& Stm, TypeCountGate<T>& gate)
{
    Stm
        .Process(gate.Types)
        .Process(gate.Min)
        .Process(gate.Max);
}

// Helper: serialize a SWReadyGate
template <typename TStm>
static void SerializeSWGate(TStm& Stm, SWReadyGate& gate)
{
    Stm
        .Process(gate.Types)
        .Process(gate.ReadyMin)
        .Process(gate.ReadyMax);
}

template <typename T>
void AITriggerTypeExt::ExtData::Serialize(T& Stm)
{
    Stm
        .Process(this->TargetHouseMode)
        .Process(this->AlliesMode)
        ;

    SerializeGate(Stm, this->OwnerBuildings);
    SerializeGate(Stm, this->OwnerUnits);
    SerializeSWGate(Stm, this->OwnerSuperWeapons);

    Stm
        .Process(this->OwnerCreditsMin)
        .Process(this->OwnerCreditsMax)
        .Process(this->OwnerPowerMin)
        .Process(this->OwnerPowerMax)
        .Process(this->OwnerPowerOutputMin)
        .Process(this->OwnerPowerOutputMax)
        .Process(this->OwnerTechLevelMin)
        .Process(this->OwnerTechLevelMax)
        .Process(this->OwnerDPSMin)
        .Process(this->OwnerDPSMax)
        .Process(this->OwnerDPSLock)
        .Process(this->OwnerDPSTypes)
        .Process(this->OwnerDPSArmor)
        ;

    SerializeGate(Stm, this->EnemyBuildings);
    SerializeGate(Stm, this->EnemyUnits);
    SerializeSWGate(Stm, this->EnemySuperWeapons);

    Stm
        .Process(this->EnemyCreditsMin)
        .Process(this->EnemyCreditsMax)
        .Process(this->EnemyPowerMin)
        .Process(this->EnemyPowerMax)
        .Process(this->EnemyPowerOutputMin)
        .Process(this->EnemyPowerOutputMax)
        .Process(this->EnemyTechLevelMin)
        .Process(this->EnemyTechLevelMax)
        .Process(this->EnemyDPSMin)
        .Process(this->EnemyDPSMax)
        .Process(this->EnemyDPSLock)
        .Process(this->EnemyDPSTypes)
        .Process(this->EnemyDPSArmor)
        .Process(this->EnemyMaxRangeMin)
        .Process(this->EnemyMaxRangeMax)
        .Process(this->EnemyMaxRangeLock)
        .Process(this->EnemyMaxRangeTypes)
        .Process(this->DPSRatioMin)
        .Process(this->DPSRatioMax)
        .Process(this->DPSRatioLock)
        .Process(this->TeamRangeRatioMin)
        .Process(this->TeamRangeRatioMax)
        .Process(this->TeamRangeRatioLock)
        .Process(this->BaseDistanceMin)
        .Process(this->BaseDistanceMax)
        .Process(this->OwnerCreditsRateMin)
        .Process(this->OwnerCreditsRateMax)
        .Process(this->EnemyCreditsRateMin)
        .Process(this->EnemyCreditsRateMax)
        .Process(this->CreditsRateWindow)
        .Process(this->StructureOnMapTypes)
        .Process(this->StructureOnMapMin)
        .Process(this->StructureOnMapMax)
        .Process(this->Cooldown)
        .Process(this->LastStartFrame)
        .Process(this->OwnerDifficultyMin)
        .Process(this->OwnerDifficultyMax)
        ;

    SerializeGate(Stm, this->AlliesBuildings);
    SerializeGate(Stm, this->AlliesUnits);
    SerializeSWGate(Stm, this->AlliesSuperWeapons);

    Stm
        .Process(this->AlliesCreditsMin)
        .Process(this->AlliesCreditsMax)
        .Process(this->AlliesPowerMin)
        .Process(this->AlliesPowerMax)
        .Process(this->AlliesPowerOutputMin)
        .Process(this->AlliesPowerOutputMax)
        .Process(this->AlliesTechLevelMin)
        .Process(this->AlliesTechLevelMax)
        ;

    SerializeGate(Stm, this->NeutralBuildings);
    SerializeGate(Stm, this->NeutralUnits);
    SerializeSWGate(Stm, this->NeutralSuperWeapons);

    Stm
        .Process(this->NeutralCreditsMin)
        .Process(this->NeutralCreditsMax)
        .Process(this->NeutralPowerMin)
        .Process(this->NeutralPowerMax)
        .Process(this->NeutralPowerOutputMin)
        .Process(this->NeutralPowerOutputMax)
        .Process(this->NeutralTechLevelMin)
        .Process(this->NeutralTechLevelMax)
        ;

    Stm
        .Process(this->ElapsedTimeMin)
        .Process(this->ElapsedTimeMax)
        ;

    // Weight adjustment (Priority 6) — behaviour-critical, so persist across
    // save/load (unlike the debug-only strings, which re-read from INI).
    Stm
        .Process(this->SuccessWeightDelta)
        .Process(this->FailureWeightDelta)
        .Process(this->SuccessCascadeTargets)
        .Process(this->SuccessCascadeDeltas)
        .Process(this->FailureCascadeTargets)
        .Process(this->FailureCascadeDeltas)
        ;
}

void AITriggerTypeExt::ExtData::LoadFromStream(PhobosStreamReader& Stm)
{
    Extension<AITriggerTypeClass>::LoadFromStream(Stm);
    this->Serialize(Stm);
}

void AITriggerTypeExt::ExtData::SaveToStream(PhobosStreamWriter& Stm)
{
    Extension<AITriggerTypeClass>::SaveToStream(Stm);
    this->Serialize(Stm);
}

// ============================================================================
// Globals
// ============================================================================

bool AITriggerTypeExt::LoadGlobals(PhobosStreamReader& Stm)
{
    return Stm.Success();
}

bool AITriggerTypeExt::SaveGlobals(PhobosStreamWriter& Stm)
{
    return Stm.Success();
}

// ============================================================================
// DEBUG DISPLAY MODE — read once, cache per game session
//
// Reads [Debug].DisplayAIWaveMessages from rulesmd.ini.
// Returns Off / Overlay / Log / Both.
// ============================================================================

AITriggerTypeExt::DebugDisplayMode AITriggerTypeExt::GetDebugMode()
{
    static DebugDisplayMode cached = DebugDisplayMode::Off;
    static bool             loaded = false;

    if (loaded)
        return cached;

if (!CCINIClass::INI_Rules)
        return DebugDisplayMode::Off;

    char buf[16] = {0};
    CCINIClass::INI_Rules->ReadString(
        "Debug", "DisplayAIWaveMessages", "no", buf, sizeof(buf));

    if      (_stricmp(buf, "yes")     == 0) cached = DebugDisplayMode::Overlay;
    else if (_stricmp(buf, "overlay") == 0) cached = DebugDisplayMode::Overlay;
    else if (_stricmp(buf, "log")     == 0) cached = DebugDisplayMode::Log;
    else if (_stricmp(buf, "both")    == 0) cached = DebugDisplayMode::Both;
    else                                    cached = DebugDisplayMode::Off;

    loaded = true;
    return cached;
}

// ============================================================================
// Lifecycle debug emitters
// ============================================================================

// Convert ASCII string to wide string in a static buffer.
// Returns nullptr if src is empty.
static const wchar_t* ToWideStatic(const char* src)
{
    static wchar_t buf[256];
    if (!src || !*src) return nullptr;
    int i = 0;
    while (src[i] && i < 255) {
        buf[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
        i++;
    }
    buf[i] = 0;
    return buf;
}

// Resolve a debug text string with NOSTR:/STT: prefix handling.
// NOSTR: — literal text, no CSF lookup (safe for arbitrary ASCII)
// STT:   — strip prefix and look up in string table
// bare   — treat as a CSF key
// Returns pointer valid until next ToWideStatic call.
static const wchar_t* ResolveDebugText(const std::string& src)
{
    if (src.empty()) return nullptr;
    if (src.length() > 6 && src.compare(0, 6, "NOSTR:") == 0)
        return ToWideStatic(src.c_str() + 6);
    if (src.length() > 4 && src.compare(0, 4, "STT:") == 0)
        return StringTable::LoadString(src.c_str() + 4);
    return StringTable::LoadString(src.c_str());
}

void AITriggerTypeExt::EmitDebugConsider(
    ExtData* pExt, AITriggerTypeClass* pThis,
    HouseClass* pOwner, HouseClass* pEnemy)
{
    if (!pExt || !pThis) return;
    auto const mode = GetDebugMode();
    if (mode == DebugDisplayMode::Off) return;

    // Overlay
if ((mode == DebugDisplayMode::Overlay || mode == DebugDisplayMode::Both)
        && !pExt->DebugMessageDisplay_Consider.empty())
    {
    const wchar_t* pMsg = ResolveDebugText(pExt->DebugMessageDisplay_Consider);
        if (pMsg && *pMsg)
            MessageListClass::Instance.PrintMessage(pMsg);
    }

    // Log
    if ((mode == DebugDisplayMode::Log || mode == DebugDisplayMode::Both)
        && !pExt->DebugLog_Consider.empty())
    {
        Debug::Log("[AIExt Consider] %s: %s\n",
            pThis->ID, pExt->DebugLog_Consider.c_str());
    }

    // Per-gate debug quad (log side here; HUD is Cancel-only inside)
    pExt->EmitGateDebug(pThis, pOwner, pEnemy, "Consider",
        mode == DebugDisplayMode::Overlay || mode == DebugDisplayMode::Both,
        mode == DebugDisplayMode::Log     || mode == DebugDisplayMode::Both);
}

void AITriggerTypeExt::EmitDebugCancel(
    ExtData* pExt, AITriggerTypeClass* pThis,
    HouseClass* pOwner, HouseClass* pEnemy)
{
    if (!pExt || !pThis) return;
    auto const mode = GetDebugMode();
    if (mode == DebugDisplayMode::Off) return;

if ((mode == DebugDisplayMode::Overlay || mode == DebugDisplayMode::Both)
        && !pExt->DebugMessageDisplay_Cancel.empty())
    {
    const wchar_t* pMsg = ResolveDebugText(pExt->DebugMessageDisplay_Cancel);
        if (pMsg && *pMsg)
            MessageListClass::Instance.PrintMessage(pMsg);
    }

    if ((mode == DebugDisplayMode::Log || mode == DebugDisplayMode::Both)
        && !pExt->DebugLog_Cancel.empty())
    {
        Debug::Log("[AIExt Cancel] %s: %s\n",
            pThis->ID, pExt->DebugLog_Cancel.c_str());
    }

    // Per-gate debug quad — the "why was this vetoed" breakdown (log) plus
    // the failing gate's MessageDisplay on the HUD.
    pExt->EmitGateDebug(pThis, pOwner, pEnemy, "Cancel",
        mode == DebugDisplayMode::Overlay || mode == DebugDisplayMode::Both,
        mode == DebugDisplayMode::Log     || mode == DebugDisplayMode::Both);
}

void AITriggerTypeExt::EmitDebugFinish(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    // Reserved for Phase 2 — needs a hook where the team's script begins.
    // Left as no-op for now so triggers can declare DebugMessageDisplay.Finish=
    // in INI without errors, ready to activate once the hook lands.
    (void)pExt;
    (void)pThis;
}

// Shared body for the confirmed-hook lifecycle events. `label` is the bracket
// tag used in the log line ("Start"/"Destroyed"/"Deleted").
static void EmitLifecycle(
    const char* label,
    AITriggerTypeClass* pThis,
    const std::string& overlay,
    const std::string& logText)
{
    auto const mode = AITriggerTypeExt::GetDebugMode();
    if (mode == AITriggerTypeExt::DebugDisplayMode::Off) return;

    if ((mode == AITriggerTypeExt::DebugDisplayMode::Overlay
            || mode == AITriggerTypeExt::DebugDisplayMode::Both)
        && !overlay.empty())
    {
        const wchar_t* pMsg = ResolveDebugText(overlay);
        if (pMsg && *pMsg)
            MessageListClass::Instance.PrintMessage(pMsg);
    }

    if ((mode == AITriggerTypeExt::DebugDisplayMode::Log
            || mode == AITriggerTypeExt::DebugDisplayMode::Both)
        && !logText.empty())
    {
        Debug::Log("[AIExt %s] %s: %s\n", label, pThis->ID, logText.c_str());
    }
}

void AITriggerTypeExt::EmitDebugStart(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    if (!pExt || !pThis) return;
    pExt->LastStartFrame = Unsorted::CurrentFrame;   // stamp for RequiredCooldown
    EmitLifecycle("Start", pThis,
        pExt->DebugMessageDisplay_Start, pExt->DebugLog_Start);
}

void AITriggerTypeExt::EmitDebugDestroyed(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    if (!pExt || !pThis) return;
    EmitLifecycle("Destroyed", pThis,
        pExt->DebugMessageDisplay_Destroyed, pExt->DebugLog_Destroyed);
}

void AITriggerTypeExt::EmitDebugDeleted(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    if (!pExt || !pThis) return;
    EmitLifecycle("Deleted", pThis,
        pExt->DebugMessageDisplay_Deleted, pExt->DebugLog_Deleted);
}

void AITriggerTypeExt::EmitDebugReject(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    if (!pExt || !pThis) return;
    EmitLifecycle("Reject", pThis,
        pExt->DebugMessageDisplay_Reject, pExt->DebugLog_Reject);
}

void AITriggerTypeExt::EmitDebugSelected(
    ExtData* pExt, AITriggerTypeClass* pThis)
{
    if (!pExt || !pThis) return;
    EmitLifecycle("Selected", pThis,
        pExt->DebugMessageDisplay_Selected, pExt->DebugLog_Selected);
}

// Replace vanilla's global weight delta for THIS trigger. Runs at the hook
// entry, BEFORE vanilla's own adjustment: we pre-apply (override - global), so
// after vanilla adds the global delta back the net effect is exactly `override`
// — while vanilla still applies its track-record scaling and [Min,Max] clamp.
void AITriggerTypeExt::ApplyWeightSelfDelta(
    ExtData* pExt, AITriggerTypeClass* pThis, bool success)
{
    if (!pExt || !pThis) return;

    auto const& ov = success ? pExt->SuccessWeightDelta : pExt->FailureWeightDelta;
    if (!ov.isset()) return;

    auto const pRules = RulesClass::Instance;
    if (!pRules) return;

    double const globalDelta = success
        ? pRules->AITriggerSuccessWeightDelta
        : pRules->AITriggerFailureWeightDelta;
    double const myDelta = static_cast<double>(ov.Get());

    pThis->Weight_Current += (myDelta - globalDelta);

    auto const mode = GetDebugMode();
    if (mode == DebugDisplayMode::Log || mode == DebugDisplayMode::Both)
        Debug::Log("[AIExt WeightDelta] %s %s: override %+d (vanilla global %+.0f)\n",
            success ? "Success" : "Failure", pThis->ID, ov.Get(), globalDelta);
}

// Adjust the Weight_Current of each cascade target when THIS trigger's team
// succeeded (success=true) or failed. Clamped to each target's own
// [Weight_Minimum, Weight_Maximum] — the same bounds vanilla uses. The weight
// change always applies; only the log line is gated behind debug mode.
void AITriggerTypeExt::ApplyWeightCascades(
    ExtData* pExt, AITriggerTypeClass* pThis, bool success)
{
    if (!pExt || !pThis) return;

    auto const& targets = success ? pExt->SuccessCascadeTargets : pExt->FailureCascadeTargets;
    auto const& deltas  = success ? pExt->SuccessCascadeDeltas  : pExt->FailureCascadeDeltas;
    if (targets.empty())
        return;

    auto const mode = GetDebugMode();
    bool const logOn = (mode == DebugDisplayMode::Log || mode == DebugDisplayMode::Both);
    const char* kind = success ? "Success" : "Failure";

    for (size_t i = 0; i < targets.size(); ++i)
    {
        // Positional delta; a single value applies to all, missing entries reuse the last.
        int delta = deltas.empty() ? 0
                  : deltas[i < deltas.size() ? i : deltas.size() - 1];
        if (delta == 0)
            continue;

        auto pTarget = AITriggerTypeClass::Find(targets[i].c_str());
        if (!pTarget || pTarget == pThis)
            continue;

        double before = pTarget->Weight_Current;
        double after  = before + delta;
        if (after < pTarget->Weight_Minimum) after = pTarget->Weight_Minimum;
        if (after > pTarget->Weight_Maximum) after = pTarget->Weight_Maximum;
        pTarget->Weight_Current = after;

        if (logOn)
            Debug::Log("[AIExt Cascade] %s %s -> %s delta %+d weight %.1f -> %.1f\n",
                kind, pThis->ID, pTarget->ID, delta, before, after);
    }
}

// ============================================================================
// Container boilerplate
// ============================================================================

AITriggerTypeExt::ExtContainer::ExtContainer()
    : Container("AITriggerTypeClass")
{ }

AITriggerTypeExt::ExtContainer::~ExtContainer() = default;

// ============================================================================
// Container hooks
// ============================================================================

// CTOR hook is in Hooks.cpp

// DTOR — *** ADDRESS UNCONFIRMED, see DISASM_GUIDE.md ***
/*
DEFINE_HOOK(0x????????, AITriggerTypeClass_SDDTOR, 0x?)
{
    GET(AITriggerTypeClass*, pItem, ESI); // confirm register
    AITriggerTypeExt::ExtMap.Remove(pItem);
    return 0;
}
*/

// SaveLoad prefix — PrepareStream before Load or Save
// *** ADDRESSES UNCONFIRMED ***
/*
DEFINE_HOOK_AGAIN(0x????????, AITriggerTypeClass_SaveLoad_Prefix, 0x?)
DEFINE_HOOK      (0x????????, AITriggerTypeClass_SaveLoad_Prefix, 0x?)
{
    GET_STACK(AITriggerTypeClass*, pItem, 0x4);
    GET_STACK(IStream*,            pStm,  0x8);
    AITriggerTypeExt::ExtMap.PrepareStream(pItem, pStm);
    return 0;
}

DEFINE_HOOK(0x????????, AITriggerTypeClass_Load_Suffix, 0x?)
{
    AITriggerTypeExt::ExtMap.LoadStatic();
    return 0;
}

DEFINE_HOOK(0x????????, AITriggerTypeClass_Save_Suffix, 0x?)
{
    AITriggerTypeExt::ExtMap.SaveStatic();
    return 0;
}
*/

// LoadFromINIList per-item hook — CONFIRMED from Ghidra
//
// Inside CreateFromINIList (0x41F2E0), immediately after the virtual
// LoadFromINI call at 0x41F39C (CALL [EDX+0x64]):
//
//   0041F399  53            PUSH EBX          ← EBX=CCINIClass* pushed as arg
//   0041F39A  8B CE         MOV ECX, ESI      ← ECX=this (trigger)
//   0041F39C  FF 52 64      CALL [EDX+0x64]   ← Trigger->LoadFromINI(pINI)
//   0041F39F  8B 44 24 14   MOV EAX,[ESP+0x14]  ← hook here
//   0041F3A3  89 86 9C...   MOV [ESI+0x9C],EAX  ← isGlobal write (re-emitted)
//
// Registers confirmed:
//   ESI = AITriggerTypeClass*  (set at 0x41F391: MOV ESI,EAX after new)
//   EBX = CCINIClass*          (still live from PUSH EBX at 0x41F399)
//
// Hook size 0xA (10): covers MOV EAX,[ESP+0x14] (4 bytes) +
//                            MOV [ESI+0x9C],EAX (6 bytes).
// Syringe re-emits both before jumping back — isGlobal is still written.
DEFINE_HOOK(0x41F39F, AITriggerTypeClass_LoadFromINI_PerItem, 0xA)
{
    GET(AITriggerTypeClass*, pItem, ESI);
    GET(CCINIClass*,         pINI,  EBX);
    AITriggerTypeExt::ExtMap.LoadFromINI(pItem, pINI);
    return 0;
}


// ============================================================================
// Detail-building methods — walk every gate entry without short-circuit and
// append one structured AIExtCheckLine per entry to out.lines. Formatting
// (log columns / HUD value) happens later in EmitGateDebug.
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
        out.lines.push_back({ gate_name, gate.Types[i]->ID, count,
            gate.GetMin(i), gate.GetMax(i), gate.CheckCount(i, count) });
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
        out.lines.push_back({ gate_name, gate.Types[i]->ID, count,
            gate.GetMin(i), gate.GetMax(i), gate.CheckCount(i, count) });
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

    // Scalar gate: no type_id.
    out.lines.push_back({ gate_name, std::string(), actual,
        min_display, max_display, ok });
}

void AITriggerTypeExt::ExtData::EvaluateAndReport(HouseClass* pOwner, HouseClass* pEnemy) const
{
    LastCheckReport.clear();

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
    if (OwnerDPSMin.isset() || OwnerDPSMax.isset())
        BuildScalarDetail("RequiredOwnerDPS",
            static_cast<int>(ComputeHouseDPS(pOwner, OwnerDPSLock, &OwnerDPSTypes, OwnerDPSArmor)),
            OwnerDPSMin, OwnerDPSMax, LastCheckReport);

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
        if (EnemyDPSMin.isset() || EnemyDPSMax.isset())
            BuildScalarDetail("RequiredEnemyDPS",
                static_cast<int>(ComputeHouseDPS(pEnemy, EnemyDPSLock, &EnemyDPSTypes, EnemyDPSArmor)),
                EnemyDPSMin, EnemyDPSMax, LastCheckReport);
        if (EnemyMaxRangeMin.isset() || EnemyMaxRangeMax.isset())
            BuildScalarDetail("RequiredEnemyMaxRange",
                ComputeHouseMaxRange(pEnemy, EnemyMaxRangeLock, &EnemyMaxRangeTypes),
                EnemyMaxRangeMin, EnemyMaxRangeMax, LastCheckReport);
    }

    // ─── ElapsedTime (game-scope, no house needed) ──────────────────────
    if (ElapsedTimeMin.isset() || ElapsedTimeMax.isset())
    {
        int elapsed = Unsorted::CurrentFrame;
        BuildScalarDetail("RequiredElapsedTime", elapsed,
            ElapsedTimeMin, ElapsedTimeMax, LastCheckReport);
    }

    // ─── DPS ratio (owner vs enemy, as a percentage) ────────────────────
    if (DPSRatioMin.isset() || DPSRatioMax.isset())
    {
        double const o = ComputeHouseDPS(pOwner, DPSRatioLock, nullptr, -1);
        double const e = pEnemy ? ComputeHouseDPS(pEnemy, DPSRatioLock, nullptr, -1) : 0.0;
        int const ratioPct = (e > 0.0) ? static_cast<int>(o * 100.0 / e) : 999999;
        BuildScalarDetail("RequiredDPSRatio", ratioPct,
            DPSRatioMin, DPSRatioMax, LastCheckReport);
    }

    // ─── Team-vs-enemy range ratio (percentage; 100 = matched) ──────────
    if (TeamRangeRatioMin.isset() || TeamRangeRatioMax.isset())
    {
        int const tr = ComputeTriggerTeamMaxRange(this->OwnerObject(), TeamRangeRatioLock);
        int const er = pEnemy ? ComputeHouseMaxRange(pEnemy, TeamRangeRatioLock, nullptr) : 0;
        int const ratioPct = (er > 0) ? (tr * 100 / er) : 999999;
        BuildScalarDetail("RequiredTeamRangeRatio", ratioPct,
            TeamRangeRatioMin, TeamRangeRatioMax, LastCheckReport);
    }

    // ─── Base-to-base distance (cells) ──────────────────────────────────
    if (BaseDistanceMin.isset() || BaseDistanceMax.isset())
    {
        int dist = 0;
        if (pEnemy)
        {
            auto const oc = pOwner->GetBaseCenter();
            auto const ec = pEnemy->GetBaseCenter();
            if (oc != CellStruct::Empty && ec != CellStruct::Empty)
                dist = static_cast<int>(oc.DistanceFrom(ec));
        }
        BuildScalarDetail("RequiredBaseDistance", dist,
            BaseDistanceMin, BaseDistanceMax, LastCheckReport);
    }

    // ─── Credit momentum (net Balance change over the window) ───────────
    if (OwnerCreditsRateMin.isset() || OwnerCreditsRateMax.isset())
    {
        int const rate = ComputeCreditsRate(pOwner, CreditsRateWindow.Get(150));
        BuildScalarDetail("RequiredOwnerCreditsRate", rate,
            OwnerCreditsRateMin, OwnerCreditsRateMax, LastCheckReport);
    }
    if (EnemyCreditsRateMin.isset() || EnemyCreditsRateMax.isset())
    {
        int const rate = pEnemy ? ComputeCreditsRate(pEnemy, CreditsRateWindow.Get(150)) : 0;
        BuildScalarDetail("RequiredEnemyCreditsRate", rate,
            EnemyCreditsRateMin, EnemyCreditsRateMax, LastCheckReport);
    }

    // ─── Global structure detection ─────────────────────────────────────
    if (!StructureOnMapTypes.empty()
        && (StructureOnMapMin.isset() || StructureOnMapMax.isset()))
    {
        int const count = CountStructuresOnMap(StructureOnMapTypes);
        BuildScalarDetail("RequiredStructureOnMap", count,
            StructureOnMapMin, StructureOnMapMax, LastCheckReport);
    }

    // ─── Dispatch cooldown (frames since this trigger last started) ─────
    if (Cooldown.isset())
    {
        int const since = (LastStartFrame < 0)
            ? 999999 : (Unsorted::CurrentFrame - LastStartFrame);
        Nullable<int> noMax;   // cooldown is a floor only
        BuildScalarDetail("RequiredCooldown", since,
            Cooldown, noMax, LastCheckReport);
    }

    // ─── Owner AI difficulty index (Hard=0, Normal=1, Easy=2) ───────────
    if (OwnerDifficultyMin.isset() || OwnerDifficultyMax.isset())
    {
        int const d = pOwner ? static_cast<int>(pOwner->GetAIDifficultyIndex()) : -1;
        BuildScalarDetail("RequiredOwnerDifficulty", d,
            OwnerDifficultyMin, OwnerDifficultyMax, LastCheckReport);
    }

    // ─── Deferred to follow-up ships ────────────────────────────────────
    // SuperWeapons: SWReadyGate has different API (ReadyMin/Max + frame math)
    // PowerOutput / TechLevel: verify field names first
    // Allies: multi-house walk (see CheckAllies for pattern)
    // Neutral: global neutral house lookup
}

// Small yes/true/1 parser for the tool's boolean debug sub-keys.
static bool ParseYesish(const char* s)
{
    return s && (_stricmp(s, "yes") == 0 || _stricmp(s, "true") == 0 || s[0] == '1');
}

// Scan the [Trigger.AIExt] section for "<root>.Debug.<subkey>" keys and build
// the GateDebug map. Generic so it captures every gate the tool emits without
// hardcoding each root.
void AITriggerTypeExt::ExtData::ParseGateDebug(CCINIClass* pINI, const char* section)
{
    GateDebug.clear();
    if (!pINI) return;

    int const keyCount = pINI->GetKeyCount(section);
    for (int i = 0; i < keyCount; ++i)
    {
        const char* key = pINI->GetKeyName(section, i);
        if (!key) continue;

        const char* marker = strstr(key, ".Debug.");
        if (!marker) continue;               // skips trigger-level DebugMessageDisplay.* and .DebugLog

        std::string root(key, static_cast<size_t>(marker - key));
        const char* sub = marker + 7;        // strlen(".Debug.")

        char buf[256];
        pINI->ReadString(section, key, "", buf, sizeof(buf));

        auto& q = GateDebug[root];
        if      (_stricmp(sub, "MessageDisplay") == 0) q.message_display  = buf;
        else if (_stricmp(sub, "LogMessage")     == 0) q.log_message      = buf;
        else if (_stricmp(sub, "DetailsTypes")   == 0) q.details_types    = buf;
        else if (_stricmp(sub, "ValueDisplay")   == 0) q.value_display    = ParseYesish(buf);
        else if (_stricmp(sub, "LogWrite")       == 0) q.log_write        = ParseYesish(buf);
        else if (_stricmp(sub, "DetailsDisplay") == 0) q.details_display  = ParseYesish(buf);
        // unknown sub-keys are ignored
    }
}

// Format one evaluated gate entry per the gate's DetailsTypes column list
// (e.g. "Type, Minimum, Maximum, Current"). Empty details_types = all columns.
// Result looks like "GATECH cur=0 min=1 max=-1".
static std::string FormatDetailLine(
    const AIExtCheckLine& L, const std::string& details_types)
{
    bool all = details_types.empty();
    bool wantType = all, wantMin = all, wantMax = all, wantCur = all;
    if (!all)
    {
        std::string lc;
        lc.reserve(details_types.size());
        for (char c : details_types) { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); lc.push_back(c); }
        wantType = lc.find("type")    != std::string::npos;
        wantMin  = lc.find("minimum") != std::string::npos;
        wantMax  = lc.find("maximum") != std::string::npos;
        wantCur  = lc.find("current") != std::string::npos;
    }

    std::string s;
    char buf[48];
    if (wantType) s += L.type_id.empty() ? "(scalar)" : L.type_id;
    if (wantCur) { snprintf(buf, sizeof(buf), " cur=%d", L.actual); s += buf; }
    if (wantMin) { snprintf(buf, sizeof(buf), " min=%d", L.min_v);  s += buf; }
    if (wantMax) { snprintf(buf, sizeof(buf), " max=%d", L.max_v);  s += buf; }
    if (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

// Emit the tool's per-gate debug quad for a lifecycle event.
// Log side: per gate with LogWrite/LogMessage, print its header then one
//   PASS/FAIL line per entry (columns per DetailsTypes).
// HUD side (Cancel only): per gate with a FAILING entry and MessageDisplay,
//   show the message (+ value when ValueDisplay=yes) — the "which gate blocked
//   this wave" signal, without the every-tick flood a Consider HUD would cause.
void AITriggerTypeExt::ExtData::EmitGateDebug(
    AITriggerTypeClass* pThis, HouseClass* pOwner, HouseClass* pEnemy,
    const char* label, bool overlay, bool doLog) const
{
    if (GateDebug.empty()) return;
    bool const isCancel = (strcmp(label, "Cancel") == 0);

    // Decide whether we actually need the (non-trivial) evaluation walk.
    bool needReport = false;
    for (auto const& kv : GateDebug)
    {
        auto const& q = kv.second;
        if (doLog && (q.log_write || !q.log_message.empty())) { needReport = true; break; }
        if (overlay && isCancel && !q.message_display.empty()) { needReport = true; break; }
    }
    if (!needReport) return;

    EvaluateAndReport(pOwner, pEnemy);

    for (auto const& kv : GateDebug)
    {
        const std::string&   root = kv.first;
        const AIExtGateDebug& q    = kv.second;

        // ── Log: header + per-entry PASS/FAIL, formatted per DetailsTypes ──
        if (doLog && (q.log_write || !q.log_message.empty()))
        {
            bool headed = false;
            for (auto const& L : LastCheckReport.lines)
            {
                if (L.root != root) continue;
                if (!headed && !q.log_message.empty())
                {
                    Debug::Log("[AIExt %s] %s %s\n",
                        label, pThis->ID, q.log_message.c_str());
                    headed = true;
                }
                Debug::Log("[AIExt %s] %s   %s %s %s\n",
                    label, pThis->ID, L.passed ? "PASS" : "FAIL",
                    root.c_str(), FormatDetailLine(L, q.details_types).c_str());
            }
        }

        // ── HUD: Cancel only, failing gates only ──
        if (overlay && isCancel && !q.message_display.empty())
        {
            std::string valsum;
            bool anyFail = false;
            for (auto const& L : LastCheckReport.lines)
            {
                if (L.root != root || L.passed) continue;
                anyFail = true;
                if (q.value_display)
                {
                    char buf[64];
                    if (L.type_id.empty()) snprintf(buf, sizeof(buf), "%s%d", valsum.empty() ? "" : " ", L.actual);
                    else                   snprintf(buf, sizeof(buf), "%s%s=%d", valsum.empty() ? "" : " ", L.type_id.c_str(), L.actual);
                    valsum += buf;
                }
            }
            if (anyFail)
            {
                std::wstring wmsg;
                if (const wchar_t* base = ResolveDebugText(q.message_display))
                    wmsg = base;
                if (q.value_display && !valsum.empty())
                    for (char c : valsum) wmsg.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
                if (!wmsg.empty())
                    MessageListClass::Instance.PrintMessage(wmsg.c_str());
            }
        }
    }
}
