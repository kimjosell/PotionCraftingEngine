/* Effect Engine — C API for FFI (Godot GDExtension, Unity P/Invoke, Unreal).
 *
 * Design decisions (see effect_engine_architecture.md §10, §15):
 *  - Objects cross the boundary as OPAQUE HANDLES owned by the caller. Ingredients
 *    (source/mysterious) and crafted objects are heap-allocated by the engine and
 *    freed by the game with the matching *_free function. This keeps the recipe tree
 *    alive on the C++ side and avoids exposing struct layout to the ABI.
 *  - Errors: functions return NULL (handles) or -1 (int accessors) on failure. The
 *    reason is retrievable via effect_engine_last_error() (thread-local, "" if none).
 *  - Coordinates and exponents are read out as flat int arrays via accessor functions.
 *
 * All returned `const char*` from query functions point to engine-owned storage and
 * stay valid until effect_engine_destroy(). Copy them if you need them longer.
 */
#ifndef EFFECT_ENGINE_C_H
#define EFFECT_ENGINE_C_H

#include <cstddef>
#if defined(_WIN32)
  #if defined(EFFECT_ENGINE_BUILD)
    #define EE_API __declspec(dllexport)
  #else
    #define EE_API __declspec(dllimport)
  #endif
#else
  #define EE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EffectEngine EffectEngine; /* engine + loaded config */
typedef struct EEIngredient EEIngredient; /* source or mysterious object */
typedef struct EECrafted    EECrafted;    /* result of craft */

/* A NON-OWNING view of one node in an ingredient's recipe tree. Valid only while the
   EEIngredient / EECrafted it was obtained from is alive. NEVER free it. See the recipe
   tree section below. */
typedef struct EERecipeNode EERecipeNode;

/* Kind of a recipe-tree node. A node models an ingredient in the tree:
   a SOURCE leaf, or a MYSTERIOUS branch produced by a MIX (binary) or ALTER (unary). */
typedef enum {
    EE_NODE_SOURCE    = 0, /* leaf: SourceObject — has id + tier + dimensions, 0 children     */
    EE_NODE_MIX       = 1, /* branch: MixRecipe — operation_id + time, N children             */
    EE_NODE_ALTER     = 2, /* branch: AlterRecipe — operation_id + alter_object_id, 1 child   */
    EE_NODE_SUBTRACT = 3  /* branch: subtractRecipe — operation_id + time, 2 children (base, tosubtract) */
} EENodeKind;

/* Reason for the most recent failed call on the current thread; "" if none. */
EE_API const char* effect_engine_last_error(void);

/* ---- Lifecycle ---- */
EE_API EffectEngine* effect_engine_load(const char* config_path); /* NULL on failure */
EE_API void          effect_engine_destroy(EffectEngine* engine);

/* ---- Ingredients (caller owns the handle; free with ingredient_free) ---- */
EE_API EEIngredient* effect_engine_get_source(EffectEngine* engine,
                                              const char* source_id, const char* tier);
EE_API EEIngredient* effect_engine_mix(EffectEngine* engine,
                                const EEIngredient* const* ingredients,
                                size_t count,
                                const char* operation_id);

EE_API EEIngredient* effect_engine_subtract(EffectEngine* engine, const EEIngredient* a, const EEIngredient* b);

EE_API EEIngredient* effect_engine_alter(EffectEngine* engine,
                                         const EEIngredient* target,
                                         const char* alter_object_id);
EE_API void          effect_engine_ingredient_free(EEIngredient* ingredient);

/* Number of domains (dimensions() writes domain_count*3 ints). */
EE_API int effect_engine_ingredient_domain_count(const EEIngredient* ingredient);
/* Writes dimensions row-major: out[domain*3 + dim]. Returns count written, or -1
   if `out` is NULL or `cap` is smaller than domain_count*3. */
EE_API int effect_engine_ingredient_dimensions(const EEIngredient* ingredient,
                                               int* out, int cap);

/* ---- Craft (caller owns the handle; free with crafted_free) ---- */
EE_API EECrafted* effect_engine_craft(EffectEngine* engine, const EEIngredient* mysterious);
EE_API void       effect_engine_crafted_free(EECrafted* crafted);

/* Dominant effect id; "" (empty string) means a null effect — the craft landed in a
   dead zone outside every capture radius (see design paper §5). */
