# Setup: Effect Engine in Unity (C#)

Step-by-step to link the Effect Engine into a Unity game. Read
[`game_engine_integration.md`](game_engine_integration.md) first for the concepts (the
mental model, the five rules, the dry run) — this file is only the wiring.

**How Unity connects:** Unity calls the C library **directly** using P/Invoke
(`[DllImport]`) — no C++ wrapper needed. Handles arrive as `IntPtr`. We wrap them in small
`IDisposable` classes so `using` blocks free them automatically.

## Quick Start

1. **Drop the library into `Assets/Plugins/`** using the layout in §1. Click it in the
   Project window and, in the Inspector, tick the platforms it applies to (e.g. *Windows
   x86_64*, or *macOS*), then hit Apply.
2. **Add `Native.cs`** — paste the `[DllImport]` declarations from §2 into a new script.
   Make sure the `LIB` constant matches your file name (`"effect_engine_c"`).
3. **Add the wrapper classes** from §3 (`Ingredient`, `Crafted`, `EffectEngine`) into a
   second script.
4. **Put your config JSON in `Assets/StreamingAssets/`** so it ships with the build and is
   readable at a real path at runtime.
5. **Create a MonoBehaviour** with the `Start()` code from §4, and attach it to a
   GameObject in your scene.
6. **Press Play.** Check the Console — you'll see the brewed effect id or "dead zone".

If you get a `DllNotFoundException`, the plugin's platform settings (step 2) don't match
your editor/build target, or the file name doesn't match `LIB` (step 3).

## 1. Where the files go

Put the platform's library under `Assets/Plugins/` and set its platform in the Unity
importer:

```
Assets/Plugins/
  x86_64/effect_engine_c.dll     # Windows
  effect_engine_c.dylib          # macOS
  x86_64/libeffect_engine_c.so   # Linux
```

The `LIB` name in the code must match the file name (without the `lib` prefix or
extension): here, `effect_engine_c`.

## 2. Declare the functions

```csharp
using System;
using System.Runtime.InteropServices;

internal static class Native {
    const string LIB = "effect_engine_c";

    [DllImport(LIB)] internal static extern IntPtr effect_engine_load(string configPath);
    [DllImport(LIB)] internal static extern void   effect_engine_destroy(IntPtr engine);
    [DllImport(LIB)] internal static extern IntPtr effect_engine_last_error();

    [DllImport(LIB)] internal static extern IntPtr effect_engine_get_source(IntPtr e, string id, string tier);
    [DllImport(LIB)] internal static extern IntPtr effect_engine_mix(IntPtr e, IntPtr a, IntPtr b, string op, int brewTime);
    [DllImport(LIB)] internal static extern IntPtr effect_engine_alter(IntPtr e, IntPtr target, string alterId);
    [DllImport(LIB)] internal static extern void   effect_engine_ingredient_free(IntPtr ing);

    [DllImport(LIB)] internal static extern IntPtr effect_engine_craft(IntPtr e, IntPtr mysterious);
    [DllImport(LIB)] internal static extern void   effect_engine_crafted_free(IntPtr c);
    [DllImport(LIB)] internal static extern IntPtr effect_engine_crafted_effect_id(IntPtr c);
    [DllImport(LIB)] internal static extern int    effect_engine_crafted_active_domain_count(IntPtr c);
    [DllImport(LIB)] internal static extern IntPtr effect_engine_crafted_active_domain_id(IntPtr c, int index);
    [DllImport(LIB)] internal static extern float  effect_engine_crafted_active_domain_distance(IntPtr c, int index);

    // Borrowed string → managed copy (Rule 3).
    internal static string LastError() => Marshal.PtrToStringAnsi(effect_engine_last_error());
}
```

## 3. Friendly wrapper classes

```csharp
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public sealed class Ingredient : IDisposable {
    internal IntPtr H;
    internal Ingredient(IntPtr h) { H = h; }
    public void Dispose() {
        if (H != IntPtr.Zero) { Native.effect_engine_ingredient_free(H); H = IntPtr.Zero; }
    }
}

public sealed class Crafted : IDisposable {
    internal IntPtr H;
    internal Crafted(IntPtr h) { H = h; }

    public string EffectId =>                         // "" means null effect (Rule 5)
        Marshal.PtrToStringAnsi(Native.effect_engine_crafted_effect_id(H));

    public Dictionary<string, float> ActiveDomains() {
        var map = new Dictionary<string, float>();
        int n = Native.effect_engine_crafted_active_domain_count(H);
        for (int i = 0; i < n; i++) {
            string id = Marshal.PtrToStringAnsi(Native.effect_engine_crafted_active_domain_id(H, i));
            map[id] = Native.effect_engine_crafted_active_domain_distance(H, i);
        }
        return map;
    }
    public void Dispose() {
        if (H != IntPtr.Zero) { Native.effect_engine_crafted_free(H); H = IntPtr.Zero; }
    }
}

public sealed class EffectEngine : IDisposable {
    IntPtr _e;

    public static EffectEngine Load(string configPath) {
        IntPtr h = Native.effect_engine_load(configPath);
        if (h == IntPtr.Zero) throw new Exception("Load failed: " + Native.LastError());
        return new EffectEngine { _e = h };
    }

    Ingredient Check(IntPtr h, string what) {
        if (h == IntPtr.Zero) throw new Exception(what + " failed: " + Native.LastError());
        return new Ingredient(h);
    }
    public Ingredient GetSource(string id, string tier) =>
        Check(Native.effect_engine_get_source(_e, id, tier), "get_source");
    public Ingredient Mix(Ingredient a, Ingredient b, string op, int brewTime) =>
        Check(Native.effect_engine_mix(_e, a.H, b.H, op, brewTime), "mix");
    public Ingredient Alter(Ingredient target, string alterId) =>
        Check(Native.effect_engine_alter(_e, target.H, alterId), "alter");

    public Crafted Craft(Ingredient mysterious) {
        IntPtr h = Native.effect_engine_craft(_e, mysterious.H);   // Rule 4: mix/alter first
        if (h == IntPtr.Zero) throw new Exception("craft failed: " + Native.LastError());
        return new Crafted(h);
    }
    public void Dispose() {
        if (_e != IntPtr.Zero) { Native.effect_engine_destroy(_e); _e = IntPtr.Zero; }
    }
}
```

