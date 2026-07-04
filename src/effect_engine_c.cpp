#include "effect_engine/effect_engine_c.h"
#include "effect_engine/engine.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace effect_engine;

// Opaque handle bodies. Only pointers to these cross the FFI boundary.
struct EffectEngine { Engine engine; };
struct EEIngredient { std::shared_ptr<Ingredient> obj; };
struct EECrafted {
    CraftedObject crafted;
    // Stable, index-addressable snapshot of the (unordered) active-domains map so that
    // the id/distance accessors agree across calls for the same index.
    std::vector<std::pair<std::string, float>> activeDomains;
};

namespace {

// Thread-local so concurrent bindings don't clobber each other's error state.
std::string& lastError() {
    static thread_local std::string err;
    return err;
}
void setError(const std::string& msg) { lastError() = msg; }
void clearError() { lastError().clear(); }

// Copies `values` into out[0..cap); returns count, or -1 if out is null / cap too small.
int copyInts(const std::vector<int>& values, int* out, int cap) {
    const int count = static_cast<int>(values.size());
    if (!out || cap < count) { setError("output buffer too small"); return -1; }
    for (int i = 0; i < count; ++i) out[i] = values[i];
    return count;
}

std::vector<int> flattenDimensions(const std::vector<DimensionTriple>& dims) {
    std::vector<int> flat;
    flat.reserve(dims.size() * kDimensionsPerDomain);
    for (const auto& d : dims)
        for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) flat.push_back(d[i]);
    return flat;
}

} // namespace

