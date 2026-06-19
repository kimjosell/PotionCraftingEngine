# Effect Engine — Architecture & Design Decisions

## What This Document Is

This document captures the architecture, design decisions, and open questions for the **Effect Engine** — a reusable, data-driven C++ library that implements the effect system described in `effect_system_design.md`. It is intended as a starting point for further design conversations and implementation planning. It should be read alongside the effect system design paper, which covers the mathematical formulas and system logic in full detail.

---

## 1. The Core Idea

The Effect Engine is a **native C++ library** that:

- Loads a JSON configuration describing domains, effects, source objects, and operation time behavior
- Exposes operations for mixing and altering objects, each accepting an optional elapsed time parameter
- Resolves the dominant effect of a mysterious object on demand
- Can be consumed by any game engine (Godot, Unity, Unreal) via a thin binding layer

It is **not** a game — it is infrastructure. It owns the rules and the math. The game owns the state, the UI, the metadata, and the narrative.

---

## 2. Design Philosophy

### Data-driven, not hardcoded

All content — domain names, effect anchors, capture radii, source object values, operation time config, per-effect β — lives in a JSON config file. The engine contains only logic. A different developer can create an entirely different effect system by writing a different config, without touching engine code.

### Fixed shape, flexible content

| Fixed by engine | Configurable by developer |
|---|---|
| 3 dimensions per domain | Number of domains |
| Euclidean distance formula | Modulus (value range per dimension) |
| β-threshold dominance algorithm | Pure effect anchors, capture radii (per effect) |
| Average-based combined score | Combined effect anchors, domain spans, capture radii, β (per effect) |
| Tier system shape for source objects | Source object definitions and tier values |
| Modulus applied after every operation | Time modifier config per mix operation type |
| Min 2 / max domain_count for combined spans | Number of source objects, alter objects, and effects |
| Alter object shape (one exponent per domain) | Alter object definitions and exponent values |
| Alter exponent range `[1, modulus-2]` | Which exponents each alter object uses |
| Alter has no time modifier | — |

### Stateful about config, stateless about game objects

The library loads config at startup and holds it internally. After that, it operates on value objects passed in by the game — it does not store them. Elapsed time is passed by the game per operation call — the library has no internal clock.

### Learnability over procedural variety

Source object domain values are **fixed per tier**. Players build reliable mental models and share discoveries. Procedural values would undermine discoverability.

---

## 3. Three-Layer Architecture

```
┌─────────────────────────────────────────────────┐
│  Layer 3 — Game                                 │
│  Names, flavor text, inventory, save state,     │
│  UI, player progression. Library never          │
│  touches this.                                  │
└───────────────────┬─────────────────────────────┘
                    │ passes/receives value objects + elapsed time
┌───────────────────▼─────────────────────────────┐
│  Layer 2 — Value Objects                        │
│  Structs of domain coordinate arrays.           │
│  Always in [0, modulus-1] after any operation.  │
│  Created and validated by the library.          │
│  Owned and stored by the game.                  │
│  Library is stateless about these.              │
└───────────────────┬─────────────────────────────┘
                    │ validated against
┌───────────────────▼─────────────────────────────┐
│  Layer 1 — Engine Config                        │
│  Domain definitions, modulus, effect anchors,   │
│  capture radii, per-effect β,                   │
│  operation time config, source object templates.│
│  Loaded from JSON. Immutable after load.        │
└─────────────────────────────────────────────────┘
```

### Layer 2 — Value objects in detail

The runtime value types form two disjoint families.

**Family 1: Ingredients (coordinate-based).** An abstract `Ingredient` carries an `id: String` and `dimensions: int[][3]` — one 3-tuple per domain, each value in `[0, modulus-1]`. Two concrete subtypes inherit from it:

- **`SourceObject`** — `id` is the source's config name (e.g., `"soul_stone"`), plus a `tier: String` (e.g., `"standard"`). Returned by `get_source()`.
- **`MysteriousObject`** — `id` is always the literal string `"mysteriousObject"` (a sentinel that distinguishes it from any source object). Carries a `recipe: Recipe` describing how it was built. Returned by `mix()` and `alter()`.

Because both subtypes inherit from `Ingredient`, mix and alter accept either as input.