## 4. Using it

`using` blocks free everything automatically at the end of the method:

```csharp
void Start() {
    string cfg = System.IO.Path.Combine(Application.streamingAssetsPath, "your_config.json");

    using var engine = EffectEngine.Load(cfg);
    using var chamomile = engine.GetSource("chamomile", "standard");  // example ingredients
    using var ginger    = engine.GetSource("ginger", "standard");
    using var brew      = engine.Mix(chamomile, ginger, "add", 30);
    using var crafted   = engine.Craft(brew);

    if (string.IsNullOrEmpty(crafted.EffectId))
        Debug.Log("No effect this time — dead zone.");
    else
        Debug.Log($"You brewed: {crafted.EffectId}");
}
```

> **iOS note:** iOS can't load a `.dll`/`.so`. Rebuild the library as a *static* library
> and set `const string LIB = "__Internal";` — the `[DllImport]` lines stay the same.

## 5. Verify it works (known-good smoke test)

Before wiring the engine into real gameplay, run this exact check against the **example
config** that ships with the project (`tea_alchemist_config-v1.0.0.json`). It uses a recipe
whose result is known, so a matching output proves the whole chain — plugin loaded, config
parsed, math running — is healthy.

1. Copy `tea_alchemist_config-v1.0.0.json` into `Assets/StreamingAssets/`.
2. Put this in a MonoBehaviour's `Start()` and press Play:

```csharp
void Start() {
    string cfg = System.IO.Path.Combine(Application.streamingAssetsPath,
                                         "tea_alchemist_config-v1.0.0.json");
    using var engine = EffectEngine.Load(cfg);
    using var cham = engine.GetSource("chamomile", "standard");
    using var gin  = engine.GetSource("ginger", "standard");
    using var brew = engine.Mix(cham, gin, "subtract", 30);   // known-good recipe
    using var crafted = engine.Craft(brew);

    Debug.Log($"effect: {crafted.EffectId}");
    foreach (var kv in crafted.ActiveDomains())
        Debug.Log($"  {kv.Key}: {kv.Value}");
}
```

**✅ If the library is working, the Console prints exactly:**

```
effect: calming_brew
  leaf: 1.414214
```

The effect id must be **`calming_brew`** and the single active domain **`leaf`** at a
distance of **≈ 1.41** (that's √2 — trailing digits may differ slightly). If you get that,
everything from the P/Invoke layer to the C library to the config is wired correctly.
(A `DllNotFoundException` here means the plugin's platform settings or `LIB` name are off —
see the Quick Start.)

## 6. Quick function reference (C#)

The wrapper classes hide the raw `IntPtr`s. Every wrapped object is `IDisposable` — free it
with a `using` block (or call `Dispose()`); the engine itself is disposed last.

**On `EffectEngine`:**

| Call | Parameters | Returns |
|---|---|---|
| `EffectEngine.Load(configPath)` | `configPath: string` — filesystem path to your config JSON (e.g. under `Application.streamingAssetsPath`) | `EffectEngine` — **throws** on failure (message includes `last_error`) |
| `engine.GetSource(id, tier)` | `id: string` — source id from your config; `tier: string` — tier name (e.g. `"standard"`) | `Ingredient` — throws on failure |
| `engine.Mix(a, b, op, brewTime)` | `a, b: Ingredient`; `op: string` — `"add"` / `"subtract"` / `"multiply"`; `brewTime: int` — elapsed brew time | `Ingredient` (a mysterious object) — throws on failure |
| `engine.Alter(target, alterId)` | `target: Ingredient`; `alterId: string` — alter object id from your config | `Ingredient` — throws on failure |
| `engine.Craft(mysterious)` | `mysterious: Ingredient` — must be a `Mix`/`Alter` result, **not** a raw source (Rule 4) | `Crafted` — throws on failure |
| `engine.Dispose()` | — | destroys the engine (do this last; `using` handles it) |

**On `Crafted`:**

| Member | Type | Meaning |
|---|---|---|
| `crafted.EffectId` | `string` (property) | the effect id, or `""` for a **null effect** (dead zone, Rule 5) |
| `crafted.ActiveDomains()` | `Dictionary<string, float>` | `{ domain_name: distance }` (empty for a null effect) |

Unlike the Godot wrapper, failures here **throw exceptions** rather than returning null, so
wrap calls in `try/catch` if you want to recover. `op` and `alterId` values, source ids and
tiers all come from **your** JSON config; the wrapper hard-codes none of them.