extern "C" {

const char* effect_engine_last_error(void) { return lastError().c_str(); }

// ---- Lifecycle ----

EffectEngine* effect_engine_load(const char* config_path) {
    clearError();
    if (!config_path) { setError("config_path is null"); return nullptr; }
    try {
        auto* handle = new EffectEngine{};
        handle->engine.load(config_path);
        return handle;
    } catch (const std::exception& ex) {
        setError(ex.what());
        return nullptr;
    }
}

void effect_engine_destroy(EffectEngine* engine) { delete engine; }

// ---- Ingredients ----

EEIngredient* effect_engine_get_source(EffectEngine* engine,
                                       const char* source_id, const char* tier) {
    clearError();
    if (!engine || !source_id || !tier) { setError("null argument"); return nullptr; }
    try {
        SourceObject src = engine->engine.getSourceObjectInfo(source_id, tier);
        return new EEIngredient{ std::make_shared<SourceObject>(std::move(src)) };
    } catch (const std::exception& ex) { setError(ex.what()); return nullptr; }
}

EEIngredient* effect_engine_mix(EffectEngine* engine,
                                const EEIngredient* a, const EEIngredient* b,
                                const char* operation_id, int elapsed_time) {
    clearError();
    if (!engine || !a || !b || !operation_id) { setError("null argument"); return nullptr; }
    try {
        MysteriousObject res =
            engine->engine.mix(*a->obj, *b->obj, operation_id, elapsed_time);
        return new EEIngredient{ std::make_shared<MysteriousObject>(std::move(res)) };
    } catch (const std::exception& ex) { setError(ex.what()); return nullptr; }
}

EEIngredient* effect_engine_alter(EffectEngine* engine,
                                  const EEIngredient* target, const char* alter_object_id) {
    clearError();
    if (!engine || !target || !alter_object_id) { setError("null argument"); return nullptr; }
    try {
        MysteriousObject res = engine->engine.alter(*target->obj, alter_object_id);
        return new EEIngredient{ std::make_shared<MysteriousObject>(std::move(res)) };
    } catch (const std::exception& ex) { setError(ex.what()); return nullptr; }
}

void effect_engine_ingredient_free(EEIngredient* ingredient) { delete ingredient; }

int effect_engine_ingredient_domain_count(const EEIngredient* ingredient) {
    clearError();
    if (!ingredient) { setError("null ingredient"); return -1; }
    return static_cast<int>(ingredient->obj->dimensions.size());
}

int effect_engine_ingredient_dimensions(const EEIngredient* ingredient, int* out, int cap) {
    clearError();
    if (!ingredient) { setError("null ingredient"); return -1; }
    return copyInts(flattenDimensions(ingredient->obj->dimensions), out, cap);
}

// ---- Craft ----

EECrafted* effect_engine_craft(EffectEngine* engine, const EEIngredient* mysterious) {
    clearError();
    if (!engine || !mysterious) { setError("null argument"); return nullptr; }
    // craft() consumes a mysterious object (produced by mix/alter), per the type
    // discipline in the design docs. Reject a raw source object.
    auto* myst = dynamic_cast<MysteriousObject*>(mysterious->obj.get());
    if (!myst) { setError("craft requires a mysterious object (mix or alter first)"); return nullptr; }
    try {
        auto* handle = new EECrafted{};
        handle->crafted = engine->engine.craft(*myst);
        for (const auto& kv : handle->crafted.effect.activeDomains)
            handle->activeDomains.emplace_back(kv.first, kv.second);
        return handle;
    } catch (const std::exception& ex) { setError(ex.what()); return nullptr; }
}

void effect_engine_crafted_free(EECrafted* crafted) { delete crafted; }

const char* effect_engine_crafted_effect_id(const EECrafted* crafted) {
    clearError();
    if (!crafted) { setError("null crafted"); return ""; }
    return crafted->crafted.effect.effectId.c_str();
}

int effect_engine_crafted_dimensions(const EECrafted* crafted, int* out, int cap) {
    clearError();
    if (!crafted) { setError("null crafted"); return -1; }
    return copyInts(flattenDimensions(crafted->crafted.effect.dimensions), out, cap);
}

int effect_engine_crafted_active_domain_count(const EECrafted* crafted) {
    clearError();
    if (!crafted) { setError("null crafted"); return -1; }
    return static_cast<int>(crafted->activeDomains.size());
}

const char* effect_engine_crafted_active_domain_id(const EECrafted* crafted, int index) {
    clearError();
    if (!crafted || index < 0 || index >= static_cast<int>(crafted->activeDomains.size())) {
        setError("active domain index out of range"); return nullptr;
    }
    return crafted->activeDomains[index].first.c_str();
}

float effect_engine_crafted_active_domain_distance(const EECrafted* crafted, int index) {
    clearError();
    if (!crafted || index < 0 || index >= static_cast<int>(crafted->activeDomains.size())) {
        setError("active domain index out of range"); return -1.0f;
    }
    return crafted->activeDomains[index].second;
}

// ---- Alter object inspection ----

int effect_engine_alter_exponents(EffectEngine* engine, const char* alter_object_id,
                                  int* out, int cap) {
    clearError();
    if (!engine || !alter_object_id) { setError("null argument"); return -1; }
    try {
        AlterObject info = engine->engine.getAlterObjectInfo(alter_object_id);
        return copyInts(info.exponents, out, cap);
    } catch (const std::exception& ex) { setError(ex.what()); return -1; }
}

// ---- Config queries for UI ----
// Small linear scans over unordered_maps — negligible for config-scale sizes, and the
// iteration order is stable for a fixed (immutable) config so indices are consistent.

int effect_engine_source_count(EffectEngine* engine) {
    clearError();
    if (!engine) { setError("null engine"); return -1; }
    return static_cast<int>(engine->engine.config().sourceObjects.size());
}

const char* effect_engine_source_id(EffectEngine* engine, int index) {
    clearError();
    if (!engine) { setError("null engine"); return nullptr; }
    const auto& sources = engine->engine.config().sourceObjects;
    if (index < 0 || index >= static_cast<int>(sources.size())) {
        setError("source index out of range"); return nullptr;
    }
    auto it = sources.begin();
    std::advance(it, index);
    return it->first.c_str();
}

int effect_engine_tier_count(EffectEngine* engine, const char* source_id) {
    clearError();
    if (!engine || !source_id) { setError("null argument"); return -1; }
    const auto& sources = engine->engine.config().sourceObjects;
    auto it = sources.find(source_id);
    if (it == sources.end()) { setError("unknown source object"); return -1; }
    return static_cast<int>(it->second.tiers.size());
}

const char* effect_engine_tier_id(EffectEngine* engine, const char* source_id, int index) {
    clearError();
    if (!engine || !source_id) { setError("null argument"); return nullptr; }
    const auto& sources = engine->engine.config().sourceObjects;
    auto srcIt = sources.find(source_id);
    if (srcIt == sources.end()) { setError("unknown source object"); return nullptr; }
    const auto& tiers = srcIt->second.tiers;
    if (index < 0 || index >= static_cast<int>(tiers.size())) {
        setError("tier index out of range"); return nullptr;
    }
    auto it = tiers.begin();
    std::advance(it, index);
    return it->first.c_str();
}

int effect_engine_alter_object_count(EffectEngine* engine) {
    clearError();
    if (!engine) { setError("null engine"); return -1; }
    return static_cast<int>(engine->engine.config().alterObjects.size());
}

const char* effect_engine_alter_object_id(EffectEngine* engine, int index) {
    clearError();
    if (!engine) { setError("null engine"); return nullptr; }
    const auto& alters = engine->engine.config().alterObjects;
    if (index < 0 || index >= static_cast<int>(alters.size())) {
        setError("alter object index out of range"); return nullptr;
    }
    auto it = alters.begin();
    std::advance(it, index);
    return it->first.c_str();
}

int effect_engine_domain_count(EffectEngine* engine) {
    clearError();
    if (!engine) { setError("null engine"); return -1; }
    return static_cast<int>(engine->engine.config().domains.size());
}

const char* effect_engine_domain_id(EffectEngine* engine, int index) {
    clearError();
    if (!engine) { setError("null engine"); return nullptr; }
    const auto& domains = engine->engine.config().domains;
    if (index < 0 || index >= static_cast<int>(domains.size())) {
        setError("domain index out of range"); return nullptr;
    }
    return domains[index].id.c_str();
}

} // extern "C"
