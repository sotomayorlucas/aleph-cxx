#include "doctest.h"

import aleph.lowering;

// Phase 5.x-c — incremental lowering scaffold (wave 0).
//
// `lower_incremental` is wired with its final signature + `IncrementalStats`
// contract but currently full-re-lowers as a correct fallback. This stub test
// just pins that the module imports and links; later waves add the real oracle
// (incremental == full, byte-identical) and work-bound checks (SPEC §6).
TEST_CASE("incremental stub") {
    CHECK(true);
}
