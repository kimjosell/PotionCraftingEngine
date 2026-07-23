# Effect System Design — Decision Paper

## Overview

This document summarizes the design of an **effect system** for a crafting/mixing game. Players combine and alter "source objects" to produce "crafted objects," which express one dominant **effect** — either a pure effect tied to a single domain, or a combined effect spanning multiple domains. The system is designed to be **opaque but learnable**: players discover patterns through experimentation rather than being handed a formula, but the underlying structure is coherent enough to be reverse-engineered over time.

---

## 1. The Structure of Objects

### Source objects

Source objects are the raw material players work with. Each source object holds an array of **domains**, and each domain holds exactly **3 dimension values**. Source object domain values are bounded to [0, modulus-1] — the same range as crafted objects — because allowing values outside this range creates no mechanical difference (modular arithmetic makes any value outside the range equivalent to its mod-reduced counterpart) and would mislead the designer into thinking two mechanically identical objects are distinct.

Formally, a source object with D domains looks like:

```
source_object = [
  domain_1: [x1, x2, x3],
  domain_2: [x1, x2, x3],
  ...
  domain_D: [x1, x2, x3]
]

where each xᵢ ∈ {0, 1, ..., modulus-1}
```

### Mysterious objects

Mysterious objects are the intermediate result of mix or alter operations. They have the same structure as source objects — D domains, each with 3 dimension values, all in [0, modulus-1]. Modulus is applied after every operation, so mysterious objects are always bounded. They are never crafted to an effect directly — the player decides when to craft them.

### Alter objects

Alter objects are a separate object type whose only purpose is to act as the exponent modifier in alter operations. They have a different shape from source/mysterious/crafted objects:

```
alter_object = [
  exponent_1,   // exponent applied to all 3 dimensions of domain 1
  exponent_2,   // exponent applied to all 3 dimensions of domain 2
  ...
  exponent_D    // exponent applied to all 3 dimensions of domain D
]

where each exponent ∈ {1, 2, ..., modulus-2}
```

Note the structural asymmetry: alter objects hold **one exponent per domain**, not 3 dimension values per domain. Within a single domain, all 3 dimensions of the target object are raised to the same exponent.

**Exponent range: `[1, modulus-2]`**

For modulus 11, this is `[1, 9]`. The bounds come from the cyclic behavior of modular exponentiation:

- The exponent **1** is the identity: the domain is left unchanged. This allows the designer to define "selective" alter objects that only affect specific domains. For example, `[1, 5, 1, 1]` modifies only the second domain and leaves the others untouched.
- The exponents **0 and `modulus-1`** are excluded because both collapse values to a fixed point (1) under modular arithmetic with prime modulus — a degenerate mechanic that should be treated as a special case, not a standard value.
- Exponents **greater than `modulus-2`** are excluded because they are mechanically equivalent to a smaller exponent by Fermat's Little Theorem (`a^(modulus-1) ≡ 1 mod modulus` for prime modulus), creating hidden duplication in the catalog.

**Additional validation:** an alter object must have at least one exponent different from 1. Otherwise it is the identity operation in disguise and has no mechanical utility.

Alter objects are **not interchangeable** with source/mysterious/crafted objects. They cannot be used as ingredients in mix operations (add, subtract, multiply), and they cannot be the target of an alter operation. They serve exclusively as the exponent input to alter.

Alter objects are defined in config and are immutable. They have no tiers — each alter object is its own distinct entity with its own exponent pattern, named and used directly.

### Crafted objects

Crafted objects are produced by crafting a mysterious object. They have the same domain structure and the same value bounds. The act of crafting triggers the effect calculation — the only expensive step in the system.

Formally, all three object types share the same coordinate structure:

```
object = [
  domain_1: (x₁, x₂, x₃),
  domain_2: (x₁, x₂, x₃),
  domain_3: (x₁, x₂, x₃),
  domain_4: (x₁, x₂, x₃)
]

where each xᵢ ∈ {0, 1, ..., modulus-1}
```