**Family 2: AlterObject (exponent-based).** `AlterObject` is a structurally distinct value type with `id: String` and `exponents: int[]` (one exponent per domain, each in `[1, modulus-2]`). It is **not an `Ingredient`** — it has no dimensions, cannot be mixed, and cannot be the target of an alter. The library exposes AlterObjects through `get_alter_object_info(id)` for read-only inspection by the game (e.g., to display in UI), but the public `alter()` API takes alter objects by **id string**, not by reference. This keeps the FFI boundary simple and makes recipes trivially serializable.

**`CraftedObject`** — the output of `craft()`. Carries:
- `recipe: Recipe` — the full operation tree that produced this craft (see §8).
- `effect: Effect` — the dominant effect with its id, the crafted object's final dimensions, and the active domains map.

The library is stateless about all of these — value objects are owned and persisted by the game.

---

## 4. Modulus Application — Every Operation

Modulus is applied after every mix and alter operation, not only at craft time. This prevents integer overflow in deep mix chains (multiply chains can reach 10^20+ values without mod) and does not hurt gameplay because players never observe intermediate raw values.

The modulus is not what distinguishes object types — **craft is when effect calculation runs**, regardless of value range.

---

## 5. Operations and the Time Modifier

### The four operations

Three **mix** operations (add, subtract, multiply) and one **alter** operation. They differ in shape:

- **Mix operations** take two coordinate-based objects (source or mysterious) of matching shape, combine them dimension-by-dimension within each domain, and apply a time modifier. Output is a mysterious object.
- **Alter** takes one coordinate-based object (source or mysterious) as the target and one alter object as the exponent input. Each domain of the target is raised to the corresponding exponent from the alter object, applied uniformly across all 3 dimensions. **Alter does not apply a time modifier.** Output is a mysterious object.

### Mix operation sequence

```
step 1: raw.domain[k].dim[i]    = (a.domain[k].dim[i] <op> b.domain[k].dim[i]) mod modulus
step 2: modifier                 = floor(elapsed_time / time_per_point) mod (max_modifier + 1)
step 3: result.domain[k].dim[i] = (raw.domain[k].dim[i] + modifier) mod modulus
```

The modifier is uniform — the same value added to every dimension of every domain.

### Alter operation sequence

```
result.domain[k].dim[i] = (target.domain[k].dim[i] ^ alter_obj.exponent[k]) mod modulus
```

Single step. No time modifier. Within a domain, all 3 dimensions use the same exponent.

### Time modifier config (per mix operation type)

| Parameter | Meaning |
|---|---|
| `max_modifier` | Maximum modifier points before cycling back to 0 |
| `time_per_point` | Time units required for modifier to increment by 1 |

The modifier cycles — waiting indefinitely does not push values in one direction forever. This prevents time from becoming a trivial farming vector while still rewarding players who learn the rhythm of specific mix operation types. Alter has no time configuration.

### Implicit carry-over

Time influence is permanently baked into a mysterious object's values after each mix operation. When that object feeds into a subsequent operation (mix or alter), those values carry forward naturally. Each mix operation applies its own time config independently. Alter passes carry-over through unchanged in any domain whose exponent is 1, and transforms it via modular exponentiation in any domain with exponent ≥ 2.

### Type rules at the API boundary

The library validates these rules and rejects mismatches with a clear error:

- Mix operands must both be coordinate-based objects (source or mysterious). An alter object passed as a mix operand is rejected.
- The target of alter must be a coordinate-based object. An alter object passed as the target is rejected.
- The exponent input to alter must be an alter object. A coordinate-based object passed as the exponent is rejected.

### Example walkthrough — mix

```
Config: add, max_modifier=5, time_per_point=10
        modulus=11, 2 domains (simplified for readability)

soul_stone standard: [ [3,7,2], [9,1,5] ]
ember_core standard: [ [8,4,6], [2,8,3] ]

Player mixes via ADD for 30 time units:

  step 1 (arithmetic + mod):
    domain 0: [(3+8)%11, (7+4)%11, (2+6)%11] = [0, 0, 8]
    domain 1: [(9+2)%11, (1+8)%11, (5+3)%11] = [0, 9, 8]

  step 2 (time modifier):
    modifier = floor(30/10) mod (5+1) = 3 mod 6 = 3

  step 3 (apply modifier + mod):
    domain 0: [(0+3)%11, (0+3)%11, (8+3)%11] = [3, 3, 0]
    domain 1: [(0+3)%11, (9+3)%11, (8+3)%11] = [3, 1, 0]

mysterious_1 = [ [3,3,0], [3,1,0] ]

Player crafts → effect calculation runs on these coordinates.
```

