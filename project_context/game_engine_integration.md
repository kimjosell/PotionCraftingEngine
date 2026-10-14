# Connecting Your Game Engine to the Effect Engine — Concepts

This is the shared, engine-agnostic half of the integration docs: what the library is,
the mental model, the handful of rules for talking to it, and a language-neutral dry run.
Once these click, pick your engine's **setup guide** and follow the wiring:

- **Godot 4** → [`setup_godot.md`](setup_godot.md)
- **Unity (C#)** → [`setup_unity.md`](setup_unity.md)
- **Unreal Engine (C++)** → [`setup_unreal.md`](setup_unreal.md)

We won't re-explain how the effect math works or how to author a config file — that lives
in `effect_system_design.md` and `effect_engine_architecture.md`.

---

## 1. What is this thing, exactly?

The Effect Engine is a small **compiled library** that acts as the "rules brain" for a
crafting/mixing system. Think of it like a physics engine, but for potions instead of
collisions: your game hands it ingredients, it does the math, and it hands back a result.

Two important ideas:

- **Your game never changes the library to add content.** All the content — which
  ingredients exist, what they do, which effects are possible — lives in a **JSON config
  file** that the library reads at startup. Want a new ingredient? Edit the JSON. The
  compiled library stays the same.
- **The library talks in a plain "C" language interface** so it can be called from any
  engine. You don't need to know C++ to use it.

> 📎 **About the examples in these docs.** The code refers to ingredients like `chamomile`
> and `ginger`, and effects like `calming_brew`. These come from an **example** config
> that ships with the project, `tea_alchemist_config-v1.0.0.json`. It's just sample
> content to make things concrete — **your game defines its own** ingredients, alters, and
> effects in your own JSON file. Wherever you see those names, imagine your own content.

---

## 2. The mental model: how a craft happens

Here's the whole journey, start to finish. Everything you do with the library follows
this shape:

```
   1. Load the config file            →  you get an "engine"
   2. Ask for source ingredients      →  e.g. "chamomile", "ginger"
   3. Mix or Alter them               →  you get a "mysterious object"
   4. Craft the mysterious object     →  you get the final "effect"
   5. Read the effect                 →  an id like "calming_brew", or nothing
```

A few words you'll meet along the way:

| Word | What it means for you |
|---|---|
| **Engine** | The loaded library + your config. You make one at startup, destroy it at shutdown. |
| **Source object** | A raw ingredient from your config (has a name and a "tier", like `"chamomile"` / `"standard"`). |
| **Mysterious object** | The in-progress result of mixing/altering. Not final yet — you can keep combining it. |
| **Mix** | Combine 2..N objects (add / multiply).|
|**Subtract**| Subtract the value of an object to a base object.|
| **Alter** | Transform one object using a named "alter object" from your config. |
| **Craft** | The final step: turn a mysterious object into an actual effect. |
| **Effect** | The result of a craft — an id (e.g. `"calming_brew"`) plus which domains were involved. |
| **Null effect** | A valid craft that landed on *no* effect (a "dead zone"). You'll see this as an empty effect id. It's normal — plan for it. |

That's the entire vocabulary you need.

---


## 3. The five rules of talking to the library

The library speaks a deliberately simple language. Learn these five rules once and every
engine's setup guide will make sense.

**Rule 1 — You get "handles", and you must give them back.**
When you ask for an ingredient or a craft, you get back a *handle* (an opaque pointer —
think of it as a coat-check ticket). You own it. When you're done, you **free** it:
- ingredient handles → `effect_engine_ingredient_free`
- craft handles → `effect_engine_crafted_free`
- the engine itself → `effect_engine_destroy` (do this **last**, after everything else)

Forget to free, and you leak memory. Most engine wrappers in the setup guides do this
automatically.

**Rule 2 — If something fails, the return value tells you, and `last_error` says why.**
Functions that make handles return **nothing (`NULL`)** on failure. Functions that return
a number return **`-1`**. The moment that happens, call `effect_engine_last_error()` to
get a human-readable reason. Read it *immediately* — it gets overwritten by your next call.

**Rule 3 — Text the library hands you is borrowed, not yours to free.**
Effect ids, domain names, ingredient names — these strings belong to the library. Read
them, copy them into your own string if you want to keep them, but never free them.

**Rule 4 — You must `mix` or `alter` before you can `craft`.**
`craft` only accepts a *mysterious object* (the result of a mix/alter), not a raw source
ingredient. If you try to craft a source directly, you'll get a `NULL` and an error. This
is intentional.

**Rule 5 — An empty effect id means "no effect" — and that's OK.**
After a craft, if the effect id is an empty string `""`, the craft landed in a dead zone.
This is a normal outcome, not an error. Always check for it.

---

## 4. A dry run (before touching any engine)

Here's the whole flow in plain pseudocode, so you recognize it when you see it in your
language of choice. (Remember: `chamomile`, `ginger`, `add` are example content.)

```
engine = load("path/to/your_config.json")
if engine is null: show last_error(); stop

chamomile = get_source(engine, "chamomile", "standard")
ginger    = get_source(engine, "ginger", "standard")

ingredients = [chamomile, ginger]

# Mix them. The result is a mysterious object.
brew = mix(engine, ingredients, count=2, "add")

crafted = craft(engine, brew)
effect  = crafted.effect_id
if effect == "":
    print("No effect this time (dead zone)")
else:
    print("You made:", effect)

# Give the handles back.
free(crafted); free(brew); free(ginger); free(chamomile)
destroy(engine)
```

Every setup guide is just this dry run, dressed in that engine's clothing. Pick yours:

- **Godot 4** → [`setup_godot.md`](setup_godot.md)
- **Unity (C#)** → [`setup_unity.md`](setup_unity.md)
- **Unreal Engine (C++)** → [`setup_unreal.md`](setup_unreal.md)

---

## 5. When something goes wrong (any engine)

| Symptom | Likely cause | Fix |
|---|---|---|
| `load` returns null / errors | Wrong file path, or the config isn't shipped with the build | Use an absolute path; make sure the JSON is copied into the packaged game |
| A call returns null/`-1` and you don't know why | You didn't check the error | Call `last_error()` **right after** the failing call (it gets overwritten) |
| `craft` fails with "requires a mysterious object" | You tried to craft a raw source ingredient | `mix` or `alter` it first, then craft the result (Rule 4) |
| Effect id is empty and you treated it as a bug | It's a **null effect** (dead zone) | That's a valid outcome — handle the `""` case (Rule 5) |
| Memory grows over time | Handles never freed | Free every ingredient/craft handle; in Godot/Unity the wrappers do it for you |
| Garbled or crashing text | You kept a borrowed string too long | Copy library strings into your own string type before storing them (Rule 3) |
| Library won't load at runtime | The `.dll`/`.so`/`.dylib` isn't where the game binary looks for it | Ship the library with your build (see your engine's setup guide; Godot macOS has an extra rpath step) |

---

## 6. Quick recap

1. Build the library, copy the **library file** + **header** into your game.
2. **Load** the config once → you get an engine.
3. **Get sources**, **mix**/**alter** them into a mysterious object, then **craft** it.
4. Read the **effect id** — remember `""` means "no effect", and that's fine.
5. **Free** your handles (or let the Godot/Unity wrappers do it), and **destroy** the
   engine at shutdown.

That's it — the same five steps in every engine. Happy brewing! 🍵

> Reminder: `chamomile`, `ginger`, `add`, `calming_brew` and the file
> `tea_alchemist_config-v1.0.0.json` are all **example** content. Swap in your own config's
> ingredients and effects.