### The meaning of position within a domain

Each domain cube has a **neutral center at (c, c, c)** where `c = floor(modulus / 2)`. For modulus 11, the center is (5, 5, 5). The distance from this center is meaningful:

- **Near center** → weak, neutral character in that domain
- **Near a corner** → strong, extreme character in that domain
- Maximum possible distance from center to any corner: √(c² + c² + c²) = c × √3

For modulus 11: max distance = 5 × √3 ≈ **8.66**

---

## 2. Producing Objects

### Mixing (two objects → one mysterious object)

Two objects (source or mysterious) can be combined via addition, subtraction, or multiplication. The operation is applied **dimension-by-dimension within each domain**, and modulus is applied immediately after each operation:

```
result.domain[k].dim[i] = (obj_a.domain[k].dim[i]  <op>  obj_b.domain[k].dim[i]) mod modulus

where <op> is one of: +, −, ×
and k iterates over all domains, i iterates over all 3 dimensions
```

### Altering (one object + one alter object → one mysterious object)

Alter is a binary operation: it takes a target object (source or mysterious) and an alter object, and produces a mysterious object. Each domain of the target is raised to the corresponding exponent from the alter object. The exponent applies uniformly to all 3 dimensions within that domain. Modulus is applied immediately:

```
result.domain[k].dim[i] = (target.domain[k].dim[i] ^ alter_obj.exponent[k]) mod modulus

where k iterates over all domains, i iterates over all 3 dimensions
```

**Type rules for alter:**
- The target must be a source object or a mysterious object — never an alter object.
- The exponent input must be an alter object — never a source/mysterious/crafted object.
- The output is always a mysterious object.
- An alter object cannot participate in mix operations (add, subtract, multiply).

**No time modifier on alter.** Unlike mix operations, alter does not apply a time modifier. This is a deliberate design choice with two consequences:

1. The exponent 1 in any domain produces a *literally* unchanged result for that domain — bit-for-bit identical to the input. This is what makes "selective" alter objects (e.g., `[1, 5, 1, 1]`) meaningful as a mechanic: the player can protect specific domains from any modification.
2. Alter is the only operation in the system that can leave parts of an object completely untouched. Mix operations always introduce some change via the time modifier; alter does not.

### Why modulus is applied after every operation, not only at craft

Deferring modulus to craft time creates integer overflow risk — multiplication chains can produce values of 10^20+ within a handful of operations, overflowing any integer type. Applying mod at every step keeps values permanently bounded. This does not hurt gameplay because players never see intermediate raw values — they observe effects after crafting. The mod does not make a mysterious object into a crafted object; **effect calculation** is what does that.

### Time modifier

After the arithmetic of a **mix** operation is applied and modded, a **time modifier** is computed from the elapsed time passed by the game and added uniformly to every dimension of every domain, with mod applied again. Alter operations do not use a time modifier:

```
step 1: raw.domain[k].dim[i]    = (obj_a.domain[k].dim[i] <op> obj_b.domain[k].dim[i]) mod modulus
step 2: modifier                 = floor(elapsed_time / time_per_point) mod (max_modifier + 1)
step 3: result.domain[k].dim[i] = (raw.domain[k].dim[i] + modifier) mod modulus
```

`time_per_point` and `max_modifier` are configured per mix operation type (add, subtract, multiply). Alter has no time configuration. The modifier cycles back to 0 after `(max_modifier + 1) × time_per_point` time units — waiting indefinitely does not push values in one direction forever. The library has no internal clock; elapsed time is always passed in by the game per operation call.

### Recipes — operation history

Every mysterious object carries a **recipe**: a tree describing the operations and ingredients that produced it. A `Mix` recipe node holds its operation id, the elapsed time used, and references to its two input ingredients. An `Alter` recipe node holds the alter object id and a reference to its target ingredient. Because each input ingredient may itself be a mysterious object with its own recipe, recipes nest naturally as trees with source objects at the leaves.

