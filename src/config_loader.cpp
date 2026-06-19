#include "effect_engine/config_loader.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace effect_engine {

namespace {

using json = nlohmann::json;

DimensionTriple parseDimensionTriple(const json& j) {
    if (!j.is_array() || j.size() != kDimensionsPerDomain) {
        throw ConfigParseError("expected array of 3 integers for dimension triple");
    }
    DimensionTriple out{};
    for (std::size_t i = 0; i < kDimensionsPerDomain; ++i) {
        out[i] = j[i].get<int>();
    }
    return out;
}

OperationConfig parseOperation(const std::string& id, const json& j) {
    OperationConfig op{};
    op.id = id;
    op.maxModifier = j.at("max_modifier").get<int>();
    op.timePerPoint = j.at("time_per_point").get<int>();
    return op;
}

PureEffectConfig parsePureEffect(const std::string& id, const json& j) {
    PureEffectConfig eff{};
    eff.id = id;
    eff.anchor.domain = j.at("domain").get<std::string>();
    eff.anchor.anchor = parseDimensionTriple(j.at("anchor"));
    eff.anchor.captureRadius = j.at("capture_radius").get<float>();
    return eff;
}

AnchorConfig parseCombinedAnchorEntry(const std::string& domain, const json& j) {
    AnchorConfig a{};
    a.domain = domain;
    a.anchor = parseDimensionTriple(j.at("coords"));
    a.captureRadius = j.at("capture_radius").get<float>();
    return a;
}

CombinedEffectConfig parseCombinedEffect(const std::string& id, const json& j) {
    CombinedEffectConfig eff{};
    eff.id = id;
    eff.beta = j.at("beta").get<float>();

    const json& anchorsJson = j.at("anchors");
    if (!anchorsJson.is_object()) {
        throw ConfigParseError("combined effect '" + id + "' anchors must be an object");
    }
    eff.anchors.reserve(anchorsJson.size());
    for (auto it = anchorsJson.begin(); it != anchorsJson.end(); ++it) {
        eff.anchors.push_back(parseCombinedAnchorEntry(it.key(), it.value()));
    }
    return eff;
}

TierConfig parseTier(const std::string& id, const json& j) {
    if (!j.is_array()) {
        throw ConfigParseError("tier '" + id + "' must be an array of dimension triples");
    }
    TierConfig tier{};
    tier.id = id;
    tier.dimensions.reserve(j.size());
    for (const json& d : j) {
        tier.dimensions.push_back(parseDimensionTriple(d));
    }
    return tier;
}

SourceObjectConfig parseSourceObject(const std::string& id, const json& j) {
    SourceObjectConfig src{};
    src.id = id;

    const json& tiersJson = j.at("tiers");
    if (!tiersJson.is_object()) {
        throw ConfigParseError("source object '" + id + "' tiers must be an object");
    }
    for (auto it = tiersJson.begin(); it != tiersJson.end(); ++it) {
        src.tiers.emplace(it.key(), parseTier(it.key(), it.value()));
    }
    return src;
}

AlterObjectConfig parseAlterObject(const std::string& id, const json& j) {
    AlterObjectConfig alt{};
    alt.id = id;

    const json& exps = j.at("exponents");
    if (!exps.is_array()) {
        throw ConfigParseError("alter object '" + id + "' exponents must be an array");
    }
    alt.exponents.reserve(exps.size());
    for (const json& e : exps) {
        alt.exponents.push_back(e.get<int>());
    }
    return alt;
}

EngineConfig parseEngineConfigJson(const json& root) {
    EngineConfig cfg{};

    cfg.modulus = root.at("engine").at("modulus").get<int>();

    const json& domainsJson = root.at("domains");
    if (!domainsJson.is_object()) {
        throw ConfigParseError("'domains' must be an object");
    }
    cfg.domains.reserve(domainsJson.size());
    for (auto it = domainsJson.begin(); it != domainsJson.end(); ++it) {
        cfg.domains.push_back(DomainConfig{it.key()});
    }

    const json& opsJson = root.at("operations");
    if (!opsJson.is_object()) {
        throw ConfigParseError("'operations' must be an object");
    }
    for (auto it = opsJson.begin(); it != opsJson.end(); ++it) {
        cfg.operations.emplace(it.key(), parseOperation(it.key(), it.value()));
    }

    const json& effectsJson = root.at("effects");
    const json& pureJson = effectsJson.at("pure");
    cfg.pureEffects.reserve(pureJson.size());
    for (auto it = pureJson.begin(); it != pureJson.end(); ++it) {
        cfg.pureEffects.push_back(parsePureEffect(it.key(), it.value()));
    }

    const json& combinedJson = effectsJson.at("combined");
    cfg.combinedEffects.reserve(combinedJson.size());
    for (auto it = combinedJson.begin(); it != combinedJson.end(); ++it) {
        cfg.combinedEffects.push_back(parseCombinedEffect(it.key(), it.value()));
    }

    const json& sourcesJson = root.at("source_objects");
    for (auto it = sourcesJson.begin(); it != sourcesJson.end(); ++it) {
        cfg.sourceObjects.emplace(it.key(), parseSourceObject(it.key(), it.value()));
    }

    const json& altersJson = root.at("alter_objects");
    for (auto it = altersJson.begin(); it != altersJson.end(); ++it) {
        cfg.alterObjects.emplace(it.key(), parseAlterObject(it.key(), it.value()));
    }

    return cfg;
}

}

EngineConfig loadEngineConfigFromString(const std::string& jsonText) {
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        throw ConfigParseError(std::string{"invalid JSON: "} + e.what());
    }

    try {
        return parseEngineConfigJson(root);
    } catch (const json::out_of_range& e) {
        throw ConfigParseError(std::string{"missing required field: "} + e.what());
    } catch (const json::type_error& e) {
        throw ConfigParseError(std::string{"type mismatch: "} + e.what());
    }
}

EngineConfig loadEngineConfigFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigParseError("could not open config file: " + path);
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return loadEngineConfigFromString(buf.str());
}

}
