#include "doctest.h"

#include <utility>  // std::move (Graph is move-only)

import aleph.edit;   // EditorController
import aleph.graph;  // Graph

// Phase 6, SPEC §5 tests 2-4 — EditorController pick/apply/rebuild.
// STUB: construct a controller over a default graph; it compiles and lives.
// Real pick/apply/relower-consistency assertions land in W2.
TEST_CASE("edit: EditorController constructs over a graph") {
    aleph::graph::Graph g;
    aleph::edit::EditorController controller{std::move(g)};
    (void)controller;  // stub has no observable surface yet (W2)
    CHECK(true);
}