Recipes are passed through to crafted objects. They serve three purposes: replay (the game can reconstruct a craft from its recipe given the same engine config), inspection (the game can display the operation history to the player), and debugging. The engine builds recipes as a side effect of mix/alter and attaches them to the returned mysterious object — it does not persist them. Serialization is the game's responsibility.

See `effect_engine_architecture.md` §8 for the full Recipe class hierarchy and serialization considerations.

---

## 3. Effects

Every crafted object expresses exactly **one dominant effect**. There are two kinds.

### 3.1 Pure effects

A pure effect is defined within a **single domain**. Each domain has its own independent set of pure effect anchors — anchors are not shared across domains. An anchor is a 3D coordinate point (x₁, x₂, x₃) inside that domain's cube, plus a **capture radius**.

### 3.2 Combined effects

A combined effect spans **two or more domains simultaneously**. It has one anchor coordinate and one capture radius per participating domain. Combined effects must span at least 2 domains and at most the total number of domains — validated at config load time.

For a crafted object to be a candidate for a combined effect, its domain coordinate must fall within the capture radius of the combined effect's anchor **in every participating domain simultaneously**. This is what makes combined effects rare.

### 3.3 Capture radius

Every effect anchor (pure or combined) has its own **capture radius** `r` — the radius of the sphere around the anchor within which a crafted object's domain coordinate must land for that effect to be a candidate in the dominance competition. If the domain coordinate falls outside the sphere, the effect is not considered.

Capture radius is configured per effect, giving the designer independent control over how accessible each effect is. A common effect gets a larger radius; a rare or powerful effect gets a tighter one.

### 3.4 Player-visible attribution

Players are told **which domain a pure effect belongs to** and **which domains a combined effect spans**. This is the primary feedback mechanism — it allows players to build a mental model without being given distances or formulas.

### 3.5 Active domains

Alongside the dominant effect identity, every crafted object exposes a map of **active domains** — the domains that participate in the winning effect, each paired with the crafted object's **normalized distance** from that effect's anchor in that domain.

```
active_domains: Map<domain_id, normalized_distance>   // in [0, 1]
```

The value is the raw Euclidean distance to the anchor **divided by that anchor's `capture_radius`**, which places it on a fixed `[0, 1]` scale regardless of how large or small the radius is:

- `0` — the coordinate sits exactly on the anchor. Strongest possible expression of the effect.
- `1` — the coordinate sits on the capture boundary (`distance == capture_radius`). Weakest still-active expression; anything past this is a null effect and does not appear in the map at all.

Strength is therefore `1 - normalized_distance` (1 at the anchor, 0 at the boundary).

**Why normalized and not raw.** A raw distance is only comparable within a single anchor: a raw `3.0` means "at the edge" for a radius-3 effect but "halfway in" for a radius-6 effect. Normalizing against each effect's own `capture_radius` makes the value comparable across effects with different radii — `0.5` always means "halfway between the anchor and the boundary", so a strength bar reads consistently no matter which effect won. (A degenerate anchor with `capture_radius <= 0` captures only its exact center and reports `0`.)

For a pure effect, the map contains exactly one entry: the domain the pure effect belongs to. For a combined effect, the map contains one entry per participating domain, each normalized against that domain's own combined anchor radius.

This is **structural feedback** for presentation (a "purity" indicator, a strength bar, or nothing at all) — it is not the score used to pick the winning effect. Dominance is still decided internally on raw distances (§6–§8); the normalized value is only what the crafted object reports outward.

---

## 4. Distance Formulas

### Distance within a single domain

For a crafted object's domain coordinate `dₖ = (x₁, x₂, x₃)` and an anchor point `P = (P₁, P₂, P₃)`:

```
dist(dₖ, P) = √[ (x₁ − P₁)² + (x₂ − P₂)² + (x₃ − P₃)² ]
```

Range: [0, c×√3] where c = floor(modulus/2). For modulus 11: [0, 8.66].

### Distance to center (per domain)

