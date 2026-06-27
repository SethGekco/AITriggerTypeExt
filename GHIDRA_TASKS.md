# AITriggerTypeExt — Ghidra Task List

## All critical hooks confirmed ✅

Every hook needed for correct fresh-game operation is now confirmed and active.

| Hook | Address | Size | Status |
|------|---------|------|--------|
| CTOR | 0x41E350 | 5 | ✅ Active |
| ConditionMet gate | 0x41EAC0 | 5 | ✅ Active |
| LoadFromINI per-item | 0x41F39F | 0xA | ✅ Active (Body.cpp) |
| IPersistStream::Load prefix | 0x41E540 | 5 | ✅ Active |
| IPersistStream::Load suffix | 0x41E5B2 | 6 | ✅ Active |
| IPersistStream::Save prefix | 0x41E5C0 | 8 | ✅ Active |
| IPersistStream::Save suffix | 0x41E5D4 | 6 | ✅ Active |
| DTOR | 0x???????? | ? | ⏳ Deferred |

**The system is ready to build and test.**

---

## Remaining: DTOR hook (low priority, deferred)

AITriggerTypeClass objects are only destroyed on scenario unload, not
mid-game. Stale ExtMap entries don't affect gate logic correctness. This
hook is only needed if you observe memory growth across many scenario loads
in a single session.

When you want to find it: the destructor is not in the IPersistStream
vtable (slot 0 of 0x007E2A50 is QueryInterface). It will be called from
the DynamicVectorClass cleanup path when the Array is cleared on unload.

Set a write breakpoint on AITriggerTypeClass::Array->Count field at
0xA8B208+0x8 = 0xA8B210, then unload a scenario. Walk the call stack
to find what calls delete on AITriggerTypeClass*.
