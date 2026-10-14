# Setup: Effect Engine in Unreal Engine (C++)

Step-by-step to link the Effect Engine into an Unreal game. Read
[`game_engine_integration.md`](game_engine_integration.md) first for the concepts (the
mental model, the five rules, the dry run) — this file is only the wiring.

**How Unreal connects:** Unreal is C++, so you include the header and call the library
directly — no wrapper language in between. You just tell Unreal's build system where the
library is. We expose a simple "subsystem" so the rest of your game can use it.

## Quick Start

1. **Build the library** (see the main guide's build section) to get the library file
   (`.dll`+`.lib` / `.dylib` / `.so`) and the header.
2. **Create the ThirdParty folder** in your module using the layout in §1, and copy the
   `include/` header and the platform `lib/` files into it.
3. **Edit your module's `Build.cs`** with the block from §2 so Unreal finds the header,
   links the library, and ships it alongside the packaged game (`RuntimeDependencies`).
4. **Add the subsystem** from §3 (`EffectEngineSubsystem.h`) to your module.
5. **Regenerate project files** (right-click the `.uproject` → *Generate Visual
   Studio/Xcode project files*) and **build** the project.
6. **Ship your config JSON** somewhere readable at runtime (e.g. under `Content/` or your
   project's `Config/`), and pass its absolute path to `LoadConfig`.
7. **Call it from game code:** `GetGameInstance()->GetSubsystem<UEffectEngineSubsystem>()`,
   then `LoadConfig(Path)` once, and `BrewAndCraft()` when you need a result.

If it fails to link, double-check the library paths in `Build.cs` (step 3); if it builds
but crashes at load, the runtime library isn't next to the binary — confirm the
`RuntimeDependencies` line.

## 1. Where the files go

```
Source/MyGame/ThirdParty/EffectEngine/
  include/effect_engine/effect_engine_c.h
  lib/
    Mac/libeffect_engine_c.dylib
    Win64/effect_engine_c.lib
    Win64/effect_engine_c.dll
    Linux/libeffect_engine_c.so
```

## 2. Point the build system at it (`Build.cs`)

```csharp
using System.IO;
using UnrealBuildTool;

public class MyGame : ModuleRules {
    public MyGame(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });

        string TP = Path.Combine(ModuleDirectory, "ThirdParty", "EffectEngine");
        PublicIncludePaths.Add(Path.Combine(TP, "include"));

        if (Target.Platform == UnrealTargetPlatform.Mac) {
            string dylib = Path.Combine(TP, "lib", "Mac", "libeffect_engine_c.dylib");
            PublicAdditionalLibraries.Add(dylib);
            RuntimeDependencies.Add(dylib);                 // ship it next to the game
        } else if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicAdditionalLibraries.Add(Path.Combine(TP, "lib", "Win64", "effect_engine_c.lib"));
            RuntimeDependencies.Add("$(BinaryOutputDir)/effect_engine_c.dll",
                                    Path.Combine(TP, "lib", "Win64", "effect_engine_c.dll"));
        }
        // Linux: mirror the Mac branch with the .so file.
    }
}
```

## 3. A subsystem you can call from anywhere

```cpp
// EffectEngineSubsystem.h
#pragma once
#include "Subsystems/GameInstanceSubsystem.h"
#include "effect_engine/effect_engine_c.h"
#include "EffectEngineSubsystem.generated.h"

UCLASS()
class UEffectEngineSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()
    EffectEngine* Engine = nullptr;
public:
    bool LoadConfig(const FString& Path) {
        Engine = effect_engine_load(TCHAR_TO_UTF8(*Path));   // FString → C string
        if (!Engine)
            UE_LOG(LogTemp, Error, TEXT("Effect engine load failed: %s"),
                   UTF8_TO_TCHAR(effect_engine_last_error()));
        return Engine != nullptr;
    }

    virtual void Deinitialize() override {
        if (Engine) { effect_engine_destroy(Engine); Engine = nullptr; }
    }

    // Mix two example ingredients and craft the result. Returns "" for a null effect.
    FString BrewAndCraft() {
        EEIngredient* cham   = effect_engine_get_source(Engine, "chamomile", "standard");
        EEIngredient* ginger = effect_engine_get_source(Engine, "ginger", "standard");
        EEIngredient* brew   = effect_engine_mix(Engine, cham, ginger, "add", 30);

        FString Result;
        if (EECrafted* Crafted = effect_engine_craft(Engine, brew)) {  // Rule 4
            Result = UTF8_TO_TCHAR(effect_engine_crafted_effect_id(Crafted));
            effect_engine_crafted_free(Crafted);                       // Rule 1
        } else {
            UE_LOG(LogTemp, Warning, TEXT("craft failed: %s"),
                   UTF8_TO_TCHAR(effect_engine_last_error()));
        }

        // Give every handle back.
        effect_engine_ingredient_free(brew);
        effect_engine_ingredient_free(ginger);
        effect_engine_ingredient_free(cham);
        return Result;   // "" means dead zone (Rule 5)
    }
};
```

Call `LoadConfig` once (e.g. on `Initialize`), then `BrewAndCraft()` wherever you need it.
Use `TCHAR_TO_UTF8` when passing strings in and `UTF8_TO_TCHAR` when reading strings out.

## 4. Verify it works (known-good smoke test)

Before wiring the engine into real gameplay, run this exact check against the **example
config** that ships with the project (`tea_alchemist_config-v1.0.0.json`). It uses a recipe
whose result is known, so a matching log proves the whole chain — library linked, config
parsed, math running — is healthy.

1. Ship `tea_alchemist_config-v1.0.0.json` somewhere readable at runtime and pass its
   absolute path to `LoadConfig`.
2. Add this method to the subsystem and call it once after `LoadConfig`:

```cpp
void Verify() {
    EEIngredient* cham = effect_engine_get_source(Engine, "chamomile", "standard");
    EEIngredient* gin  = effect_engine_get_source(Engine, "ginger", "standard");
    EEIngredient* brew = effect_engine_mix(Engine, cham, gin, "subtract", 30);  // known-good recipe

    EECrafted* c = effect_engine_craft(Engine, brew);
    UE_LOG(LogTemp, Display, TEXT("effect: %s"),
           UTF8_TO_TCHAR(effect_engine_crafted_effect_id(c)));
    for (int i = 0; i < effect_engine_crafted_active_domain_count(c); ++i)
        UE_LOG(LogTemp, Display, TEXT("  %s: %f"),
               UTF8_TO_TCHAR(effect_engine_crafted_active_domain_id(c, i)),
               effect_engine_crafted_active_domain_distance(c, i));

    effect_engine_crafted_free(c);
    effect_engine_ingredient_free(brew);
    effect_engine_ingredient_free(gin);
    effect_engine_ingredient_free(cham);
}
```

**✅ If the library is working, the Output Log prints:**

```
LogTemp: effect: calming_brew
LogTemp:   leaf: 1.414214
```

The effect id must be **`calming_brew`** and the single active domain **`leaf`** at a
distance of **≈ 1.41** (that's √2 — trailing digits may differ slightly). If you get that,
everything from `Build.cs` to the C library to the config is wired correctly. (Note: the
§3 `BrewAndCraft` used `"add"` with brew time 30, which lands in a *dead zone* — a valid
empty result, Rule 5. This check uses `"subtract"` so you get an unmistakable named effect.)

## 5. Quick function reference (C API)

Unreal calls the C library directly — no wrapper. These are the functions from
`effect_engine/effect_engine_c.h` you'll use most. **You own every handle** (`EEIngredient*`,
`EECrafted*`, `EffectEngine*`) and must free it with the matching function (Rule 1).

**Lifecycle & errors:**

| Function | Parameters | Returns |
|---|---|---|
| `effect_engine_load(config_path)` | `const char*` — path to your config JSON (`TCHAR_TO_UTF8(*Path)`) | `EffectEngine*`, or `NULL` on failure |
| `effect_engine_destroy(engine)` | `EffectEngine*` | — (call last, at shutdown) |
| `effect_engine_last_error()` | — | `const char*` — reason for the last failure, `""` if none (borrowed — don't free, copy if keeping) |

**Ingredients** (free each with `effect_engine_ingredient_free`):

| Function | Parameters | Returns |
|---|---|---|
| `effect_engine_get_source(engine, source_id, tier)` | `EffectEngine*`, `const char* source_id`, `const char* tier` | `EEIngredient*`, or `NULL` |
| `effect_engine_mix(engine, a, b, operation_id, elapsed_time)` | `EffectEngine*`, `const EEIngredient* a`, `const EEIngredient* b`, `const char* operation_id` (`"add"`/`"subtract"`/`"multiply"`), `int elapsed_time` | `EEIngredient*` (mysterious object), or `NULL` |
| `effect_engine_alter(engine, target, alter_object_id)` | `EffectEngine*`, `const EEIngredient* target`, `const char* alter_object_id` | `EEIngredient*`, or `NULL` |
| `effect_engine_ingredient_free(ingredient)` | `EEIngredient*` | — |

**Craft** (free the result with `effect_engine_crafted_free`):

| Function | Parameters | Returns |
|---|---|---|
| `effect_engine_craft(engine, mysterious)` | `EffectEngine*`, `const EEIngredient* mysterious` — must be a `mix`/`alter` result, not a raw source (Rule 4) | `EECrafted*`, or `NULL` |
| `effect_engine_crafted_effect_id(crafted)` | `const EECrafted*` | `const char*` — effect id, or `""` for a **null effect** (dead zone, Rule 5). Borrowed — don't free. |
| `effect_engine_crafted_active_domain_count(crafted)` | `const EECrafted*` | `int` — number of active domains (0 for a null effect) |
| `effect_engine_crafted_active_domain_id(crafted, index)` | `const EECrafted*`, `int index` | `const char*` — domain name (borrowed) |
| `effect_engine_crafted_active_domain_distance(crafted, index)` | `const EECrafted*`, `int index` | `float` — distance from the effect anchor |
| `effect_engine_crafted_free(crafted)` | `EECrafted*` | — |

Any function returning a handle gives `NULL` on failure; int accessors return `-1`. Either
way, call `effect_engine_last_error()` **immediately** (it's overwritten by the next call).
The header lists a few more query/inspection functions (`effect_engine_source_count`,
`effect_engine_alter_exponents`, etc.) with the same conventions.
