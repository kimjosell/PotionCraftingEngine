# Setup: Effect Engine in Godot 4 (macOS/Windows/Linux)

Step-by-step to link the Effect Engine into a Godot game. Read
[`game_engine_integration.md`](game_engine_integration.md) first for the concepts (the
mental model, the five rules, the dry run) — this file is only the wiring.

**How Godot connects:** GDScript can't call a C library directly. So you write a tiny
**GDExtension** in C++ — using the same [`godot-cpp`](https://github.com/godotengine/godot-cpp)
setup you already got working — that forwards GDScript calls to our library. The payoff:
we wrap each handle in a Godot `RefCounted` object, so when your GDScript variable goes
out of scope, Godot frees the handle for you. No manual cleanup.

You've already built the godot-cpp example once, so you have the pieces that matter:

- a **`godot-cpp/`** folder (the bindings submodule),
- a **`src/`** folder with your C++ and a `register_types.cpp`,
- a **`SConstruct`** build file,
- a **demo Godot project** whose `bin/` folder receives the compiled extension.

This guide adds *our library* to that exact project. Two things trip everyone up, and we
spell both out: (a) telling SConstruct where our header and `.dylib` live (step 2), and
(b) registering the new classes so Godot can see them (step 4).

## Quick Start — the whole path

Skim this once, then follow the numbered sections.

1. **Build our library** (see the main guide's build section) → you get
   `libeffect_engine_c.dylib` (or `.dll`/`.so`) and the header `effect_engine_c.h`.
2. **Drop those two into your godot-cpp project** under `libs/effect_engine/` (§1).
3. **Teach SConstruct about them** — a few lines for include path, link, and (macOS) rpath (§2).
4. **Add the wrapper classes** (`ee_wrapper.h`) to `src/` (§3).
5. **Register the classes** in `register_types.cpp` (§4).
6. **Build with `scons`** — the compiled extension lands in your demo project's `bin/`.
7. **Put our `.dylib` where the extension can load it** (macOS: *inside* the `.framework`),
   and drop your config JSON into the project. Reuse the example's `.gdextension` (§5).
8. **Reload the project**, write the GDScript from §6 in a node's `_ready()`, press Play.

**Sanity check after step 6:** in the Godot editor, type `EffectEngineGD` in a script. If
autocomplete knows it, the extension loaded. If not, see Troubleshooting.

## 1. Put our library inside your godot-cpp project

Copy the built library and header into the godot-cpp project you already have. A `libs/`
folder keeps it out of the way:

```
your-godot-cpp-project/
  godot-cpp/                       # (already here) the bindings submodule
  src/
    register_types.cpp            # (already here) you'll edit this in §4
    ee_wrapper.h                  # NEW — the classes from §3
  libs/
    effect_engine/                 # NEW — our library lives here
      include/effect_engine/effect_engine_c.h
      libeffect_engine_c.dylib     # (or .dll+.lib / .so, per platform)
  SConstruct                       # you'll edit this in §2
  demo/                            # (already here) your test Godot project
    bin/                           # compiled extension lands here
```

## 2. Teach the SConstruct build about our library

This is the step that stops most people. The godot-cpp example's `SConstruct` compiles
`src/`, but it knows nothing about *our* header or `.dylib`. You have to tell it three
things: where the header is, where the library is, and its name.

Open `SConstruct`. The example ends with something like this — find these two lines:

```python
sources = Glob("src/*.cpp")
# ...
library = env.SharedLibrary("demo/bin/libgdexample...", source=sources)
```

Just **before** the `env.SharedLibrary(...)` line, add:

```python
# --- Effect Engine library ---
env.Append(CPPPATH=["libs/effect_engine/include"])   # so #include "effect_engine/..." resolves
env.Append(LIBPATH=["libs/effect_engine"])           # where the .dylib/.so/.lib lives
env.Append(LIBS=["effect_engine_c"])                 # the name, WITHOUT "lib" prefix or extension

# macOS ONLY: required, not optional. Our .dylib's install-name is @rpath/…, and
# godot-cpp's macOS build adds NO rpath by default — so at load time @rpath resolves to
# nothing and Godot crashes on startup. This tells the extension "look for the .dylib in
# my own folder" (@loader_path = the binary's directory).
if env["platform"] == "macos":
    env.Append(LINKFLAGS=["-Wl,-rpath,@loader_path"])
```

In plain terms:

- **CPPPATH** = the include search path, so `#include "effect_engine/effect_engine_c.h"`
  in the wrapper finds the file.
- **LIBPATH** + **LIBS** = "link against this library." The name is `effect_engine_c`
  (drop the `lib` prefix and the `.dylib`/`.so`/`.a` extension — the linker adds those).
- **LINKFLAGS rpath (macOS)** = where to *find* the `.dylib` at runtime. Without it Godot
  links fine but **crashes the moment it loads the extension**. Verify with
  `otool -l <binary> | grep -A2 LC_RPATH` — you should see `@loader_path`.

> **Why this bites on macOS:** the extension isn't a plain `.dylib`, it's a `.framework`
> bundle, and the actual binary lives *inside* it. `@loader_path` points at that inner
> folder — so our `libeffect_engine_c.dylib` must sit right there next to the binary,
> **inside the `.framework`**. §5 shows how to keep it there across rebuilds.

## 3. The wrapper classes

Create `src/ee_wrapper.h` with the three classes below: one for ingredients, one for
craft results, one for the engine. Each frees its handle automatically in its destructor,
so GDScript never has to.

```cpp
#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "effect_engine/effect_engine_c.h"

using namespace godot;

// An ingredient (source or mysterious object).
class EEIngredientRef : public RefCounted {
    GDCLASS(EEIngredientRef, RefCounted)
public:
    EEIngredient *handle = nullptr;
    ~EEIngredientRef() { if (handle) effect_engine_ingredient_free(handle); } // Rule 1: freed for you

    // The raw coordinates, as a flat array [d0x,d0y,d0z, d1x,d1y,d1z, ...].
    PackedInt32Array get_dimensions() const {
        PackedInt32Array out;
        int n = effect_engine_ingredient_domain_count(handle) * 3;
        out.resize(n);
        effect_engine_ingredient_dimensions(handle, out.ptrw(), n);
        return out;
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_dimensions"), &EEIngredientRef::get_dimensions);
    }
};

// A craft result.
class EECraftedRef : public RefCounted {
    GDCLASS(EECraftedRef, RefCounted)
public:
    EECrafted *handle = nullptr;
    ~EECraftedRef() { if (handle) effect_engine_crafted_free(handle); }

    String get_effect_id() const {                       // "" means null effect (Rule 5)
        return String(effect_engine_crafted_effect_id(handle));
    }
    Dictionary get_active_domains() const {              // { domain_name: distance }
        Dictionary d;
        int n = effect_engine_crafted_active_domain_count(handle);
        for (int i = 0; i < n; ++i)
            d[String(effect_engine_crafted_active_domain_id(handle, i))] =
                effect_engine_crafted_active_domain_distance(handle, i);
        return d;
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_effect_id"), &EECraftedRef::get_effect_id);
        ClassDB::bind_method(D_METHOD("get_active_domains"), &EECraftedRef::get_active_domains);
    }
};

// The engine.
class EffectEngineGD : public RefCounted {
    GDCLASS(EffectEngineGD, RefCounted)
    EffectEngine *engine = nullptr;
public:
    ~EffectEngineGD() { if (engine) effect_engine_destroy(engine); }

    bool load(const String &path) {
        engine = effect_engine_load(path.utf8().get_data());
        return engine != nullptr;                        // false → call last_error()
    }
    String last_error() const { return String(effect_engine_last_error()); }

    Ref<EEIngredientRef> get_source(const String &id, const String &tier) {
        EEIngredient *h = effect_engine_get_source(engine, id.utf8().get_data(),
                                                   tier.utf8().get_data());
        if (!h) return Ref<EEIngredientRef>();           // null → check last_error()
        Ref<EEIngredientRef> r; r.instantiate(); r->handle = h;
        return r;
    }
    Ref<EEIngredientRef> mix(const Ref<EEIngredientRef> &a, const Ref<EEIngredientRef> &b,
                             const String &op, int brew_time) {
        EEIngredient *h = effect_engine_mix(engine, a->handle, b->handle,
                                            op.utf8().get_data(), brew_time);
        if (!h) return Ref<EEIngredientRef>();
        Ref<EEIngredientRef> r; r.instantiate(); r->handle = h;
        return r;
    }
    // Transform one object with a named "alter object" from your JSON config.
    Ref<EEIngredientRef> alter(const Ref<EEIngredientRef> &target, const String &alter_id) {
        EEIngredient *h = effect_engine_alter(engine, target->handle,
                                              alter_id.utf8().get_data());
        if (!h) return Ref<EEIngredientRef>();           // null → check last_error()
        Ref<EEIngredientRef> r; r.instantiate(); r->handle = h;
        return r;
    }
    Ref<EECraftedRef> craft(const Ref<EEIngredientRef> &mysterious) {
        EECrafted *h = effect_engine_craft(engine, mysterious->handle);
        if (!h) return Ref<EECraftedRef>();
        Ref<EECraftedRef> r; r.instantiate(); r->handle = h;
        return r;
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("load", "path"), &EffectEngineGD::load);
        ClassDB::bind_method(D_METHOD("last_error"), &EffectEngineGD::last_error);
        ClassDB::bind_method(D_METHOD("get_source", "id", "tier"), &EffectEngineGD::get_source);
        ClassDB::bind_method(D_METHOD("mix", "a", "b", "op", "brew_time"), &EffectEngineGD::mix);
        ClassDB::bind_method(D_METHOD("alter", "target", "alter_id"), &EffectEngineGD::alter);
        ClassDB::bind_method(D_METHOD("craft", "mysterious"), &EffectEngineGD::craft);
    }
};
```

That's a complete, copy-paste-ready wrapper: `get_source`, `mix`, `alter`, and `craft`
cover the whole craft flow, and every id/tier/operation/alter name it takes comes from
**your own JSON config** — the C++ never hard-codes content. To drive it in your game you
only author that JSON; you never touch this file again.

(The config-listing functions from the header — `effect_engine_source_count`,
`effect_engine_alter_object_id`, etc. — follow the exact same pattern if you later want to
populate UI menus from the config.)

## 4. Register the classes so Godot can see them

Godot only knows about classes you register. Open `src/register_types.cpp` — the same file
where the godot-cpp example registers *its* class — and add our three, right next to the
existing `GDREGISTER_RUNTIME_CLASS(...)` line:

```cpp
#include "ee_wrapper.h"   // add near the top, with the other includes

// ...inside the module's initialize function, next to the example's registration:
void initialize_example_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    GDREGISTER_RUNTIME_CLASS(EEIngredientRef);   // add
    GDREGISTER_RUNTIME_CLASS(EECraftedRef);      // add
    GDREGISTER_RUNTIME_CLASS(EffectEngineGD);    // add — this is the one GDScript uses
    // ...the example's own GDREGISTER_RUNTIME_CLASS(Example) stays here too
}
```

The function may be named differently in your project — add the three registrations
wherever the example registers its class. Then build:

```bash
scons            # from the project root; add platform=macos / target=template_debug if you set those before
```

A successful build writes your compiled extension into `demo/bin/`.

## 5. Put the files into the Godot project

Here's the key thing to understand first, because it's the source of most confusion:

> **You do NOT build a separate "wrapper" file.** When you added our classes to `src/`
> (§3) and registered them (§4), they got compiled *into the same extension* the godot-cpp
> example already builds. So your `scons` output in `demo/bin/` — the thing named like
> `libgdexample.macos.template_debug.framework` — **is** the wrapper. There is only one
> build artifact, and you already have it and its `.gdextension`.

So there's almost nothing to "assemble." You only add **one new file**: **our**
`libeffect_engine_c.dylib`. On macOS it must sit **inside the `.framework`, next to the
binary** (that's where `@loader_path` from §2 looks). Not in `demo/bin/` — *inside* the
`.framework` folder:

```
demo/                                        # the example's Godot project — already yours
  bin/
    example.gdextension                            # already here — points Godot at the extension
    libgdexample.macos.template_debug.framework/   # your extension (now contains our wrapper)
      libgdexample.macos.template_debug            # the binary
      libeffect_engine_c.dylib                     # ← THE ONE FILE YOU ADD, right next to the binary
  data/
    your_config.json                               # ← your JSON (see §6)
```

Concretely:

1. **Copy `libs/effect_engine/libeffect_engine_c.dylib` INTO the `.framework` folder**,
   next to the binary — because `@loader_path` resolves to that inner folder, not to
   `demo/bin/`. (On Windows/Linux there's no framework: put the `.dll`/`.so` next to your
   extension in `demo/bin/`.)
2. **Leave the example's `.gdextension` alone.** It already has the correct per-platform,
   per-target paths *and* the correct `entry_symbol` for your build. Don't hand-write one.
3. **Copy your config JSON** into the project, e.g. `demo/data/your_config.json`.

Then reload the Godot project so it re-scans the extension.

> **Better: automate the copy so `scons -c` can't orphan it.** Copying by hand means every
> `scons -c` (clean) wipes the `.framework` and you have to remember to re-copy the
> `.dylib`. Let SConstruct do it. After the `library = env.SharedLibrary(...)` line, add:
>
> ```python
> if env["platform"] == "macos":
>     # library[0] is the built binary inside the .framework; drop our dylib beside it.
>     env.Install(library[0].dir, "libs/effect_engine/libeffect_engine_c.dylib")
> ```
>
> Now a plain `scons` always lands the `.dylib` in the right place. (Node layout can vary
> by godot-cpp version — after building, confirm the `.dylib` really is inside the
> `.framework` next to the binary.)

## 6. Using it from GDScript

Now the fun part — this reads just like the dry run in the main guide:

```gdscript
var engine := EffectEngineGD.new()

# The library reads a real file on disk, so turn res:// into an absolute path.
var cfg_path := ProjectSettings.globalize_path("res://data/your_config.json")
if not engine.load(cfg_path):
    push_error("Could not load effect engine: %s" % engine.last_error())
    return

# "chamomile" / "ginger" are example ingredients — use your own config's names.
var chamomile := engine.get_source("chamomile", "standard")
var ginger    := engine.get_source("ginger", "standard")

var brew := engine.mix(chamomile, ginger, "add", 30)   # 30 = brew time

# Optional: transform the mix with an "alter object" defined in your JSON.
# brew = engine.alter(brew, "steep")                  # "steep" = example alter id

var crafted := engine.craft(brew)

var effect_id := crafted.get_effect_id()
if effect_id == "":
    print("No effect this time — dead zone.")   # Rule 5: totally normal
else:
    print("You brewed: %s" % effect_id)
    print("Domains involved: %s" % crafted.get_active_domains())

# No cleanup needed — when these variables leave scope, the wrappers free everything.
```

## 7. Verify it works (known-good smoke test)

Before wiring the engine into real gameplay, run this exact check against the **example
config** that ships with the project (`tea_alchemist_config-v1.0.0.json`). It uses a recipe
whose result is known, so a matching output proves the whole chain — extension loaded,
library linked, config parsed, math running — is healthy.

1. Copy `tea_alchemist_config-v1.0.0.json` into your project, e.g.
   `res://data/tea_alchemist_config-v1.0.0.json`.
2. Attach this to any node and press Play:

```gdscript
func _ready() -> void:
    var engine := EffectEngineGD.new()
    var cfg := ProjectSettings.globalize_path("res://data/tea_alchemist_config-v1.0.0.json")
    if not engine.load(cfg):
        push_error("load failed: %s" % engine.last_error())
        return

    var cham := engine.get_source("chamomile", "standard")
    var gin  := engine.get_source("ginger", "standard")
    var brew := engine.mix(cham, gin, "subtract", 30)   # known-good recipe
    var crafted := engine.craft(brew)

    print("effect: ", crafted.get_effect_id())
    print("domains: ", crafted.get_active_domains())
```

**✅ If the library is working, the Output panel prints exactly:**

```
effect: calming_brew
domains: { "leaf": 1.414214 }
```

The effect id must be **`calming_brew`** and the single active domain **`leaf`** at a
distance of **≈ 1.41** (that's √2 — trailing digits may differ slightly). If you get that,
you're done: everything from the extension to the C library to the config is wired
correctly. (Note: the §6 example used `"add"` with brew time 30, which lands in a *dead
zone* — a valid empty result, Rule 5. This check uses `"subtract"` so you get an
unmistakable named effect instead.)

If instead Godot crashes, or `EffectEngineGD` is unknown, or `load()` fails — see
Troubleshooting below.

## Troubleshooting

**Godot crashes the instant it loads the extension** (macOS) — the most common failure.
It's the missing rpath from §2. Our `.dylib`'s install-name is `@rpath/…` but the extension
binary has no `LC_RPATH`, so `@rpath` resolves to nothing. Fix: add the
`-Wl,-rpath,@loader_path` linkflag (§2), rebuild, and make sure `libeffect_engine_c.dylib`
is **inside the `.framework`** next to the binary (§5). Verify with
`otool -l <binary> | grep -A2 LC_RPATH`.

**`EffectEngineGD.new()` isn't recognized** — the extension didn't load. Work down this list:

1. **The build didn't register the class.** Rebuild with `scons` after §4 and confirm the
   three `GDREGISTER_RUNTIME_CLASS` lines are in `register_types.cpp`. (While you're there:
   if the example's *own* scene uses a node like `GDExample`, that class must be registered
   too, or Godot crashes loading the scene — that's the example's bug, not ours.)
2. **`entry_symbol` is wrong.** The value in the `.gdextension` must match the symbol your
   `SConstruct` exports. You're reusing the example's file (§5), so this should already be
   correct — don't hand-edit it.
3. **The `.dylib` isn't where the extension looks for it** (see the crash case above).
4. **You didn't reload the project.** Close and reopen it so Godot re-scans extensions.

## 8. Quick function reference (GDScript)

Every method returns a wrapped object or a plain value — you never touch raw pointers, and
you never free anything (the `RefCounted` wrappers do it when the variable leaves scope).

**On `EffectEngineGD` (the engine):**

| Call | Parameters | Returns |
|---|---|---|
| `engine.load(path)` | `path: String` — **absolute** filesystem path to your config JSON (wrap `res://…` in `ProjectSettings.globalize_path()`) | `bool` — `true` on success; `false` → read `engine.last_error()` |
| `engine.last_error()` | — | `String` — reason for the last failed call, or `""`. Read it *immediately* after a failure. |
| `engine.get_source(id, tier)` | `id: String` — source id from your config; `tier: String` — tier name (e.g. `"standard"`) | `EEIngredientRef`, or `null` on failure |
| `engine.mix(a, b, op, brew_time)` | `a, b: EEIngredientRef`; `op: String` — `"add"` / `"subtract"` / `"multiply"`; `brew_time: int` — elapsed brew time | `EEIngredientRef` (a mysterious object), or `null` |
| `engine.alter(target, alter_id)` | `target: EEIngredientRef`; `alter_id: String` — alter object id from your config | `EEIngredientRef`, or `null` |
| `engine.craft(mysterious)` | `mysterious: EEIngredientRef` — must be a `mix`/`alter` result, **not** a raw source (Rule 4) | `EECraftedRef`, or `null` |

**On `EEIngredientRef` (a source or mysterious object):**

| Call | Parameters | Returns |
|---|---|---|
| `ingredient.get_dimensions()` | — | `PackedInt32Array` — flat coords `[d0x,d0y,d0z, d1x,d1y,d1z, …]` |

**On `EECraftedRef` (a craft result):**

| Call | Parameters | Returns |
|---|---|---|
| `crafted.get_effect_id()` | — | `String` — the effect id, or `""` for a **null effect** (dead zone, Rule 5) |
| `crafted.get_active_domains()` | — | `Dictionary` — `{ domain_name: distance }` (empty for a null effect) |

Any call that returns `null` (an empty `Ref`, which is falsy) failed — check
`engine.last_error()` right away. `op` and `alter_id` values, source ids and tiers all come
from **your** JSON config; the wrapper hard-codes none of them.
