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
    //   zero  = chamomile - chamomile  (all zeros, via the dedicated subtract function)
    //   brew  = chamomile + zero       (mysterious object carrying chamomile's dims)
    EEIngredient* chamomile = effect_engine_get_source(engine, "chamomile", "standard");
    assert(chamomile);
    EEIngredient* zero = effect_engine_subtract(engine, chamomile, chamomile, 0);
    assert(zero);
    const EEIngredient* addArgs[] = {chamomile, zero};
    EEIngredient* brew = effect_engine_mix(engine, addArgs, 2, "add", 0);
    assert(brew);

    int dims[6] = {0};
    int n = effect_engine_ingredient_dimensions(brew, dims, 6);
    assert(n == 6);
    // chamomile standard = [[4,2,3],[1,5,2]]
    const int expected[6] = {4, 2, 3, 1, 5, 2};
    for (int i = 0; i < 6; ++i) assert(dims[i] == expected[i]);

    // Recipe tree: brew = mix(chamomile, zero, "add"), zero = subtract(chamomile, chamomile).
    // So the root is a MIX with a SOURCE leaf and a nested subtract branch.
    const EERecipeNode* root = effect_engine_ingredient_root(brew);
    assert(root);
    assert(effect_engine_node_kind(root) == EE_NODE_MIX);
    assert(std::strcmp(effect_engine_node_operation_id(root), "add") == 0);
    assert(effect_engine_node_child_count(root) == 2);
    const EERecipeNode* left = effect_engine_node_child(root, 0);
    const EERecipeNode* right = effect_engine_node_child(root, 1);
    assert(effect_engine_node_kind(left) == EE_NODE_SOURCE);
    assert(std::strcmp(effect_engine_node_id(left), "chamomile") == 0);
    assert(std::strcmp(effect_engine_node_tier(left), "standard") == 0);
    assert(effect_engine_node_kind(right) == EE_NODE_SUBTRACT);    // recursion
    assert(std::strcmp(effect_engine_node_operation_id(right), "subtract") == 0);
    assert(effect_engine_node_child_count(right) == 2);
    assert(effect_engine_node_kind(effect_engine_node_child(right, 0)) == EE_NODE_SOURCE);
    // Leaf dimensions match chamomile standard [[4,2,3],[1,5,2]].
    int leafDims[6] = {0};
    assert(effect_engine_node_dimensions(left, leafDims, 6) == 6);
    for (int i = 0; i < 6; ++i) assert(leafDims[i] == expected[i]);
    std::cout << "recipe tree: mix(" << effect_engine_node_id(left) << ", subtract(...))\n";

    // Craft it → calming_brew (leaf coord [4,2,3] is within calming_brew's radius).
    EECrafted* crafted = effect_engine_craft(engine, brew);
    assert(crafted);
    assert(std::strcmp(effect_engine_crafted_effect_id(crafted), "calming_brew") == 0);
    assert(effect_engine_crafted_active_domain_count(crafted) == 1);
    assert(std::strcmp(effect_engine_crafted_active_domain_id(crafted, 0), "leaf") == 0);
    std::cout << "craft(brew) -> " << effect_engine_crafted_effect_id(crafted)
              << " (leaf distance " << effect_engine_crafted_active_domain_distance(crafted, 0)
              << ")\n";

    // Crafted keeps the tree too: its root is the mysterious object that was crafted.
    const EERecipeNode* craftedRoot = effect_engine_crafted_root(crafted);
    assert(craftedRoot);
    assert(effect_engine_node_kind(craftedRoot) == EE_NODE_MIX);
    assert(effect_engine_node_child_count(craftedRoot) == 2);

    // Null-effect path: crafting the all-zeros object lands in a dead zone.
    EECrafted* nullCraft = effect_engine_craft(engine, zero);
    assert(nullCraft);
    assert(effect_engine_crafted_effect_id(nullCraft)[0] == '\0');
    assert(effect_engine_crafted_active_domain_count(nullCraft) == 0);
    std::cout << "craft(zero) -> null effect\n";

    // Error path: an unknown operation returns NULL and sets last_error.
    const EEIngredient* badArgs[] = {chamomile, zero};
    EEIngredient* bad = effect_engine_mix(engine, badArgs, 2, "divide", 0);
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