```
dist_center(dₖ) = √[ (x₁ − c)² + (x₂ − c)² + (x₃ − c)² ]
```

Used as a signal of how "opinionated" a domain's position is. Low = neutral, high = strong character.

### Distance to a combined effect anchor

A combined effect `E` spans domains {k₁, k₂, ..., kₘ}. Its score is the **average per-domain distance** across all participating domains:

```
score_combined(R, E) = (1/m) × Σⱼ dist(dₖⱼ, Eₖⱼ)
```

Using the average rather than a raw Euclidean sum keeps scores on the same scale regardless of how many domains the effect spans, enabling direct comparison with pure effect scores.

---

## 5. Effect Candidacy

Before the dominance competition runs, effects that are out of range are eliminated. An effect is a **candidate** only if:

**For pure effects:** `dist(dₖ, anchor) ≤ capture_radius` for the effect's domain k.

**For combined effects:** `dist(dₖⱼ, anchorₖⱼ) ≤ capture_radiusₖⱼ` for **every** participating domain j.

If no effects are candidates (the crafted object lands in a "dead zone" far from all anchors), the craft resolves to a **null effect** — there is no dead-zone fallback to the nearest pure effect. A null effect is a valid crafted result: the crafted object still carries its coordinates and recipe, but expresses no dominant effect (empty effect id, empty active-domains map). A crafted object therefore has **at most one** dominant effect: exactly one when at least one effect is a candidate, and none when the object lands in a dead zone.

---

## 6. Determining the Dominant Pure Effect

### Step 1 — Per-domain winner

Each domain independently finds the nearest pure effect anchor among candidates:

```
local_score(k)  = min over candidate anchors p in domain k:  dist(dₖ, Pₖ_p)
local_winner(k) = anchor p achieving that minimum
```

Only anchors within their capture radius are candidates. If a domain has no candidate, it contributes no local winner (there is no fallback to the nearest out-of-range anchor).

### Step 2 — Global dominant pure effect

The dominant pure effect is whichever domain produced the smallest local score:

```
best_pure_score  = min over k:  local_score(k)
best_pure_domain = k achieving that minimum
dominant_pure    = local_winner(best_pure_domain)
```

If no domain produced a candidate, there is no dominant pure effect (`dominant_pure = none`).

**Design rationale:** "Strongest signal wins" — the domain most precisely expressing an effect speaks for the whole object. Players know which domain an effect comes from, so they can learn to deliberately strengthen specific domains over time.

---

## 7. Determining the Dominant Combined Effect

For each combined effect `E` where all participating domains are within capture radius:

```
score_combined(E) = (1/m) × Σⱼ dist(dₖⱼ, Eₖⱼ)
```

The candidate combined effect is the one with the lowest score:

```
best_combined_score  = min over candidate combined effects E:  score_combined(E)
best_combined_effect = E achieving that minimum
```

---

## 8. The Dominance Decision — Pure vs. Combined

Each combined effect has its own **β parameter** that controls how it competes against the best pure effect:

```
combined effect E wins  if:  best_combined_score < best_pure_score × βₑ
pure effect wins        otherwise
```

### The β parameter

| β value | Meaning |
|---|---|
| β = 1.0 | Fair fight — geometrically closer effect wins |
| β < 1.0 | Combined effect handicapped — must be significantly closer than pure to win |
| β > 1.0 | Combined effect gets a reach bonus — can win even if somewhat farther |

**Design rationale:** Combined effects spanning more domains are geometrically rarer by default (they require simultaneous proximity in multiple independent domains). A higher β on a 4-domain combined effect compensates for that geometric rarity, making it reachable without making it trivial. This is the designer's explicit responsibility to tune.

If multiple combined effects are candidates, the one with the lowest `score_combined` competes against the best pure score using its own β. Only the winning combined effect (if any) enters the final decision.

---

## 9. Rarity

### Why combined effects are rarer

A pure effect's capture zone is a sphere of radius `r` inside one domain's cube. The probability that a random crafted object lands inside it:

