#include "effect_engine/engine.h"
#include "effect_engine/config_loader.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace effect_engine;

int main(int argc, char* argv[]) {
    const std::string configPath = (argc > 1)
        ? std::string(argv[1])
        : "project_context/tea_alchemist_config-v1.0.0.json";

    Engine engine;
    try {
        engine.load(configPath);
        std::cout << "Engine loaded successfully from: " << configPath << "\n";
    } catch (const ConfigParseError& e) {
        std::cerr << "Failed to load engine: " << e.what() << "\n";
        return 1;
    }

    try {
        SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");
        std::cout << "getSourceObjectInfo(\"chamomile\", \"standard\"):\n";
        std::cout << "  id:   " << chamomile.id   << "\n";
        std::cout << "  tier: " << chamomile.tier  << "\n";
        for (std::size_t k = 0; k < chamomile.dimensions.size(); ++k) {
            const auto& d = chamomile.dimensions[k];
            std::cout << "  domain[" << k << "]: ["
                      << d[0] << ", " << d[1] << ", " << d[2] << "]\n";
        }
    } catch (const EngineError& e) {
        std::cerr << "Engine error: " << e.what() << "\n";
        return 1;
    }

    try {
        SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");

        // Self-check: subtracting an ingredient from itself with 0 elapsed time
        // must zero out every dimension (a-a=0, no time modifier). Config-independent.
        MysteriousObject zero = engine.mix(chamomile, chamomile, "subtract", 0);
        assert(zero.id == "mysteriousObject");
        for (const auto& d : zero.dimensions)
            assert(d[0] == 0 && d[1] == 0 && d[2] == 0);
        assert(zero.recipe && zero.recipe->operationId == "subtract");

        MysteriousObject mixed = engine.mix(chamomile, chamomile, "add", 30);
        std::cout << "mix(chamomile, chamomile, \"add\", 30):\n";
        for (std::size_t k = 0; k < mixed.dimensions.size(); ++k) {
            const auto& d = mixed.dimensions[k];
            std::cout << "  domain[" << k << "]: ["
                      << d[0] << ", " << d[1] << ", " << d[2] << "]\n";
        }
    } catch (const EngineError& e) {
        std::cerr << "mix error: " << e.what() << "\n";
        return 1;
    }

    try {
        SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");

        // Self-check: fire_focus exponents [1, 5] on chamomile [[4,2,3],[1,5,2]] (modulus 7).
        // domain 0 exponent 1 = identity → unchanged; domain 1 exponent 5 → [1^5,5^5,2^5]%7 = [1,3,4].
        MysteriousObject altered = engine.alter(chamomile, "fire_focus");
        assert(altered.id == "mysteriousObject");
        assert((altered.dimensions[0] == DimensionTriple{4, 2, 3}));
        assert((altered.dimensions[1] == DimensionTriple{1, 3, 4}));
        assert(altered.recipe && altered.recipe->operationId == "exponentiation");

        std::cout << "alter(chamomile, \"fire_focus\"):\n";
        for (std::size_t k = 0; k < altered.dimensions.size(); ++k) {
            const auto& d = altered.dimensions[k];
            std::cout << "  domain[" << k << "]: ["
                      << d[0] << ", " << d[1] << ", " << d[2] << "]\n";
        }
    } catch (const EngineError& e) {
        std::cerr << "alter error: " << e.what() << "\n";
        return 1;
    }

    try {
        // Self-check: craft chamomile standard [[4,2,3],[1,5,2]] (modulus 7).
        // leaf coord [4,2,3] is 1.41 from calming_brew anchor [5,2,4] (radius 2.0) → candidate.
        // root coord [1,5,2] is 6.16 from warming_brew (radius 2.0) → out of range, so
        // harmony_tea (needs both domains in range) is not a candidate. Pure wins: calming_brew.
        SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");
        MysteriousObject onlyChamomile{};
        onlyChamomile.id = "mysteriousObject";
        onlyChamomile.dimensions = chamomile.dimensions;
        CraftedObject crafted = engine.craft(onlyChamomile);
        assert(crafted.effect.effectId == "calming_brew");
        assert(crafted.effect.activeDomains.size() == 1);
        assert(crafted.effect.activeDomains.count("leaf") == 1);
        std::cout << "craft(chamomile) -> effect: " << crafted.effect.effectId << "\n";
        for (const auto& [domainId, dist] : crafted.effect.activeDomains) {
            std::cout << "  active domain " << domainId << ": distance " << dist << "\n";
        }

        // Self-check: an object at the origin is far from every anchor (leaf and root
        // anchors sit near [5,2,4]/[6,3,5] with radius 2.0–2.5), so no effect is a
        // candidate → null effect: empty id, empty active domains.
        MysteriousObject deadZone{};
        deadZone.id = "mysteriousObject";
        deadZone.dimensions = {DimensionTriple{0, 0, 0}, DimensionTriple{0, 0, 0}};
        CraftedObject nullCraft = engine.craft(deadZone);
        assert(nullCraft.effect.effectId.empty());
        assert(nullCraft.effect.activeDomains.empty());
        std::cout << "craft(dead zone) -> null effect (no dominant effect)\n";
    } catch (const EngineError& e) {
        std::cerr << "craft error: " << e.what() << "\n";
        return 1;
    }

    try {
        // Experiment: mix two ingredients, then craft the mysterious object it produces.
        SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");
        SourceObject ginger    = engine.getSourceObjectInfo("ginger", "standard");

        MysteriousObject brew = engine.mix(chamomile, ginger, "add", 0);
        std::cout << "mix(chamomile, ginger, \"add\", 0):\n";
        for (std::size_t k = 0; k < brew.dimensions.size(); ++k) {
            const auto& d = brew.dimensions[k];
            std::cout << "  domain[" << k << "]: ["
                      << d[0] << ", " << d[1] << ", " << d[2] << "]\n";
        }

        CraftedObject crafted = engine.craft(brew);
        if (crafted.effect.effectId.empty()) {
            std::cout << "craft(brew) -> null effect (landed in a dead zone)\n";
        } else {
            std::cout << "craft(brew) -> effect: " << crafted.effect.effectId << "\n";
            for (const auto& [domainId, dist] : crafted.effect.activeDomains) {
                std::cout << "  active domain " << domainId << ": distance " << dist << "\n";
            }
        }
    } catch (const EngineError& e) {
        std::cerr << "mix+craft experiment error: " << e.what() << "\n";
        return 1;
    }

    try {
        AlterObject unknown = engine.getAlterObjectInfo("fire_focus");
        std::cout<< "Objecto encontrado con exito";
    } catch (const EngineError& e) {
        std::cout << "getAlterObjectInfo(\"fire_focus\"): " << e.what() << "\n";
    }

    return 0;
}
