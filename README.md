# AITriggerTypeExt

A Syringe-injected DLL for Red Alert 2: Yuri's Revenge that extends `AITriggerTypeClass` with comprehensive per-trigger prerequisite gates.

## What it does

Allows modders to attach a `[TriggerID.AIExt]` sidecar section to any AI trigger, specifying buildings, units, superweapons, economy, and power conditions that must be met before the trigger is eligible to fire — for the AI itself, its enemies, its allies, and the neutral house.

See [INI_REFERENCE.md](INI_REFERENCE.md) for the complete tag documentation.

## Dependencies

- [Phobos-developers/YRpp](https://github.com/Phobos-developers/YRpp) (phobos-dev branch) — YR C++ headers
- [Phobos-developers/Phobos](https://github.com/Phobos-developers/Phobos) (develop branch) — Extension/Container/Stream utilities

Both are included as git submodules.

## Building

```bash
git clone --recursive https://github.com/SethGekco/AITriggerTypeExt.git
```

Then open `AITriggerTypeExt.sln` in Visual Studio 2019+ and build the `DevBuild|x86` configuration. Or push to `main` and let GitHub Actions build it.

## Injection

Load alongside Ares and Phobos via Syringe:

```
Syringe.exe "gamemd.exe" -DLL=Ares.dll -DLL=Phobos.dll -DLL=AITriggerTypeExt.dll
```

Or via your `wine-game.sh` script with `-i=AITriggerTypeExt.dll`.

## Status

- ✅ CTOR hook (0x41E350) — allocates ExtData per trigger
- ✅ INI read hook (0x41F39F) — reads `[TriggerID.AIExt]` sections
- ✅ Gate hook (0x41EAC0) — applies prerequisites at ConditionMet true-return
- ✅ Save/Load hooks (0x41E540/0x41E5B2/0x41E5C0/0x41E5D4) — save game persistence
- ⏳ DTOR hook — deferred (no correctness impact in single session)
