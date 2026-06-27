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
        for (char* tok = strtok_s(raw, Phobos::readDelims, &ctx);
             tok;
             tok = strtok_s(nullptr, Phobos::readDelims, &ctx))
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
        for (char* tok = strtok_s(raw, Phobos::readDelims, &ctx);
             tok;
             tok = strtok_s(nullptr, Phobos::readDelims, &ctx))
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
        for (char* tok = strtok_s(raw, Phobos::readDelims, &ctx);
             tok;
             tok = strtok_s(nullptr, Phobos::readDelims, &ctx))
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
        for (char* tok = strtok_s(raw, Phobos::readDelims, &ctx);
             tok;
             tok = strtok_s(nullptr, Phobos::readDelims, &ctx))
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
        char buf[32] = {};
        if (exINI.ReadString(section, "TargetHouseMode", buf, sizeof(buf)))
        {
            if      (_stricmp(buf, "Any")            == 0) TargetHouseMode = AIExtTargetHouseMode::Any;
            else if (_stricmp(buf, "All")            == 0) TargetHouseMode = AIExtTargetHouseMode::All;
            else if (_stricmp(buf, "Most_Buildings") == 0) TargetHouseMode = AIExtTargetHouseMode::MostBuildings;
            else if (_stricmp(buf, "Least_Buildings")== 0) TargetHouseMode = AIExtTargetHouseMode::LeastBuildings;
            else if (_stricmp(buf, "Most_Units")     == 0) TargetHouseMode = AIExtTargetHouseMode::MostUnits;
            else if (_stricmp(buf, "Least_Units")    == 0) TargetHouseMode = AIExtTargetHouseMode::LeastUnits;
            else                                           TargetHouseMode = AIExtTargetHouseMode::Current;
        }

        if (exINI.ReadString(section, "RequiredAlliesMode", buf, sizeof(buf)))
        {
            if (_stricmp(buf, "All") == 0) AlliesMode = AIExtAlliesMode::All;
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

        for (int i = 0; i < HouseClass::Array->Count; ++i)
        {
            auto const pH = HouseClass::Array->Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->IsDefeated) continue;
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
        for (int i = 0; i < HouseClass::Array->Count; ++i)
        {
            auto const pH = HouseClass::Array->Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->IsDefeated) continue;
            if (pCallingHouse->IsAlliedWith(pH)) continue;
            if (CheckOneEnemy(pH)) return true;
        }
        return false; // No enemy satisfied
    }

    if (mode == AIExtTargetHouseMode::All)
    {
        // All enemy houses must satisfy all checks.
        bool foundAny = false;
        for (int i = 0; i < HouseClass::Array->Count; ++i)
        {
            auto const pH = HouseClass::Array->Items[i];
            if (!pH || pH == pCallingHouse) continue;
            if (pH->IsNeutral()) continue;
            if (pH->IsDefeated) continue;
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

    for (int i = 0; i < HouseClass::Array->Count; ++i)
    {
        auto const pH = HouseClass::Array->Items[i];
        if (!pH || pH == pCallingHouse) continue;
        if (pH->IsNeutral()) continue;
        if (pH->IsDefeated) continue;
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
    for (int i = 0; i < HouseClass::Array->Count; ++i)
    {
        auto const pH = HouseClass::Array->Items[i];
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
// Container boilerplate
// ============================================================================

AITriggerTypeExt::ExtContainer::ExtContainer()
    : Container("AITriggerTypeClass")
{ }

AITriggerTypeExt::ExtContainer::~ExtContainer() = default;

// ============================================================================
// Container hooks
// ============================================================================

// CTOR — confirmed address 0x41E350
// Allocates ExtData for every newly constructed AITriggerTypeClass.
// Hook size 5: verify first instruction at 0x41E350 covers >= 5 bytes.
// Typically the function prolog (PUSH EBP / MOV EBP, ESP) covers exactly 5.
DEFINE_HOOK(0x41E350, AITriggerTypeClass_CTOR, 0x5)
{
    GET(AITriggerTypeClass*, pItem, ECX);
    AITriggerTypeExt::ExtMap.TryAllocate(pItem);
    return 0;
}

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
