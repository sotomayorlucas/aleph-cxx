// aleph.editor:perf — pure frame-timing helpers for the live editor HUD
// (2026-06-09 perf-HUD spec). Window-free: the shell measures phase durations
// with Window::perf_counter deltas and feeds them here; this partition only
// aggregates (rolling means) and decides (frame dirty-flag), so it is fully
// doctest-able without SDL at runtime.
module;
#include <cstddef>

export module aleph.editor:perf;

export namespace aleph::editor {

// Rolling-window capacity: ~0.5 s of samples at 60 FPS — enough to smooth
// frame-to-frame jitter without hiding a real slowdown.
inline constexpr int kRollingMeanCapacity = 30;

// A fixed-capacity ring buffer of the last kRollingMeanCapacity samples (ms).
// No allocation; mean() averages only the samples present (0.0f when empty).
struct RollingMean {
    float samples[kRollingMeanCapacity]{};
    int   count{0};   // samples present (saturates at capacity)
    int   next{0};    // ring write index

    void push(float ms) noexcept {
        samples[next] = ms;
        next = (next + 1) % kRollingMeanCapacity;
        if (count < kRollingMeanCapacity) ++count;
    }

    float mean() const noexcept {
        if (count == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += samples[i];
        return sum / static_cast<float>(count);
    }
};

// The shell-observable reasons the next frame must actually be re-rendered.
// When ALL signals are false the previously presented frame is still exact,
// so the shell may re-present the cached back surface and sleep (frame
// pacing) instead of re-running the render pipeline.
struct FrameSignals {
    bool had_input{false};            // any window event arrived this frame
    bool op_applied{false};           // a graph Op ran (add/delete/kick/nudge)
    bool sim_stepping{false};         // wave/sim mode: step() runs every frame
    bool crossfade_active{false};     // raster<->PT fade ramping (alpha < 1)
    bool pt_accumulating{false};      // path trace still converging
    bool view_rebake_pending{false};  // throttled view-dependent rebake queued
    bool selection_changed{false};    // a pick/select happened this frame
    bool first_frame{false};          // nothing has been presented yet
};

// True if ANY signal demands a re-render.
constexpr bool frame_dirty(FrameSignals s) noexcept {
    return s.had_input || s.op_applied || s.sim_stepping || s.crossfade_active
        || s.pt_accumulating || s.view_rebake_pending || s.selection_changed
        || s.first_frame;
}

// One frame's per-phase durations (ms) as measured by the shell — the
// run_live phases in loop order. Phases that did not run this frame stay 0.
struct PhaseTimes {
    float events_ms{0.0f};       // poll_events + gestures->Ops + pick
    float step_ms{0.0f};         // wave/sim sub-step
    float raster_ms{0.0f};       // clear_sky + SSAA rasterize
    float outline_ms{0.0f};      // selection-silhouette pass + ring
    float downsample_ms{0.0f};   // SSAA box-downsample
    float ui_ms{0.0f};           // UI panel + HUD overlay draw
    float present_ms{0.0f};      // crossfade/tonemap loop + present
    float frame_ms{0.0f};        // whole loop iteration
};

}  // namespace aleph::editor
