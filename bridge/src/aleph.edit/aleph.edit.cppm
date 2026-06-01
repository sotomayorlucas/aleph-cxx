// aleph.edit — the structural editor's headless core (Phase 6, SPEC §3.2).
//
// Umbrella for the EditorController. `aleph.edit` is the sanctioned
// cross-cutter (like aleph.lowering): it may import every backend
// (render.sw / render.rt / scene) plus lowering / graph / dpo. There is no
// iso_edit isolation test for this reason.
export module aleph.edit;
export import :controller;
