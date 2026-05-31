#include "doctest.h"

import aleph.render.rt;

// Phase 5.x-b W3 stub. `RenderOpts` gains opt-in adaptive-spp knobs; the
// adaptive loop is filled later. For now assert the default contract: the
// feature is off by default (so the uniform path stays byte-identical) and the
// scale knob defaults to 4 (SPEC §4.3).
TEST_CASE("adaptive_spp: RenderOpts defaults are opt-out (off) with scale 4") {
    aleph::render::rt::RenderOpts opts;
    CHECK(opts.adaptive_spp == false);
    CHECK(opts.max_spp_scale == 4);
}
