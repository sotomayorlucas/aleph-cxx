// aleph.editor:perf — RollingMean oracles for the per-phase frame-timing HUD.
// The partition is pure (no SDL at runtime), but aleph_editor is SDL-gated in
// CMake, so this file is registered in the ALEPH_HAVE_SDL2 test block.
#include "doctest.h"
import aleph.editor;

using aleph::editor::kRollingMeanCapacity;
using aleph::editor::RollingMean;

TEST_CASE("RollingMean: empty -> mean is 0") {
    RollingMean rm{};
    CHECK(rm.mean() == 0.0f);
}

TEST_CASE("RollingMean: partial fill averages only the samples present") {
    RollingMean rm{};
    rm.push(2.0f);
    rm.push(4.0f);
    CHECK(rm.mean() == doctest::Approx(3.0f));
    rm.push(6.0f);
    CHECK(rm.mean() == doctest::Approx(4.0f));
}

TEST_CASE("RollingMean: exact mean over a full window") {
    RollingMean rm{};
    // 1..30 -> mean = 31/2 = 15.5 exactly.
    for (int i = 1; i <= kRollingMeanCapacity; ++i) {
        rm.push(static_cast<float>(i));
    }
    CHECK(rm.mean() == doctest::Approx(15.5f));
}

TEST_CASE("RollingMean: wraparound keeps only the newest capacity samples") {
    RollingMean rm{};
    // 40 pushes of 1..40: the ring retains 11..40 -> mean = (11+40)/2 = 25.5.
    for (int i = 1; i <= kRollingMeanCapacity + 10; ++i) {
        rm.push(static_cast<float>(i));
    }
    CHECK(rm.mean() == doctest::Approx(25.5f));

    // A burst of a single value flushes the window entirely.
    for (int i = 0; i < kRollingMeanCapacity; ++i) {
        rm.push(7.0f);
    }
    CHECK(rm.mean() == doctest::Approx(7.0f));
}