```
p_pure ≈ (4/3 × π × r³) / modulus³
```

A combined effect spanning `m` domains requires simultaneous proximity in all participating domains. Because domains are independent:

```
p_combined(m) ≈ p_pure_1 × p_pure_2 × ... × p_pure_m
```

Where each factor is the per-domain capture probability using that domain's capture radius. Each additional domain multiplies rarity by another factor less than 1.

### Example rarity table (modulus=11, r=3 for all effects)

```
p_pure (one domain)          ≈ 0.085   →  ~1 in 12
p_combined (m=2 domains)     ≈ 0.0072  →  ~1 in 139
p_combined (m=3 domains)     ≈ 0.00061 →  ~1 in 1,630
p_combined (m=4 domains)     ≈ 0.000052→  ~1 in 19,200
```

### Tuning rarity

The designer has three independent levers:

| Lever | Effect |
|---|---|
| `capture_radius` per effect | Controls how large the landing zone is — directly sets geometric rarity |
| `beta` per combined effect | Controls how combined effects compete against pure — independent of geometry |
| Combined effect domain count | More domains = exponentially rarer by default |

---

## 10. Complete Decision Algorithm

```
Input:  crafted object R = [d1, d2, d3, d4]  (each dₖ is a 3D coordinate)
        pure effect anchors Pₖ for each domain k (each with capture_radius)
        combined effect anchors E (each with per-domain anchors, capture_radii, and β)

Step 1 — Pure effect candidates
  For each domain k:
    For each pure effect anchor p in domain k:
      if dist(dₖ, Pₖ_p) ≤ capture_radius(p):
        add p to candidates(k)
    local_score(k)  = min dist over candidates(k)
                      (if no candidates: domain k has no local winner)
    local_winner(k) = anchor achieving local_score(k)

  best_pure_score  = min over k: local_score(k)   (none if no domain had a candidate)
  best_pure_domain = k achieving that minimum
  dominant_pure    = local_winner(best_pure_domain)  (none if best_pure_score is none)

Step 2 — Combined effect candidates
  For each combined effect E spanning domains {k₁, ..., kₘ}:
    if dist(dₖⱼ, Eₖⱼ) ≤ capture_radius(E, kⱼ) for ALL j:
      score(E) = (1/m) × Σⱼ dist(dₖⱼ, Eₖⱼ)
      add E to combined_candidates

  best_combined_effect = combined effect E with lowest score(E)
  best_combined_score  = score(best_combined_effect)

Step 3 — Dominance decision
  if combined_candidates is not empty:
    if dominant_pure is none:
      dominant effect = best_combined_effect        // no pure competitor
    else:
      threshold = best_pure_score × β(best_combined_effect)
      if best_combined_score < threshold:
        dominant effect = best_combined_effect
      else:
        dominant effect = dominant_pure
  else if dominant_pure is not none:
    dominant effect = dominant_pure
  else:
    dominant effect = null            // dead zone: no candidate at all

Output: at most one dominant effect (pure or combined) with domain attribution,
        or a null effect if the crafted object landed in a dead zone
```

---

## 11. Design Principles Summary

| Principle | Decision |
|---|---|
| One effect per crafted object | At most one — one dominant effect when in range, null effect in a dead zone (no fallback) |
| Pure effects shared across domains | No — each domain has its own independent anchor set |
| Combined effects require all participating domains in range | Yes — all must be within capture radius simultaneously |
| Dominant pure effect selection | Strongest signal wins (smallest distance, any domain) |
| Pure vs. combined competition | Per-combined-effect β threshold |
| Capture radius | Per effect anchor — pure and combined each define their own |
| Player feedback | Domain attribution is visible; distances and formulas are not |
| Discoverability | Opaque but learnable — players reverse-engineer through mixing patterns |
| Primary rarity knobs | capture_radius per effect, β per combined effect, domain count |

---

## 12. Parameters Reference