### Example walkthrough — alter

```
Config: modulus=11, 2 domains

target (mysterious_1):  [ [3,3,0], [3,1,0] ]
alter_obj water_focus:  [1, 5]   // identity on domain 0, exponent 5 on domain 1

Player applies alter:

  domain 0 (exponent 1, identity):
    [3^1 % 11, 3^1 % 11, 0^1 % 11] = [3, 3, 0]   ← unchanged

  domain 1 (exponent 5):
    [3^5 % 11, 1^5 % 11, 0^5 % 11]
    = [243 % 11, 1 % 11, 0 % 11]
    = [1, 1, 0]

mysterious_2 = [ [3,3,0], [1,1,0] ]
```

The first domain is bit-for-bit identical to the input — the property of exponent 1.

---

## 6. Source Objects

### Structure

Every source object tier is a nested array — one subarray per domain, each subarray containing exactly 3 dimension values:

```json
"soul_stone": {
  "tiers": {
    "standard": [
      [3, 7, 2],
      [9, 1, 5],
      [4, 8, 3],
      [6, 2, 7]
    ],
    "high_quality": [
      [4, 8, 3],
      [9, 2, 6],
      [5, 9, 4],
      [7, 3, 8]
    ]
  }
}
```

Four subarrays for four domains, three values per subarray for three dimensions.

### Why source object values are bounded to [0, modulus-1]

Values outside [0, modulus-1] are mechanically equivalent to their mod-reduced counterparts for all supported operations — a source object value of 25 with modulus 11 behaves identically to a value of 3 (25 mod 11 = 3) in any mix. Allowing out-of-range values would mislead the designer into believing two mechanically identical objects are distinct. The validator rejects out-of-range source object values at config load time.

### Validation

At config load time the engine checks:
- Every source object tier has exactly `domain_count` subarrays
- Every subarray has exactly 3 values
- Every value is in [0, modulus-1]
- No duplicate tier names within a source object

---

## 7. Alter Objects

### Structure

Each alter object is a flat array of `domain_count` exponents, one per domain:

```json
"fire_focus": {
  "exponents": [1, 1, 7, 1]
}
```

The exponent at index `k` is applied to all 3 dimensions of domain `k` in the target object during an alter operation. Note the structural asymmetry with source objects: alter objects have one value per domain (not three), because the engine fixes that all dimensions within a domain share the same exponent.

### Why one exponent per domain (not per dimension)

Allowing a separate exponent per dimension would give D×3 degrees of freedom per alter object instead of D. This is more expressive in principle but harder for players to reason about — each alter object would have 12 parameters in a 4-domain config. The one-exponent-per-domain choice keeps alter objects within the player's mental model: each one has a clear effect "per domain" that maps to the same domain-level vocabulary used everywhere else in the system.

### No tiers

Alter objects do not have tiers. Tiers in source objects make sense because tier values are monotonically "better" — a high-quality soul stone has different but comparable values to a standard one. Modular exponentiation has no such ordering: exponent 7 is not "better" or "worse" than exponent 3, just different. Each alter object is its own distinct entity, named and used directly.

### Why exponents are bounded to [1, modulus-2]

Under prime modulus, exponents are cyclic with period `modulus-1` by Fermat's Little Theorem. The effective range of meaningful exponents is `[1, modulus-2]`:

- **Exponent 0** collapses any value to 1 (`a^0 = 1`). Excluded as standard input — degenerate "total reset" mechanic.
- **Exponent 1** is the identity: the domain is left unchanged. Allowed deliberately, so the designer can build "selective" alter objects that only affect specific domains.
- **Exponents 2 to `modulus-2`** are the genuinely distinct transformation patterns. With modulus 11, this is `{2, 3, 4, 5, 6, 7, 8, 9}`.
- **Exponent `modulus-1`** collapses non-zero values to 1 by Fermat's theorem. Excluded for the same reason as 0.
- **Exponents ≥ `modulus-1`** are mechanically equivalent to `exponent mod (modulus-1)` and are rejected to avoid hidden duplication, mirroring the same reasoning used for source object value bounds.

### Validation

