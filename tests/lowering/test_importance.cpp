#include "doctest.h"

import aleph.types;
import aleph.graph;
import aleph.containers;
import aleph.lowering;

// Phase 5.x-b W1 stub. `entity_importance` is currently empty-but-valid;
// later waves fill the Ricci aggregate. For now assert the stub contract:
// an empty graph yields no importance entries, deterministically.
TEST_CASE("importance: stub entity_importance is empty (uniform) and deterministic") {
    aleph::graph::Graph g;
    const auto a = aleph::lowering::entity_importance(g);
    const auto b = aleph::lowering::entity_importance(g);
    CHECK(a.size() == 0);
    CHECK(b.size() == 0);
    CHECK(a.size() == b.size());
}
