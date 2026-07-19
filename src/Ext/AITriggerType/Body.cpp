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
        EnemyTechLevelMin.isset()  || EnemyTechLevelMax.isset();

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
    if (!CheckAllies(pCallingHouse))              return false;
    if (!CheckNeutral())                          return false;
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