| Parameter | Symbol | Role | Configured at |
|---|---|---|---|
| Combined vs. pure bias | β | Threshold multiplier in dominance decision | Per combined effect |
| Capture radius | r | Defines the landing zone sphere around each anchor | Per effect anchor |
| Modulus | m | Value range per dimension: [0, m-1] | Engine block |
| Number of domains | D | Fixed per config | Domains block |
| Dimensions per domain | 3 | Fixed by engine — not configurable | — |
| Neutral center per domain | c = floor(m/2) | Reference point for strength signals | Derived from modulus |
| Max distance in one domain | c × √3 | Normalizes scores to [0,1] if needed | Derived from modulus |
| Time max modifier | — | Max points time can add before cycling | Per mix operation type |
| Time per point | — | Time units per modifier increment | Per mix operation type |
| Alter object exponent range | — | `[1, modulus-2]` for prime modulus | Derived from modulus |
| Alter object exponents | — | One exponent per domain | Per alter object |

---

## 13. Implementation Notes

### Negative modulo

Subtraction can produce negative intermediate values. Ensure the modulo implementation handles this correctly:

```cpp
int mod(int a, int m) { return ((a % m) + m) % m; }
```

### Modular exponentiation cycles

Under prime modulus `p`, exponents are cyclic with period `p-1` by Fermat's Little Theorem:

```
a^(p-1) ≡ 1 (mod p)   for all a ∈ {1, 2, ..., p-1}
```

For modulus 11, this means exponents cycle every 10 steps: `a^11 ≡ a^1`, `a^12 ≡ a^2`, etc. This is the structural reason why alter object exponents are bounded to `[1, modulus-2]`:

- Exponent 0 collapses any value to 1 (`a^0 = 1` for all a, except the special case `0^0` which is 1 by convention).
- Exponent `modulus-1` collapses non-zero values to 1 by Fermat's theorem (only 0 stays as 0).
- Exponents `≥ modulus-1` are mechanically equivalent to `exponent mod (modulus-1)`, creating hidden duplication.

**Behavioral note for designers:** within the `[1, modulus-2]` range, exponents have qualitatively different behaviors depending on their relationship to `modulus-1`:

- **Exponents coprime with `modulus-1`** are **bijections** — they permute values without collapsing them. For modulus 11 these are `{3, 7, 9}`. Alter objects using these exponents reorganize the domain space without losing information.
- **Exponents sharing factors with `modulus-1`** **collapse** multiple values into fewer outputs — they lose information. For modulus 11 these are `{2, 4, 5, 6, 8}`. They produce more aggressive transformations that concentrate the value space.
- **Exponent 5 with modulus 11** is particularly extreme: it maps the 11 possible values to only 3 outputs (`{0, 1, 10}`). Domains with this exponent end up heavily concentrated.

This is not enforced by the engine — designers can use any exponent in `[1, modulus-2]`. It is documented as a tuning consideration when defining alter objects with intended thematic identities.

### Normalization (optional)

To display scores to players or compare across configs, normalize by the maximum possible distance:

```
normalized_score = raw_distance / (floor(modulus/2) × √3)
```

This maps all scores to [0, 1] where 0 = exactly on the anchor, 1 = as far as possible.

### Defining anchors

- **Spacing:** Pure effect anchors within a domain should be spread out enough that most crafted objects have a clear nearest neighbor. Anchors too close together create ambiguous zones.
- **Combined anchor placement:** Place combined anchors in regions reachable by meaningful mixing operations — otherwise combined effects may never appear in practice.
- **Capture radius vs. β:** capture_radius controls whether an effect is a candidate at all. β controls how it competes once it is a candidate. These are independent knobs and should be tuned separately.

### Tiebreaking

If two effects produce identical scores (exact tie):
1. Prefer the effect with the lower index in the config definition list, or
2. Prefer pure over combined (reinforcing the rarity hierarchy)

---

*This document should be read alongside `effect_engine_architecture.md`, which covers the library architecture, API design, JSON config schema, and implementation decisions.*
