# Plan — Shared skeleton adjacency for bounded κ_R

Spec: `docs/superpowers/specs/2026-07-03-shared-adjacency-bounded-curvature-design.md`

1. **Test first** — add `tests/flow/test_shared_adjacency.cpp` (primitive≡builder,
   wrapper≡overload, both bitwise) + register in tests/CMakeLists.txt. It fails to
   compile (no `SkeletonAdjacency` yet).
2. **ollivier_ricci.cppm** — add exported `SkeletonAdjacency` + `build_adjacency`;
   split `build_local_state` into (shared-adj overload) + (4-arg wrapper); add the
   exported `ricci_curvature_edge_bounded` shared-adj overload; keep detail 4-arg.
3. **laplacian.cppm** — `build_laplacian_bounded` / `build_laplacian_local` build
   the adjacency once and use the overload.
4. **Gate** — full ctest (Tier-1 mv_localization byte-exact suite is the oracle).
5. **Measure** — rerun `aleph_bench_scaling --reps 3 --max-grid 16` and compare
   t_full/t_local against the pre-fix smoke (17.4→21.0 ms drift at dirty=37 must
   flatten); then the full paper sweep.