At config load time the engine checks:
- Every alter object has exactly `domain_count` exponents
- Every exponent is in `[1, modulus-2]`
- At least one exponent is different from 1 (rejects the identity alter object)
- No duplicate alter object names

### Designer guidance — exponent character

Within `[1, modulus-2]`, exponents fall into two categories that have qualitatively different mechanical behaviors. The engine does not distinguish between them, but designers should know:

- **Exponents coprime with `modulus-1`** are bijections — they permute values without losing information. For modulus 11: `{3, 7, 9}`.
- **Exponents sharing factors with `modulus-1`** collapse multiple input values into fewer outputs. For modulus 11: `{2, 4, 5, 6, 8}`.

Bijective exponents preserve the full reachable space of the target object. Collapsing exponents concentrate the result, often making certain effect anchors unreachable after the alter is applied. This is a design lever worth exploiting when building alter objects with intended thematic identities (e.g., a "purifying" alter object that uses bijective exponents vs. a "consuming" alter object that uses collapsing ones).

### Example

```json
"alter_objects": {
  "water_focus":      { "exponents": [1, 1, 1, 5] },
  "fire_amplifier":   { "exponents": [1, 7, 1, 1] },
  "dual_storm":       { "exponents": [3, 1, 1, 7] },
  "chaos_burst":      { "exponents": [2, 4, 6, 8] },
  "pure_amplifier":   { "exponents": [3, 3, 3, 3] }
}
```

`water_focus` only modifies the fourth domain. `pure_amplifier` applies a bijective transformation to every domain uniformly. `chaos_burst` applies different collapsing exponents to each domain.

---

## 8. Recipes — Operation Trees

Every `MysteriousObject` and every `CraftedObject` carries a `recipe: Recipe` describing the full chain of operations that produced it. The recipe is a **tree**, not a flat list — it preserves the complete structure of how ingredients were combined.

### Recipe hierarchy

```
Recipe (abstract)
  + operationId: String

  ├── Mix
  │     + operationId: String     // "add" | "subtract" | "multiply"
  │     + time: int               // elapsed time passed to the mix
  │     + ingredientA: Ingredient
  │     + ingredientB: Ingredient
  │
  └── Alter
        + operationId: String     // always "exponentiation"
        + alterObjectId: String   // the id of the alter object used
        + ingredient: Ingredient  // the target of the alter
```

`Mix` references two `Ingredient`s (which can each be a `SourceObject` leaf or a `MysteriousObject` whose own `recipe` forms a subtree). `Alter` references one `Ingredient` plus the `alterObjectId: String` — the alter object is **not embedded by reference**, only by id, so the recipe stays serializable as plain data.

### Why a tree, not a flat list

The natural shape of mix and alter operations is a tree: a `Mix` has two children (its operands), an `Alter` has one child (its target), and the leaves are always `SourceObject`s. A flat list would have to invent some traversal order and lose the explicit grouping, making replay and inspection harder.

### `operationId` semantics

- For `Mix`, `operationId` matches an entry in the `operations` block of the config: `"add"`, `"subtract"`, or `"multiply"`.
- For `Alter`, `operationId` is the fixed sentinel string `"exponentiation"`. This id is **not** present in the config's `operations` block — alter has no time configuration and is not a configurable operation type. The sentinel exists only so that the `Recipe` base class can carry a uniform `operationId` field for any subclass.

### What recipes are for

Three intended use cases:

1. **Replay and determinism.** Given a recipe and the engine config, the game can reconstruct the exact sequence of operations that produced a craft. This is useful for tutorials ("here is how this potion was made"), achievements, and reproducibility across saves.
2. **UI and inspection.** The game can walk the tree to display the recipe to the player, label each ingredient, and show the operation sequence.
3. **Debugging and validation.** During development, recipes make it possible to trace why a particular crafted object ended up where it did in the domain space.

### Serialization is the game's responsibility

The engine builds recipes and returns them attached to mysterious and crafted objects, but **does not persist them**. If the game wants to save a craft and reload it later — preserving the recipe — the game serializes the tree itself using whatever format suits its save system. The recipe contains only ids (`SourceObject.id`, `SourceObject.tier`, `"mysteriousObject"`, `alterObjectId`, `operationId`) and primitive data (`time`, `dimensions`), so any standard serializer (JSON, binary, protobuf) works.

