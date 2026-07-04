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

/* Reason for the most recent failed call on the current thread; "" if none. */
EE_API const char* effect_engine_last_error(void);

/* ---- Lifecycle ---- */
EE_API EffectEngine* effect_engine_load(const char* config_path); /* NULL on failure */
EE_API void          effect_engine_destroy(EffectEngine* engine);

/* ---- Ingredients (caller owns the handle; free with ingredient_free) ---- */
EE_API EEIngredient* effect_engine_get_source(EffectEngine* engine,
                                              const char* source_id, const char* tier);
EE_API EEIngredient* effect_engine_mix(EffectEngine* engine,
                                       const EEIngredient* a, const EEIngredient* b,
                                       const char* operation_id, int elapsed_time);
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
EE_API float       effect_engine_crafted_active_domain_distance(const EECrafted* crafted, int index);

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
