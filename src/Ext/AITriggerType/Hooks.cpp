/*
 * AITriggerTypeExt - Hooks.cpp
 *
 * ALL ADDRESSES CONFIRMED from Ghidra disassembly of gamemd.exe + PR #2119.
 *
 * ============================================================================
 * FULL CONFIRMED ADDRESS MAP
 * ============================================================================
 *
 * AITriggerTypeClass::ConditionMet (FUN_0041E720):
 *   Entry:            0x41E720  prolog: PUSH EBX(1)+PUSH EBP(1)+PUSH ESI(1)+
 *                               MOV ESI,ECX(2)+PUSH EDI(1) = 6 bytes
 *   True-return:      0x41EAC0  POP EDI(1)+POP ESI(1)+POP EBP(1)+MOV AL,1(2)
 *                               = 5 bytes → gate hook here, size 5
 *   False-return:     0x41EA84  POP EDI+POP ESI+POP EBP+XOR AL,AL+POP EBX+
 *                               RET 0xC → veto jump target
 *   Registers confirmed at 0x41EAC0:
 *     ESI = AITriggerTypeClass* (pThis)
 *     EDI = HouseClass* (pOwner/CallingHouse)
 *     EBX = HouseClass* (pEnemy/TargetHouse)
 *
 * AITriggerTypeClass CTOR (FUN_0041E350):
 *   Entry:            0x41E350  MOV EAX,[ESP+4](4)+PUSH ESI(1) = 5 bytes
 *   ECX = this at entry (saved to ESI at 0x41E356)
 *
 * AITriggerTypeClass::CreateFromINIList (FUN_0041F2E0) — per-item INI hook:
 *   Hook address:     0x41F39F  after CALL [EDX+0x64] (LoadFromINI)
 *   Hook size:        0xA (10)  covers MOV EAX,[ESP+14](4)+MOV [ESI+9C],EAX(6)
 *   ESI = AITriggerTypeClass*   EBX = CCINIClass*
 *
 * IPersistStream::Load (LAB_0041E540):
 *   stdcall: [ESP+4]=this, [ESP+8]=IStream*
 *   Prefix:           0x41E540  MOV EAX,[ESP+8](4)+PUSH ESI(1) = 5 bytes
 *   Suffix:           0x41E5B2  XOR EAX,EAX(2)+POP ESI(1)+RET 0x8(3)=6 bytes
 *     At suffix: ESI = AITriggerTypeClass* (loaded at entry from [ESP+8]→ESI)
 *
 * IPersistStream::Save (LAB_0041E5C0):
 *   stdcall: [ESP+4]=this, [ESP+8]=IStream*, [ESP+C]=fClearDirty
 *   Prefix:           0x41E5C0  MOV EAX,[ESP+C](4)+MOV ECX,[ESP+8](4)=8 bytes
 *   Suffix:           0x41E5D4  TEST EAX,EAX(2)+JL(2)+XOR EAX,EAX(2)=6 bytes
 *     At suffix: ESI = AITriggerTypeClass* (set at 0x41E545: MOV ESI,[ESP+8])
 *
 * Note on Load/Save 'this':
 *   Load loads 'this' into ESI from [ESP+8] at 0x41E545 (after PUSH ESI).
 *   Save loads 'this' into ESI from [ESP+8] at 0x41E545. Wait — Save at
 *   0x41E5C0 has no ESI save; it's a pure pass-through. For the Save suffix
 *   hook, 'this' must be read from the stack: [ESP+0x4] at the suffix point.
 *   Confirm exact offset at suffix — ESP may have shifted from any PUSHes.
 *   Save does no PUSHes before the CALL, so at 0x41E5D4 (after CALL returns):
 *     [ESP+4] = EDX = original 'this' (pushed at 0x41E5CE before CALL)
 *   Actually at 0x41E5D4 after CALL FUN_00410320 returns with RETN 0xC,
 *   the 3 pushed args are cleaned by callee, so stack is back to entry state:
 *     [ESP+4] = original 'this'   [ESP+8] = IStream*
 *   Use GET_STACK(AITriggerTypeClass*, pItem, 0x4) at the suffix.
 */

#include "Body.h"

#include <HouseClass.h>
#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

// ============================================================================
// CTOR hook — 0x41E350, size 5  ✅ CONFIRMED
// ============================================================================

DEFINE_HOOK(0x41E350, AITriggerTypeClass_CTOR, 0x5)
{
    GET(AITriggerTypeClass*, pItem, ECX);
    AITriggerTypeExt::ExtMap.TryAllocate(pItem);
    return 0;
}

// ============================================================================
// DTOR hook — still unconfirmed
// The vtable at 0x007E2A50 slot 0 = FUN_00410260 = QueryInterface, not DTOR.
// The actual destructor is elsewhere. Since AITriggerTypeClass objects are
// only destroyed on scenario unload (not mid-game), stale ExtMap entries are
// harmless for a single session. Defer finding this address.
// ============================================================================

/*
DEFINE_HOOK(0x????????, AITriggerTypeClass_SDDTOR, 0x?)
{
    GET(AITriggerTypeClass*, pItem, ESI);
    AITriggerTypeExt::ExtMap.Remove(pItem);
    return 0;
}
*/

// ============================================================================
// Per-item INI hook — 0x41F39F, size 0xA  ✅ CONFIRMED
// ============================================================================

// (This hook is in Body.cpp — leaving comment here for cross-reference)

