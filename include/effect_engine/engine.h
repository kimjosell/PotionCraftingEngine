#pragma once

#include "effect_engine/engine_config.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace effect_engine {

// ---- Forward declaration (Ingredient <-> Recipe tree is mutually recursive) ----

struct Recipe;

// ---- Ingredient family (coordinate-based value objects) ----

struct Ingredient {
    std::string id;
    std::vector<DimensionTriple> dimensions; // one triple per domain, values in [0, modulus-1]
    virtual ~Ingredient() = default;
    // Polymorphic copy so a Recipe can embed its input ingredients (keeping the
    // concrete subtype and, for a MysteriousObject, its own recipe subtree).
    virtual std::shared_ptr<Ingredient> clone() const = 0;
};

struct SourceObject : Ingredient {
    std::string tier;
    std::shared_ptr<Ingredient> clone() const override {
        return std::make_shared<SourceObject>(*this);
    }
};

struct MysteriousObject : Ingredient {
    std::shared_ptr<Recipe> recipe;
    std::shared_ptr<Ingredient> clone() const override {
        return std::make_shared<MysteriousObject>(*this);
    }
};

// ---- AlterObject (exponent-based — not an Ingredient, cannot be mixed or crafted) ----

struct AlterObject {
    std::string id;
    std::vector<int> exponents; // one exponent per domain, values in [1, modulus-2]
};

// ---- Effect and CraftedObject ----

struct Effect {
    // Null effect (craft landed outside every capture radius): effectId is empty and
    // activeDomains is empty. Otherwise effectId names the dominant pure/combined effect.
    std::string effectId;
    std::vector<DimensionTriple> dimensions;               // final crafted coordinates
    // domain_id -> normalized distance to anchor in [0,1]: 0 = on the anchor (strongest),
    // 1 = on the capture boundary (weakest still active). Strength = 1 - value. See §3.5.
    std::unordered_map<std::string, float> activeDomains;
};

struct CraftedObject {
    std::shared_ptr<Recipe> recipe;
    Effect effect;
};

// ---- Recipe hierarchy ----

struct Recipe {
    std::string operationId;
    virtual ~Recipe() = default;
};

struct MixRecipe : Recipe {
    std::vector<std::shared_ptr<Ingredient>> ingredients;
};

struct AlterRecipe : Recipe {
    std::string alterObjectId;
    std::shared_ptr<Ingredient> ingredient;
};

struct SubtractRecipe : Recipe {
    std::shared_ptr<Ingredient> baseIngredient;
    std::shared_ptr<Ingredient> ingredientToSubtract;
};

// ---- Engine errors ----

class EngineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---- Engine ----

class Engine {
public:
    void load(const std::string& configPath);

    CraftedObject    craft(const MysteriousObject& mysteriousObject) const;
    MysteriousObject mix(const std::vector<std::shared_ptr<Ingredient>>& ingredients,
                         const std::string& operationId) const;
    MysteriousObject subtract(const Ingredient& baseIngredient, const Ingredient& ingredientToSubtract) const;
    MysteriousObject alter(const Ingredient& ingredient,
                           const std::string& alterObjectId) const;

    SourceObject getSourceObjectInfo(const std::string& sourceObjectId,
                                     const std::string& tier) const;
    AlterObject  getAlterObjectInfo(const std::string& alterObjectId) const;

    // Read-only config access — used by the C API for UI list queries.
    const EngineConfig& config() const noexcept { return engineConfig_; }

private:
    EngineConfig engineConfig_;
};

} // namespace effect_engine
