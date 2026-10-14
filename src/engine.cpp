#include "effect_engine/engine.h"
#include "effect_engine/config_loader.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace effect_engine {

namespace {
// Handles negative operands (Subtraction); see effect_system_design.md §13.
int mod(int a, int m) { return ((a % m) + m) % m; }

// Euclidean distance between a domain coordinate and an anchor (3 dims).
float domainDistance(const DimensionTriple& p, const DimensionTriple& q) {
    float sum = 0.0f;
    for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
        const float d = static_cast<float>(p[i] - q[i]);
        sum += d * d;
    }
    return std::sqrt(sum);
}

// (base ^ exp) mod m, exp >= 0. Square-and-multiply keeps intermediates bounded.
int modpow(int base, int exp, int m) {
    int result = 1 % m;
    base = mod(base, m);
    while (exp > 0) {
        if (exp & 1) result = mod(result * base, m);
        base = mod(base * base, m);
        exp >>= 1;
    }
    return result;
}

// Normalizes a raw anchor distance into [0,1] against the effect's capture radius:
// 0 = exactly on the anchor (strongest), 1 = on the capture boundary (weakest still
// active). Strength is (1 - this). Radius <= 0 (degenerate anchor that captures only
// its exact center) maps to 0. See effect_system_design.md §3.5.
float normalizedDistance(float dist, float captureRadius) {
    return captureRadius > 0.0f ? dist / captureRadius : 0.0f;
}
} // namespace

void Engine::load(const std::string& configPath) {
    engineConfig_ = loadEngineConfigFromFile(configPath);
}

CraftedObject Engine::craft(const MysteriousObject& mysteriousObject) const {
    const std::size_t domainCount = engineConfig_.domains.size();
    if (mysteriousObject.dimensions.size() != domainCount) {
        throw EngineError("mysterious object domain count does not match engine domain count");
    }

    // domain id -> coordinate index (anchors reference domains by id).
    std::unordered_map<std::string, std::size_t> domainIndex;
    for (std::size_t k = 0; k < domainCount; ++k) {
        domainIndex.emplace(engineConfig_.domains[k].id, k);
    }
    const std::vector<DimensionTriple>& coords = mysteriousObject.dimensions;

    constexpr float kInf = std::numeric_limits<float>::infinity();

    // --- Step 1: dominant pure effect (design paper §6, §10 Step 1) ---
    // Per domain: nearest anchor within its capture radius. Anchors out of range are
    // ignored — there is no dead-zone fallback (a craft with no candidate anywhere
    // yields a null effect in Step 3). Best (smallest) local score across all domains
    // wins. Strict '<' keeps the earlier config entry on ties.
    const PureEffectConfig* dominantPure = nullptr;
    float bestPureScore = kInf;
    for (std::size_t k = 0; k < domainCount; ++k) {
        const std::string& domainId = engineConfig_.domains[k].id;
        const PureEffectConfig* localWinner = nullptr;
        float localScore = kInf;
        for (const PureEffectConfig& pe : engineConfig_.pureEffects) {
            if (pe.anchor.domain != domainId) continue;
            const float dist = domainDistance(coords[k], pe.anchor.anchor);
            if (dist <= pe.anchor.captureRadius && dist < localScore) {
                localScore = dist;
                localWinner = &pe;
            }
        }
        if (localWinner && localScore < bestPureScore) {
            bestPureScore = localScore;
            dominantPure = localWinner;
        }
    }

    // --- Step 2: dominant combined effect (design paper §7, §10 Step 2) ---
    // Candidate only if every participating domain is within its capture radius.
    // Score is the average per-domain distance.
    const CombinedEffectConfig* bestCombined = nullptr;
    float bestCombinedScore = kInf;
    for (const CombinedEffectConfig& ce : engineConfig_.combinedEffects) {
        float sum = 0.0f;
        bool allInRange = true;
        for (const AnchorConfig& a : ce.anchors) {
            auto idxIt = domainIndex.find(a.domain);
            if (idxIt == domainIndex.end()) {
                throw EngineError("combined effect '" + ce.id +
                                  "' references unknown domain: " + a.domain);
            }
            const float dist = domainDistance(coords[idxIt->second], a.anchor);
            if (dist > a.captureRadius) { allInRange = false; break; }
            sum += dist;
        }
        if (!allInRange || ce.anchors.empty()) continue;
        const float score = sum / static_cast<float>(ce.anchors.size());
        if (score < bestCombinedScore) {
            bestCombinedScore = score;
            bestCombined = &ce;
        }
    }

    // --- Step 3: dominance decision (design paper §8, §10 Step 3) ---
    // If a combined effect is a candidate it wins when strictly closer than the pure
    // score scaled by its own β (and always when there is no pure candidate to beat).
    // '<' means an exact tie prefers the pure effect.
    bool combinedWins = false;
    if (bestCombined) {
        combinedWins = !dominantPure ||
                       bestCombinedScore < bestPureScore * bestCombined->beta;
    }

    // Null effect: the craft landed outside every capture radius (pure and combined).
    // Signalled by an empty effectId and empty activeDomains (design paper §5).
    Effect effect{};
    effect.dimensions = coords; // final crafted coordinates are unchanged by craft
    if (combinedWins) {
        effect.effectId = bestCombined->id;
        for (const AnchorConfig& a : bestCombined->anchors) {
            const std::size_t idx = domainIndex.at(a.domain);
            effect.activeDomains[a.domain] =
                normalizedDistance(domainDistance(coords[idx], a.anchor), a.captureRadius);
        }
    } else if (dominantPure) {
        effect.effectId = dominantPure->id;
        const AnchorConfig& pa = dominantPure->anchor;
        effect.activeDomains[pa.domain] = normalizedDistance(bestPureScore, pa.captureRadius);
    }

    CraftedObject crafted{};
    crafted.recipe = mysteriousObject.recipe; // preserved through craft (architecture §10)
    crafted.effect = std::move(effect);
    return crafted;
}

