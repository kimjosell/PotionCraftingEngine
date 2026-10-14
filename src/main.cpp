#include "effect_engine/engine.h"
#include "effect_engine/config_loader.h"

#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace effect_engine;

// Returns true iff fn() throws EngineError (and nothing else).
static bool throwsEngineError(const std::function<void()>& fn) {
    try { fn(); }
    catch (const EngineError&) { return true; }
    catch (...) { return false; }
    return false;
}



int main(int argc, char* argv[]) {
    const std::string configPath = (argc > 1)
        ? std::string(argv[1])
        : "project_context/tea_alchemist_config-v1.0.0.json";

    Engine engine;
    try {
        engine.load(configPath);
    } catch (const ConfigParseError& e) {
        std::cerr << "Failed to load engine: " << e.what() << "\n";
        return 1;
    }

    const SourceObject chamomile = engine.getSourceObjectInfo("chamomile", "standard");
    const SourceObject ginger    = engine.getSourceObjectInfo("ginger", "standard");
    const int modulus = 7; // matches tea_alchemist_config-v1.0.0.json


    // 1. a -  zeroes out every dimension.
    {
        MysteriousObject r = engine.subtract(chamomile, chamomile);
        assert(r.id == "mysteriousObject");
        for (const auto& d : r.dimensions)
            assert(d[0] == 0 && d[1] == 0 && d[2] == 0);
        assert(r.recipe && r.recipe->operationId == "subtract");
        std::cout << "[ok] subtract self, time 0 -> all zeros\n";
    }

    // 2. THE negative worry: base < tosubtract must never yield a negative value.
    //    Every dimension must stay in [0, modulus).
    {
        MysteriousObject r = engine.subtract(chamomile, ginger);
        for (const auto& d : r.dimensions)
            for (int v : d)
                assert(v >= 0 && v < modulus);
        std::cout << "[ok] subtract underflow stays in [0, modulus) -> never negative\n";
    }

    // 3. Domain count mismatch throws.
    {
        SourceObject bad;
        bad.id = "bad";
        bad.dimensions = {DimensionTriple{0, 0, 0}}; // 1 domain, engine has 2
        assert(throwsEngineError([&] { engine.subtract(bad, chamomile); }));
        std::cout << "[ok] subtract rejects wrong domain count\n";
    }

    // ---------------------------------------------------------------------
    // mix(vector<shared_ptr<Ingredient>>, operationId, elapsedTime) — add/multiply
    // ---------------------------------------------------------------------

    // 5. add, two ingredients, time 0: (a+b) % 7.
    //    domain0 [6,6,4], domain1 [6,1,6].
    {
        MysteriousObject r = engine.mix({chamomile.clone(), ginger.clone()}, "add");
        assert((r.dimensions[0] == DimensionTriple{6, 6, 4}));
        assert((r.dimensions[1] == DimensionTriple{6, 1, 6}));
        std::cout << "[ok] mix add (2 ingredients) matches (a+b) % modulus\n";
    }

    // 6. multiply, two ingredients, (a*b) % 7. Seed starts at 1.
    //    domain0 [1,1,3], domain1 [5,1,1].
    {
        MysteriousObject r = engine.mix({chamomile.clone(), ginger.clone()}, "multiply");
        assert((r.dimensions[0] == DimensionTriple{1, 1, 3}));
        assert((r.dimensions[1] == DimensionTriple{5, 1, 1}));
        std::cout << "[ok] mix multiply (2 ingredients) matches (a*b) % modulus\n";
    }

    // 7. add identity: a single ingredient returns itself % modulus (seed 0).
    {
        MysteriousObject r = engine.mix({chamomile.clone()}, "add");
        assert((r.dimensions[0] == DimensionTriple{4, 2, 3}));
        assert((r.dimensions[1] == DimensionTriple{1, 5, 2}));
        std::cout << "[ok] mix add identity (single ingredient)\n";
    }

    // 8. multiply identity: a single ingredient returns itself % modulus (seed 1).
    {
        MysteriousObject r = engine.mix({chamomile.clone()}, "multiply");
        assert((r.dimensions[0] == DimensionTriple{4, 2, 3}));
        assert((r.dimensions[1] == DimensionTriple{1, 5, 2}));
        std::cout << "[ok] mix multiply identity (single ingredient)\n";
    }

    // 9. empty ingredient list throws.
    {
        assert(throwsEngineError([&] { engine.mix({}, "add"); }));
        std::cout << "[ok] mix rejects empty ingredient list\n";
    }

    // 10. null ingredient in the list throws (guards the !ing check).
    {
        assert(throwsEngineError([&] {
            engine.mix({std::shared_ptr<Ingredient>{nullptr}}, "add");
        }));
        std::cout << "[ok] mix rejects null ingredient\n";
    }

    // 11. unsupported operation throws.
    {
        assert(throwsEngineError([&] {
            engine.mix({chamomile.clone()}, "divide");
        }));
        std::cout << "[ok] mix rejects unknown operation\n";
    }

    // ---------------------------------------------------------------------
    // alter / craft — unchanged behavior, kept as regression checks
    // ---------------------------------------------------------------------

    // 12. alter: fire_focus exponents [1,5] on chamomile [[4,2,3],[1,5,2]] (mod 7).
    {
        MysteriousObject altered = engine.alter(chamomile, "fire_focus");
        assert((altered.dimensions[0] == DimensionTriple{4, 2, 3}));
        assert((altered.dimensions[1] == DimensionTriple{1, 3, 4}));
        assert(altered.recipe && altered.recipe->operationId == "exponentiation");
        std::cout << "[ok] alter fire_focus\n";
    }

    // 13. craft: chamomile lands on calming_brew (leaf domain only).
    {
        MysteriousObject obj{};
        obj.id = "mysteriousObject";
        obj.dimensions = chamomile.dimensions;
        CraftedObject crafted = engine.craft(obj);
        assert(crafted.effect.effectId == "calming_brew");
        assert(crafted.effect.activeDomains.count("leaf") == 1);
        std::cout << "[ok] craft chamomile -> calming_brew\n";
    }

    // 14. craft: origin is outside every capture radius -> null effect.
    {
        MysteriousObject dead{};
        dead.id = "mysteriousObject";
        dead.dimensions = {DimensionTriple{0, 0, 0}, DimensionTriple{0, 0, 0}};
        CraftedObject crafted = engine.craft(dead);
        assert(crafted.effect.effectId.empty());
        assert(crafted.effect.activeDomains.empty());
        assert(effectDistance(crafted.effect) == 1.0f); // null effect is maximally weak
        std::cout << "[ok] craft dead zone -> null effect\n";
    }

    // 15. effectDistance averages the per-domain values of a combined effect (§3.5).
    //     Distance 1 from both anchors: harmony_tea beats the tied pure score via its
    //     beta, and each domain normalizes to 1/2.5 = 0.4, so the mean is 0.4 too.
    {
        MysteriousObject obj{};
        obj.id = "mysteriousObject";
        obj.dimensions = {DimensionTriple{5, 2, 3}, DimensionTriple{6, 3, 4}};
        CraftedObject crafted = engine.craft(obj);
        assert(crafted.effect.effectId == "harmony_tea");
        assert(crafted.effect.activeDomains.size() == 2);
        assert(std::fabs(effectDistance(crafted.effect) - 0.4f) < 1e-6f);
        std::cout << "[ok] effectDistance harmony_tea -> 0.4\n";
    }

    std::cout << "\nAll tests passed.\n";
    return 0;
}
