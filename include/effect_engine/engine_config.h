#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace effect_engine {

inline constexpr std::size_t kDimensionsPerDomain = 3;

using DimensionTriple = std::array<int, kDimensionsPerDomain>;

struct DomainConfig {
    std::string id;
};

struct OperationConfig {
    std::string id;
    int maxModifier;
    int timePerPoint;
};

struct AnchorConfig {
    DimensionTriple anchor;
    float captureRadius;
    std::string domain;
};

struct PureEffectConfig {
    std::string id;
    AnchorConfig anchor;
};

struct CombinedEffectConfig {
    std::string id;
    float beta;
    std::vector<AnchorConfig> anchors;
};

struct TierConfig {
    std::string id;
    std::vector<DimensionTriple> dimensions;
};

struct SourceObjectConfig {
    std::string id;
    std::unordered_map<std::string, TierConfig> tiers;
};

struct AlterObjectConfig {
    std::string id;
    std::vector<int> exponents;
};

struct EngineConfig {
    int modulus = 0;
    std::vector<DomainConfig> domains;
    std::unordered_map<std::string, OperationConfig> operations;
    std::vector<PureEffectConfig> pureEffects;
    std::vector<CombinedEffectConfig> combinedEffects;
    std::unordered_map<std::string, SourceObjectConfig> sourceObjects;
    std::unordered_map<std::string, AlterObjectConfig> alterObjects;
};

}
