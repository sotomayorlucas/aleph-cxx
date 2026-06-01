#include "doctest.h"

import aleph.lowering;  // build_sw_scene, SwBuild, LoweredScene

// Phase 6, SPEC §5 test 1 — build_sw_scene face counts + face_source map.
// STUB: the empty LoweredScene lowers to an empty SwBuild (no faces, no
// sources). Real face-count / face_source / determinism assertions land in W1.
TEST_CASE("build_sw: empty lowered scene -> empty SwBuild") {
    aleph::lowering::LoweredScene lowered;
    aleph::lowering::SwBuild sw = aleph::lowering::build_sw_scene(lowered);
    CHECK(sw.face_source.empty());
}