MysteriousObject Engine::subtract(const Ingredient& baseIngredient, const Ingredient& ingredientToSubtract) const {

    const std::size_t domainCount = engineConfig_.domains.size();
    if (baseIngredient.dimensions.size() != domainCount ||
        ingredientToSubtract.dimensions.size() != domainCount) {
        throw EngineError("ingredient domain count does not match engine domain count");
    }

    const int modulus = engineConfig_.modulus;

    MysteriousObject result{};
    result.id = "mysteriousObject";
    result.dimensions.resize(domainCount);
    for (std::size_t k = 0; k < domainCount; ++k) {
        for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
            const int a = baseIngredient.dimensions[k][i];
            const int b = ingredientToSubtract.dimensions[k][i];
            int raw = a - b;
            // arithmetic % mod 
            result.dimensions[k][i] = mod(raw, modulus);
        }
    }

    auto recipe = std::make_shared<SubtractRecipe>();
    recipe->operationId = "subtract";
    recipe->baseIngredient = baseIngredient.clone();
    recipe->ingredientToSubtract = ingredientToSubtract.clone();
    result.recipe = recipe;
    return result;
}

MysteriousObject Engine::mix(const std::vector<std::shared_ptr<Ingredient>>& ingredients,
                             const std::string& operationId) const {
    if (ingredients.empty()){
        throw EngineError("mix operation requires at least one ingredient");
    }

    const std::size_t domainCount = engineConfig_.domains.size();
    for(const auto& ing : ingredients){
        if(!ing || ing->dimensions.size() != domainCount){
            throw EngineError("Ingredient domain count does not match engine domain count");
        }
    }

    const int modulus = engineConfig_.modulus;

    MysteriousObject result{};
    result.id = "mysteriousObject";
    result.dimensions.resize(domainCount);
    for (std::size_t k = 0; k < domainCount; ++k) {
        for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
            
            //raw starts at 1 when operationId is multiplying because if raw starts at 0 multiplying by 0 will led always to 0
            //It also starts at 0 when its addition.
            long long raw = (operationId == "multiply") ? 1 : 0;

            for (const auto& ing : ingredients){
                const int val = ing->dimensions[k][i];
                if(operationId == "add"){
                    raw = mod(raw + val, modulus);
                }
                else if(operationId == "multiply"){
                    raw = mod(raw * val, modulus);
                }
                else throw EngineError("unsupported mix operation " + operationId);
            }
            result.dimensions[k][i] = static_cast<int>(raw);
        }
    }

    auto recipe = std::make_shared<MixRecipe>();
    recipe->operationId = operationId;
    recipe->ingredients.reserve(ingredients.size());
    for(const auto& ing : ingredients) {
        recipe->ingredients.push_back(ing->clone());
    }
    result.recipe = recipe;
    return result;
}

MysteriousObject Engine::alter(const Ingredient& ingredient,
                               const std::string& alterObjectId) const {
    auto it = engineConfig_.alterObjects.find(alterObjectId);
    if (it == engineConfig_.alterObjects.end()) {
        throw EngineError("unknown alter object: " + alterObjectId);
    }

    const std::size_t domainCount = engineConfig_.domains.size();
    if (ingredient.dimensions.size() != domainCount) {
        throw EngineError("ingredient domain count does not match engine domain count");
    }

    const std::vector<int>& exponents = it->second.exponents;
    if (exponents.size() != domainCount) {
        throw EngineError("alter object '" + alterObjectId +
                          "' exponent count does not match engine domain count");
    }

    const int modulus = engineConfig_.modulus;

    MysteriousObject result{};
    result.id = "mysteriousObject";
    result.dimensions.resize(domainCount);
    for (std::size_t k = 0; k < domainCount; ++k) {
        // One exponent per domain, applied uniformly to all 3 dimensions. No time modifier.
        for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
            result.dimensions[k][i] = modpow(ingredient.dimensions[k][i], exponents[k], modulus);
        }
    }

    auto recipe = std::make_shared<AlterRecipe>();
    recipe->operationId   = "exponentiation"; // fixed sentinel (architecture §8.3)
    recipe->alterObjectId = alterObjectId;
    recipe->ingredient    = ingredient.clone();
    result.recipe = recipe;

    return result;
}

SourceObject Engine::getSourceObjectInfo(const std::string& sourceObjectId,
                                         const std::string& tier) const {
    auto srcIt = engineConfig_.sourceObjects.find(sourceObjectId);
    if (srcIt == engineConfig_.sourceObjects.end()) {
        throw EngineError("unknown source object: " + sourceObjectId);
    }

    auto tierIt = srcIt->second.tiers.find(tier);
    if (tierIt == srcIt->second.tiers.end()) {
        throw EngineError("unknown tier '" + tier + "' for source object: " + sourceObjectId);
    }

    SourceObject obj{};
    obj.id         = sourceObjectId;
    obj.tier       = tier;
    obj.dimensions = tierIt->second.dimensions;
    return obj;
}

AlterObject Engine::getAlterObjectInfo(const std::string& alterObjectId) const {
    auto it = engineConfig_.alterObjects.find(alterObjectId);
    if (it == engineConfig_.alterObjects.end()) {
        throw EngineError("unknown alter object: " + alterObjectId);
    }

    AlterObject obj{};
    obj.id        = alterObjectId;
    obj.exponents = it->second.exponents;
    return obj;
}

} // namespace effect_engine