EE_API const char* effect_engine_crafted_effect_id(const EECrafted* crafted);
EE_API int         effect_engine_crafted_dimensions(const EECrafted* crafted,
                                                    int* out, int cap);
/* Active domains: one entry per participating domain (0 for a null effect). */
EE_API int         effect_engine_crafted_active_domain_count(const EECrafted* crafted);
EE_API const char* effect_engine_crafted_active_domain_id(const EECrafted* crafted, int index);
/* Normalized distance to the domain's anchor in [0,1]: 0 = on the anchor (strongest),
   1 = on the capture boundary (weakest still active). Strength = 1 - value. Already
   divided by the anchor's capture_radius, so it is comparable across effects with
   different radii. Returns -1 on a bad index. See design paper §3.5. */
EE_API float       effect_engine_crafted_active_domain_distance(const EECrafted* crafted, int index);
/* Single normalized distance for the whole effect in [0,1]: the mean of the per-domain
   values above. 0 = on every anchor (strongest), 1 = on the capture boundary in every
   domain (weakest). Strength is (1 - value). Returns 1 for a null effect, -1 on a null
   argument. See design paper §3.5. */
EE_API float       effect_engine_crafted_effect_distance(const EECrafted* crafted);

/* ---- Recipe tree traversal (non-owning views) ----
   Lets the caller walk the binary tree of how an object was built: which operation
   produced it and, recursively, the ingredients that went in. Every returned node is a
   view into the parent handle's tree and stays valid until that EEIngredient/EECrafted is
   freed — do NOT free nodes, and do NOT keep them past the parent's lifetime. */

/* Root node of the tree. For an ingredient this is the ingredient itself (a SOURCE leaf,
   or a MYSTERIOUS branch). For a crafted object it is the mysterious object that was
   crafted. Returns NULL on a null argument. */
EE_API const EERecipeNode* effect_engine_ingredient_root(const EEIngredient* ingredient);
EE_API const EERecipeNode* effect_engine_crafted_root(const EECrafted* crafted);

/* Node kind (EENodeKind), or -1 on error. */
EE_API int         effect_engine_node_kind(const EERecipeNode* node);
/* Ingredient id at this node ("mysteriousObject" for a branch), or "" on error. */
EE_API const char* effect_engine_node_id(const EERecipeNode* node);
/* Tier — SOURCE nodes only; "" for branches or on error. */
EE_API const char* effect_engine_node_tier(const EERecipeNode* node);
/* Operation id — MIX/ALTER nodes only; "" for a SOURCE leaf or on error. */
EE_API const char* effect_engine_node_operation_id(const EERecipeNode* node);
/* Alter object id — ALTER nodes only; "" otherwise. */
EE_API const char* effect_engine_node_alter_object_id(const EERecipeNode* node);

/* Dimensions of the ingredient at this node — same flat layout as
   effect_engine_ingredient_dimensions (out[domain*3 + dim]). */
EE_API int effect_engine_node_domain_count(const EERecipeNode* node);
EE_API int effect_engine_node_dimensions(const EERecipeNode* node, int* out, int cap);

/* Children (non-owning views). SOURCE: 0, ALTER: 1, MIX: 2. child() returns NULL if index
   is out of range for this node's kind. */
EE_API int                 effect_engine_node_child_count(const EERecipeNode* node);
EE_API const EERecipeNode* effect_engine_node_child(const EERecipeNode* node, int index);

/* ---- Alter object inspection ---- */
/* Writes domain_count exponents into out. Returns count, or -1 on error/small buffer. */
EE_API int effect_engine_alter_exponents(EffectEngine* engine, const char* alter_object_id,
                                         int* out, int cap);

/* ---- Config queries for UI (count + index accessors) ---- */
EE_API int         effect_engine_source_count(EffectEngine* engine);
EE_API const char* effect_engine_source_id(EffectEngine* engine, int index);
EE_API int         effect_engine_tier_count(EffectEngine* engine, const char* source_id);
EE_API const char* effect_engine_tier_id(EffectEngine* engine, const char* source_id, int index);
EE_API int         effect_engine_alter_object_count(EffectEngine* engine);
EE_API const char* effect_engine_alter_object_id(EffectEngine* engine, int index);
EE_API int         effect_engine_domain_count(EffectEngine* engine);
EE_API const char* effect_engine_domain_id(EffectEngine* engine, int index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EFFECT_ENGINE_C_H */