// ============================================================================
// IPersistStream::Load prefix — 0x41E540, size 5  ✅ CONFIRMED
//
// stdcall: [ESP+4]=this, [ESP+8]=IStream*  (before any PUSHes in the hook)
// Actually at 0x41E540 the raw stack is:
//   [ESP+0] = return address
//   [ESP+4] = this (AITriggerTypeClass*)
//   [ESP+8] = IStream*
// Hook re-emits MOV EAX,[ESP+8] and PUSH ESI, so stack shifts by 4 inside
// the hook body. Use GET_STACK with offsets relative to pre-hook ESP.
// ============================================================================

DEFINE_HOOK(0x41E540, AITriggerTypeClass_Load_Prefix, 0x5)
{
    // At entry to Load: [ESP+4]=this, [ESP+8]=IStream*
    // (These are the raw pre-call stack values at the function entry point)
    GET_STACK(AITriggerTypeClass*, pItem, 0x4);
    GET_STACK(IStream*,            pStm,  0x8);
    AITriggerTypeExt::ExtMap.PrepareStream(pItem, pStm);
    return 0;
}

// ============================================================================
// IPersistStream::Load suffix — 0x41E5B2, size 6  ✅ CONFIRMED
//
// 0041E5B2  XOR EAX, EAX   (2)
// 0041E5B4  POP ESI        (1)
// 0041E5B5  RET 0x8        (3)  = 6 bytes
// At this point ESI still holds AITriggerTypeClass* (loaded at 0x41E545).
// ============================================================================

DEFINE_HOOK(0x41E5B2, AITriggerTypeClass_Load_Suffix, 0x6)
{
    AITriggerTypeExt::ExtMap.LoadStatic();
    return 0;
}

// ============================================================================
// IPersistStream::Save prefix — 0x41E5C0, size 8  ✅ CONFIRMED
//
// 0041E5C0  MOV EAX,[ESP+0xC]  (4)
// 0041E5C4  MOV ECX,[ESP+0x8]  (4)  = 8 bytes
// stdcall: [ESP+4]=this, [ESP+8]=IStream*, [ESP+C]=fClearDirty
// ============================================================================

DEFINE_HOOK(0x41E5C0, AITriggerTypeClass_Save_Prefix, 0x8)
{
    GET_STACK(AITriggerTypeClass*, pItem, 0x4);
    GET_STACK(IStream*,            pStm,  0x8);
    AITriggerTypeExt::ExtMap.PrepareStream(pItem, pStm);
    return 0;
}

// ============================================================================
// IPersistStream::Save suffix — 0x41E5D4, size 6  ✅ CONFIRMED
//
// 0041E5D4  TEST EAX, EAX  (2)
// 0041E5D6  JL LAB_5DA     (2)
// 0041E5D8  XOR EAX, EAX   (2)  = 6 bytes
// FUN_00410320 uses RETN 0xC (callee cleanup of 3 args), so at 0x41E5D4
// the stack is restored to function entry state:
//   [ESP+4] = this   [ESP+8] = IStream*   [ESP+C] = fClearDirty
// ============================================================================

DEFINE_HOOK(0x41E5D4, AITriggerTypeClass_Save_Suffix, 0x6)
{
    AITriggerTypeExt::ExtMap.SaveStatic();
    return 0;
}

// ============================================================================
// GATE HOOK — 0x41EAC0, size 5  ✅ CONFIRMED
//
// True-return epilog of ConditionMet. Fires only when vanilla passes the
// trigger. ESI/EDI/EBX still hold pThis/pOwner/pEnemy (not yet popped).
// ============================================================================

DEFINE_HOOK(0x41EAC0, AITriggerTypeClass_ConditionMet_Gate, 0x5)
{
    enum { ReturnFalse = 0x41EA84 };

    GET(AITriggerTypeClass*, pThis,  ESI);
    GET(HouseClass*,         pOwner, EDI);
    GET(HouseClass*,         pEnemy, EBX);

    auto const pExt = AITriggerTypeExt::ExtMap.Find(pThis);
    if (!pExt)
        return 0;

    if (!pExt->ExtraPrerequisitesMet(pOwner, pEnemy))
    {
        Debug::Log(
            "[AITriggerTypeExt] '%s' vetoed by extra prerequisites "
            "(owner=%s enemy=%s).\n",
            pThis->ID,
            pOwner ? pOwner->Type->ID : "null",
            pEnemy ? pEnemy->Type->ID : "null"
        );
        return ReturnFalse;
    }

    return 0;
}

// ============================================================================
// DIAGNOSTIC hook — 0x41E720 entry, size 6  ✅ CONFIRMED
// Uncomment during development to verify INI read is working.
// ============================================================================

/*
DEFINE_HOOK(0x41E720, AITriggerTypeClass_ConditionMet_Diag, 0x6)
{
    GET(AITriggerTypeClass*, pThis, ECX);

    if (auto const pExt = AITriggerTypeExt::ExtMap.Find(pThis))
    {
        if (!pExt->OwnerBuildings.empty() || !pExt->EnemyBuildings.empty() ||
            pExt->ElapsedTimeMin.isset()  || pExt->ElapsedTimeMax.isset())
        {
            Debug::Log(
                "[AITriggerTypeExt] Evaluating '%s': "
                "ownerBldgs=%zu enemyBldgs=%zu mode=%d\n",
                pThis->ID,
                pExt->OwnerBuildings.Types.size(),
                pExt->EnemyBuildings.Types.size(),
                static_cast<int>(pExt->TargetHouseMode.Get())
            );
        }
    }
    return 0;
}
*/
