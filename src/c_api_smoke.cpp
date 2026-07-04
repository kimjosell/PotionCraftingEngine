/* Smoke test exercising the C API end to end through the FFI surface only.
 * Run from the repo root (needs the config path); pass a path as argv[1] to override. */
#include "effect_engine/effect_engine_c.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    const char* configPath = (argc > 1)
        ? argv[1]
        : "project_context/tea_alchemist_config-v1.0.0.json";

    EffectEngine* engine = effect_engine_load(configPath);
    if (!engine) {
        std::cerr << "load failed: " << effect_engine_last_error() << "\n";
        return 1;
    }

    // Config queries.
    assert(effect_engine_domain_count(engine) == 2);
    assert(effect_engine_source_count(engine) == 2);
    std::cout << "domains:";
    for (int i = 0; i < effect_engine_domain_count(engine); ++i)
        std::cout << " " << effect_engine_domain_id(engine, i);
    std::cout << "\n";

    // Build a mysterious object equal to chamomile's coordinates using only the C API:
    //   zero  = chamomile - chamomile  (all zeros)
    //   brew  = chamomile + zero       (mysterious object carrying chamomile's dims)
    EEIngredient* chamomile = effect_engine_get_source(engine, "chamomile", "standard");
    assert(chamomile);
    EEIngredient* zero = effect_engine_mix(engine, chamomile, chamomile, "subtract", 0);
    EEIngredient* brew = effect_engine_mix(engine, chamomile, zero, "add", 0);
    assert(brew);

    int dims[6] = {0};
    int n = effect_engine_ingredient_dimensions(brew, dims, 6);
    assert(n == 6);
    // chamomile standard = [[4,2,3],[1,5,2]]
    const int expected[6] = {4, 2, 3, 1, 5, 2};
    for (int i = 0; i < 6; ++i) assert(dims[i] == expected[i]);

    // Craft it → calming_brew (leaf coord [4,2,3] is within calming_brew's radius).
    EECrafted* crafted = effect_engine_craft(engine, brew);
    assert(crafted);
    assert(std::strcmp(effect_engine_crafted_effect_id(crafted), "calming_brew") == 0);
    assert(effect_engine_crafted_active_domain_count(crafted) == 1);
    assert(std::strcmp(effect_engine_crafted_active_domain_id(crafted, 0), "leaf") == 0);
    std::cout << "craft(brew) -> " << effect_engine_crafted_effect_id(crafted)
              << " (leaf distance " << effect_engine_crafted_active_domain_distance(crafted, 0)
              << ")\n";

    // Null-effect path: crafting the all-zeros object lands in a dead zone.
    EECrafted* nullCraft = effect_engine_craft(engine, zero);
    assert(nullCraft);
    assert(effect_engine_crafted_effect_id(nullCraft)[0] == '\0');
    assert(effect_engine_crafted_active_domain_count(nullCraft) == 0);
    std::cout << "craft(zero) -> null effect\n";

    // Error path: an unknown operation returns NULL and sets last_error.
    EEIngredient* bad = effect_engine_mix(engine, chamomile, zero, "divide", 0);
    assert(bad == nullptr);
    assert(effect_engine_last_error()[0] != '\0');
    std::cout << "mix(divide) rejected: " << effect_engine_last_error() << "\n";

    effect_engine_crafted_free(nullCraft);
    effect_engine_crafted_free(crafted);
    effect_engine_ingredient_free(brew);
    effect_engine_ingredient_free(zero);
    effect_engine_ingredient_free(chamomile);
    effect_engine_destroy(engine);

    std::cout << "c_api_smoke: all checks passed\n";
    return 0;
}
