#include "effect_engine/engine.h"
#include "effect_engine/config_loader.h"

#include <cmath>
#include <limits>

namespace effect_engine {

namespace {
// Handles negative operands (subtraction); see effect_system_design.md §13.
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
    // Per domain: nearest anchor within its capture radius; if none is in range,
    // fall back to the nearest anchor regardless of radius. Best (smallest) local
    // score across all domains wins. Strict '<' keeps the earlier config entry on ties.
    const PureEffectConfig* dominantPure = nullptr;
    float bestPureScore = kInf;
    for (std::size_t k = 0; k < domainCount; ++k) {
        const std::string& domainId = engineConfig_.domains[k].id;
        const PureEffectConfig* candidateWinner = nullptr;
        float candidateScore = kInf;
        const PureEffectConfig* anyWinner = nullptr;
        float anyScore = kInf;
        for (const PureEffectConfig& pe : engineConfig_.pureEffects) {
            if (pe.anchor.domain != domainId) continue;
            const float dist = domainDistance(coords[k], pe.anchor.anchor);
            if (dist < anyScore) { anyScore = dist; anyWinner = &pe; }
            if (dist <= pe.anchor.captureRadius && dist < candidateScore) {
                candidateScore = dist;
                candidateWinner = &pe;
            }
        }
        const PureEffectConfig* localWinner = candidateWinner ? candidateWinner : anyWinner;
        const float localScore = candidateWinner ? candidateScore : anyScore;
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
    // Combined wins only if strictly closer than the pure score scaled by its own β;
    // '<' means an exact tie prefers the pure effect.
    const bool combinedWins =
        bestCombined && bestCombinedScore < bestPureScore * bestCombined->beta;

    if (!combinedWins && !dominantPure) {
        throw EngineError("no effect could be resolved: config has no pure effects");
    }

    Effect effect{};
    effect.dimensions = coords; // final crafted coordinates are unchanged by craft
    if (combinedWins) {
        effect.effectId = bestCombined->id;
        for (const AnchorConfig& a : bestCombined->anchors) {
            const std::size_t idx = domainIndex.at(a.domain);
            effect.activeDomains[a.domain] = domainDistance(coords[idx], a.anchor);
        }
    } else {
        effect.effectId = dominantPure->id;
        effect.activeDomains[dominantPure->anchor.domain] = bestPureScore;
    }

    CraftedObject crafted{};
    crafted.recipe = mysteriousObject.recipe; // preserved through craft (architecture §10)
    crafted.effect = std::move(effect);
    return crafted;
}

MysteriousObject Engine::mix(const Ingredient& ingredientA, const Ingredient& ingredientB,
                             const std::string& operationId, int elapsedTime) const {
    auto opIt = engineConfig_.operations.find(operationId);
    if (opIt == engineConfig_.operations.end()) {
        throw EngineError("unknown mix operation: " + operationId);
    }

    const std::size_t domainCount = engineConfig_.domains.size();
    if (ingredientA.dimensions.size() != domainCount ||
        ingredientB.dimensions.size() != domainCount) {
        throw EngineError("ingredient domain count does not match engine domain count");
    }

    const int modulus = engineConfig_.modulus;
    const OperationConfig& op = opIt->second;

    // step 2: time modifier — uniform across every dimension of every domain.
    // ponytail: guard time_per_point==0 (config loader doesn't enforce >0) → no division by zero.
    const int modifier = op.timePerPoint > 0
        ? mod(elapsedTime / op.timePerPoint, op.maxModifier + 1)
        : 0;

    MysteriousObject result{};
    result.id = "mysteriousObject";
    result.dimensions.resize(domainCount);
    for (std::size_t k = 0; k < domainCount; ++k) {
        for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
            const int a = ingredientA.dimensions[k][i];
            const int b = ingredientB.dimensions[k][i];
            int raw;
            if (operationId == "add")           raw = a + b;
            else if (operationId == "subtract") raw = a - b;
            else if (operationId == "multiply") raw = a * b;
            else throw EngineError("unsupported mix operation: " + operationId);
            // step 1: arithmetic % mod  →  step 3: (raw + modifier) % mod
            result.dimensions[k][i] = mod(mod(raw, modulus) + modifier, modulus);
        }
    }

    auto recipe = std::make_shared<MixRecipe>();
    recipe->operationId = operationId;
    recipe->time        = elapsedTime;
    recipe->ingredientA = ingredientA.clone();
    recipe->ingredientB = ingredientB.clone();
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