When the game reloads a serialized recipe, it can either:
- Treat it as opaque data for display purposes, or
- Replay it through the engine to regenerate the corresponding `MysteriousObject` or `CraftedObject` from scratch (since operations are deterministic given the same inputs and time values).

### Memory note

Because a `MysteriousObject` carries its full recipe, deeply nested chains of mixes can grow the recipe tree linearly with the number of operations performed. This is acceptable for typical gameplay (recipes of a few dozen nodes) but worth noting for any system that creates very deep chains. The game can prune or flatten old recipes if it does not need replay capability for them.

---

## 9. The Combination Space

Fixed source object values do not limit the system meaningfully. Example with 15 source objects × 2 tiers = 30 coordinate-based value objects, 6 alter objects, 3 mix operations + 1 alter operation:

```
After 1 mix:    30 × 30 × 3       =      2,700 mix combinations
After 1 alter:  30 × 6            =        180 alter combinations
After 1 op:     2,700 + 180       =      2,880 base combinations
After 2 ops:    2,880 × (30 × 3 + 6) = ~276,000
After 3 ops:    ~276,000 × 96     = ~26,500,000
```

Time variation multiplies each mix branch further — with `max_modifier = 5`, each mix can produce up to 6 distinct outcomes depending on elapsed time. Alter has no time variation, so it produces a single outcome per (target, alter_object) pair. Order of operations also matters due to mod arithmetic. The reachable space is effectively enormous.

---

## 10. API Design (Conceptual)

The public API is a **C API** (not C++) for maximum portability across languages and engines via FFI.

### Lifecycle

```c
EffectEngine* engine = effect_engine_load("config.json", &error);
if (!engine) { /* handle error */ }

effect_engine_destroy(engine);
```

### Getting source objects

```c
// Returns a SourceObject — an Ingredient with id == "soul_stone" and tier == "standard"
SourceObject src = effect_engine_get_source(
    engine, "soul_stone", "standard", &error
);
```

### Inspecting alter objects

```c
// Returns an AlterObject for read-only inspection (e.g., to display in UI).
// The runtime AlterObject is NOT passed back into alter() — alter() takes
// the alter object id as a string. AlterObjects are exposed only for queries.
AlterObject info = effect_engine_get_alter_object_info(
    engine, "fire_focus", &error
);
```

### Mixing and altering

```c
// Mix: takes two Ingredients (SourceObject or MysteriousObject).
// elapsed_time in whatever units match time_per_point in config.
// Pass 0 for no time influence.
MysteriousObject result = effect_engine_mix(
    engine, ingredient_a, ingredient_b, "add", elapsed_time, &error
);

// Alter: takes one Ingredient as target and an alter object id as a string.
// No elapsed_time parameter — alter has no time modifier.
MysteriousObject result = effect_engine_alter(
    engine, target_ingredient, "fire_focus", &error
);
```

The library validates rules at the API boundary and returns an error on mismatches:
- Mix rejects an unknown `operation_id` (anything not in the config's `operations` block).
- Mix rejects an Ingredient whose dimensions do not match the engine's domain count.
- Alter rejects an unknown `alter_object_id`.
- Alter rejects an Ingredient whose dimensions do not match the engine's domain count.

Both mix and alter attach a `Recipe` to the returned `MysteriousObject` describing the operation that produced it, with the input Ingredients embedded as the recipe's children (for `Mix`) or child (for `Alter`).

### Crafting an effect

```c
// No time parameter — time is already baked into the mysterious object's values.
// The recipe field is preserved on the CraftedObject.
CraftedObject crafted = effect_engine_craft(engine, mysterious, &error);

const char* effect_id      = crafted.effect.effect_id;
const char* effect_type    = crafted.effect.effect_type;      // "pure" or "combined"
// active_domains maps each participating domain id to the distance
// from the crafted object's coordinate to that domain's effect anchor
const ActiveDomain* active_domains = crafted.effect.active_domains;
size_t              n_active        = crafted.effect.n_active_domains;
// Full recipe tree, available for replay, display, or serialization by the game
const Recipe*       recipe          = crafted.recipe;
```

### Querying config (for game UI)

```c
effect_engine_list_sources(engine, &out_ids, &out_count);
effect_engine_list_tiers(engine, "soul_stone", &out_tiers, &out_count);
effect_engine_list_alter_objects(engine, &out_ids, &out_count);
effect_engine_list_domains(engine, &out_ids, &out_count);
```

---

## 11. JSON Config Schema

### Top-level structure

```json
{
  "engine": { ... },
  "domains": { ... },
  "operations": { ... },
  "effects": {
    "pure": { ... },
    "combined": { ... }
  },
  "source_objects": { ... },
  "alter_objects": { ... }
}
```

### Engine block

```json
"engine": {
  "modulus": 11
}
```

### Domains block

```json
"domains": {
  "water": { "display_name": "Water" },
  "fire":  { "display_name": "Fire" },
  "earth": { "display_name": "Earth" },
  "wind":  { "display_name": "Wind" }
}
```

Domain order determines the index used for all domain coordinate arrays throughout the config.

### Operations block

The operations block configures the time modifier behavior of the **mix** operations only. Alter has no time configuration and does not appear here.

```json
"operations": {
  "add": {
    "max_modifier": 5,
    "time_per_point": 10
  },
  "subtract": {
    "max_modifier": 3,
    "time_per_point": 20
  },
  "multiply": {
    "max_modifier": 8,
    "time_per_point": 5
  }
}
```

`time_per_point` uses whatever in-game time unit the game tracks — the engine treats it as an opaque integer. The game is responsible for passing consistent units.

### Effects block

```json
"effects": {
  "pure": {
    "tidal_surge": {
      "domain": "water",
      "anchor": [8, 2, 7],
      "capture_radius": 2.5
    },
    "ember_heart": {
      "domain": "fire",
      "anchor": [9, 1, 8],
      "capture_radius": 3.0
    }
  },
  "combined": {
    "storm_call": {
      "domains": ["water", "wind"],
      "beta": 0.8,
      "anchors": {
        "water": { "coords": [7, 8, 6], "capture_radius": 2.0 },
        "wind":  { "coords": [6, 9, 7], "capture_radius": 2.0 }
      }
    },
    "world_convergence": {
      "domains": ["water", "fire", "earth", "wind"],
      "beta": 1.3,
      "anchors": {
        "water": { "coords": [8, 7, 9], "capture_radius": 3.5 },
        "fire":  { "coords": [9, 8, 7], "capture_radius": 3.5 },
        "earth": { "coords": [7, 9, 8], "capture_radius": 3.5 },
        "wind":  { "coords": [8, 8, 9], "capture_radius": 3.5 }
      }
    }
  }
}
```

Notes on capture_radius and β:
- `capture_radius` controls whether the effect is a candidate at all — it defines the landing zone sphere
- `β` controls how the combined effect competes against the best pure effect once it is a candidate
- These are independent knobs and should be tuned separately
- A 4-domain combined effect typically needs a larger `capture_radius` and/or higher `β` to compensate for its geometric rarity
- Combined effects must span at least 2 and at most `domain_count` domains — enforced at config load

### Source objects block

```json
"source_objects": {
  "soul_stone": {
    "tiers": {
      "standard": [
        [3, 7, 2],
        [9, 1, 5],
        [4, 8, 3],
        [6, 2, 7]
      ],
      "high_quality": [
        [4, 8, 3],
        [9, 2, 6],
        [5, 9, 4],
        [7, 3, 8]
      ]
    }
  },
  "ember_core": {
    "tiers": {
      "standard": [
        [8, 4, 6],
        [2, 8, 3],
        [7, 1, 9],
        [3, 6, 2]
      ]
    }
  }
}
```

Outer array order matches domain order from the domains block. Each inner array is [dim1, dim2, dim3].

### Alter objects block

```json
"alter_objects": {
  "water_focus":     { "exponents": [1, 1, 1, 5] },
  "fire_amplifier":  { "exponents": [1, 7, 1, 1] },
  "dual_storm":      { "exponents": [3, 1, 1, 7] },
  "chaos_burst":     { "exponents": [2, 4, 6, 8] },
  "pure_amplifier":  { "exponents": [3, 3, 3, 3] }
}
```

The `exponents` array order matches domain order from the domains block. Each value is a single integer in `[1, modulus-2]`. Validation rejects out-of-range exponents and any alter object with all exponents equal to 1 (identity).

---

## 12. Game Engine Integration

### The binding pattern

```
effect_engine/          ← pure C++ + C API, no engine dependency
  src/
  include/effect_engine.h
  CMakeLists.txt

bindings/
  godot/                ← GDExtension wrapper (~200-300 lines)
  unity/                ← C# P/Invoke declarations
  unreal/               ← Unreal plugin module wrapper
```

### Godot (primary target)

Uses **GDExtension**. The wrapper exposes an `EffectEngine` node class, translates GDScript types (String, Dictionary, Array) to/from the C API, and surfaces errors as Godot signals.

### Unity and Unreal

The same compiled binary works for Unity via **P/Invoke**. Unreal requires a thin plugin module wrapper but the core binary is identical.

### Cross-platform compilation

CMake from a single `CMakeLists.txt`:
- Windows: `.dll` / Linux: `.so` / macOS: `.dylib`
- Web via Emscripten: deferred

---

## 13. Scope for First Version

| Item | Target |
|---|---|
| Number of domains | 4 (Water, Fire, Earth, Wind) |
| Modulus | 11 (values 0–10) |
| Source objects | 10–15, each with 2 tiers |
| Alter objects | 5–8 distinct, no tiers |
| Pure effects | ~20 total across all domains |
| Combined effects | A handful (2–4) spanning 2 domains each |
| Mix operations | 3 (add, subtract, multiply) with time modifier |
| Alter operation | 1, no time modifier |
| Primary game engine | Godot |
| Config format | Single JSON file |

---

## 14. Deliberately Deferred

| Item | Why deferred |
|---|---|
| Visual config editor | Config is small enough to edit by hand for v1 |
| Procedural source object value generation | Adds complexity, undermines learnability |
| Player upgrade paths between tiers | Not in first game's acquisition model |
| Alter object tiers | Modular exponentiation has no natural ordering — tiers don't map cleanly |
| Web/WASM export | Emscripten adds build complexity |
| Save state in the library | Game concern, not engine concern |
| More than 3 dimensions per domain | Fixed by design to keep validation tractable |
| Per-dimension exponents in alter objects | One exponent per domain keeps the player's mental model tractable |
| Multiple config files | Deferred until config outgrows a single file |

---

## 15. Open Questions

1. **Error handling strategy in the C API:** Return codes + error string out-param (simple, portable) vs. a more structured error type. Decide before writing the first binding.

2. **MysteriousObject and AlterObject serialization formats:** MysteriousObject values are always in [0, modulus-1] and can be serialized as a flat integer array of size `domain_count × 3`. AlterObject can be serialized as a flat integer array of size `domain_count`. Confirm and document the final memory layouts explicitly so game developers know exactly what to persist.

3. **Time unit convention:** `time_per_point` is an opaque integer unit — the game decides what it represents (real seconds, in-game ticks, in-game minutes). This must be documented clearly in the config schema so different games use it consistently.

4. **Dead zone fallback behavior:** If a crafted object lands outside all effect capture zones (no candidates at all), the current design falls back to the nearest pure effect regardless of radius. This should be confirmed as the intended behavior and whether a special "null effect" or "neutral" result is preferable in some designs.

5. **Recipe tree memory representation across the FFI boundary:** Recipes are recursive trees. The C API must expose them in a way that game-engine bindings (Godot, Unity, Unreal) can walk without exposing C++ ownership semantics. Options: opaque handles + accessor functions, or a fully flattened serialized form returned alongside the tree. Decide before writing the first binding.

6. **Recipe replay API:** Whether the engine should expose a `replay(recipe) → MysteriousObject` function that walks a serialized recipe tree and re-executes it. Useful for save/load and tutorials, but adds API surface. Defer until a concrete use case demands it.

---

## 16. Next Steps (Suggested Order)

1. Finalize the JSON schema (confirm domain order convention, capture_radius units, time unit documentation, alter object exponent validation)
2. Write the config validator as a standalone tool — forces all edge cases to surface before game logic is written, including alter object range and identity checks
3. Implement core math functions (mix, alter, craft, time modifier, capture zone check, modular exponentiation) in pure C++ with unit tests
4. Wrap in the C API with explicit type validation between coordinate-based objects and alter objects
5. Write the Godot GDExtension binding
6. Build a minimal Godot test scene that loads a config, mixes and alters objects with time values, and prints crafted effects

---

*This document should be read alongside `effect_system_design.md`, which contains the mathematical formulas and effect system logic in full detail.*
