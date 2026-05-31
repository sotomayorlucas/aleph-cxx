module;
#include <optional>
#include <string>
#include <tuple>

export module aleph.dpo:rules;

import aleph.types;
import :pattern;
import :rule;

namespace aleph::dpo::detail {

// Build a NodeConstraint with no attribute predicate.
inline NodeConstraint kind_only(aleph::types::NodeKind k) {
    return NodeConstraint{k, std::nullopt};
}

}  // namespace aleph::dpo::detail

export namespace aleph::dpo::rules {

// ── Rule 1: spawn_light ─────────────────────────────────────────────
//   L = { m:mesh }            K = { m }
//   R = { m, light, influences(light -> m) }
// Adds a Point light and an Influences edge onto a matched mesh.
inline const Rule& spawn_light() {
    static const Rule r = [] {
        using namespace aleph::types;
        Rule rule;
        rule.name = "spawn_light";

        // LHS: one mesh (p0).
        rule.lhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.interface = { PatternNodeId{0} };  // mesh preserved

        // RHS: mesh (p0) + light (p1), edge influences(p1 -> p0).
        rule.rhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.rhs.nodes.insert(PatternNodeId{1}, detail::kind_only(NodeKind::Light));
        rule.rhs.edges.insert(
            PatternEdgeId{0},
            std::tuple{PatternNodeId{1}, PatternNodeId{0}, EdgeKind::Influences});

        // Side effects: create the light, then the influences edge.
        AttrInit light_init;
        light_init.kind  = NodeKind::Light;
        light_init.build = [](NodeId id) -> Node {
            return Light{id, LightKind::Point, std::string("dpo/spawned")};
        };
        rule.side_effects.push_back(CreateNode{PatternNodeId{1}, std::move(light_init)});
        rule.side_effects.push_back(CreateEdge{PatternEdgeId{0}});
        return rule;
    }();
    return r;
}

// ── Rule 2: remove_object ───────────────────────────────────────────
//   L = { m:mesh, incident edges }   K = {}   R = {}
// Removes a matched mesh and cascades every incident edge.
inline const Rule& remove_object() {
    static const Rule r = [] {
        using namespace aleph::types;
        Rule rule;
        rule.name = "remove_object";

        rule.lhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.interface = {};  // K empty: the mesh is deleted

        // RHS empty.
        rule.side_effects.push_back(DeleteNode{PatternNodeId{0}});
        return rule;
    }();
    return r;
}

// ── Rule 3: replace_material ────────────────────────────────────────
//   L = { m:mesh, old:material, new:material, references(m -> old) }
//   K = { m, new }
//   R = { m, new, references(m -> new) }
// Reroutes a mesh's single References edge from one material to another.
inline const Rule& replace_material() {
    static const Rule r = [] {
        using namespace aleph::types;
        Rule rule;
        rule.name = "replace_material";

        // LHS: mesh (p0), old material (p1), new material (p2),
        //      references(p0 -> p1).
        rule.lhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.lhs.nodes.insert(PatternNodeId{1}, detail::kind_only(NodeKind::Material));
        rule.lhs.nodes.insert(PatternNodeId{2}, detail::kind_only(NodeKind::Material));
        rule.lhs.edges.insert(
            PatternEdgeId{0},
            std::tuple{PatternNodeId{0}, PatternNodeId{1}, EdgeKind::References});
        rule.interface = { PatternNodeId{0}, PatternNodeId{2} };  // mesh + new mat

        // RHS: references(p0 -> p2).
        rule.rhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.rhs.nodes.insert(PatternNodeId{2}, detail::kind_only(NodeKind::Material));
        rule.rhs.edges.insert(
            PatternEdgeId{1},
            std::tuple{PatternNodeId{0}, PatternNodeId{2}, EdgeKind::References});

        // Side effects: drop the old reference, add the new one.
        rule.side_effects.push_back(DeleteEdge{PatternEdgeId{0}});
        rule.side_effects.push_back(CreateEdge{PatternEdgeId{1}});
        return rule;
    }();
    return r;
}

// ── Rule 4: refine_cell ─────────────────────────────────────────────
//   L = { M:mesh, mat:material, references(M -> mat) }   K = { M, mat }
//   R = { M, mat, M_a, M_b,
//         references(M_a -> mat), references(M_b -> mat),
//         adjacent(M_a -> M_b) }
// Monotone split: adds two child meshes sharing M's material, joined by an
// Adjacent edge. M and its reference survive (no deletions).
inline const Rule& refine_cell() {
    static const Rule r = [] {
        using namespace aleph::types;
        Rule rule;
        rule.name = "refine_cell";

        // LHS: mesh (p0) referencing material (p1).
        rule.lhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.lhs.nodes.insert(PatternNodeId{1}, detail::kind_only(NodeKind::Material));
        rule.lhs.edges.insert(
            PatternEdgeId{0},
            std::tuple{PatternNodeId{0}, PatternNodeId{1}, EdgeKind::References});
        rule.interface = { PatternNodeId{0}, PatternNodeId{1} };  // both preserved

        // RHS: original mesh+mat, plus M_a (p2), M_b (p3), and edges
        //      references(p2->p1), references(p3->p1), adjacent(p2->p3).
        rule.rhs.nodes.insert(PatternNodeId{0}, detail::kind_only(NodeKind::Mesh));
        rule.rhs.nodes.insert(PatternNodeId{1}, detail::kind_only(NodeKind::Material));
        rule.rhs.nodes.insert(PatternNodeId{2}, detail::kind_only(NodeKind::Mesh));
        rule.rhs.nodes.insert(PatternNodeId{3}, detail::kind_only(NodeKind::Mesh));
        rule.rhs.edges.insert(
            PatternEdgeId{1},
            std::tuple{PatternNodeId{2}, PatternNodeId{1}, EdgeKind::References});
        rule.rhs.edges.insert(
            PatternEdgeId{2},
            std::tuple{PatternNodeId{3}, PatternNodeId{1}, EdgeKind::References});
        rule.rhs.edges.insert(
            PatternEdgeId{3},
            std::tuple{PatternNodeId{2}, PatternNodeId{3}, EdgeKind::Adjacent});

        // Side effects: create M_a, M_b, then the 3 RHS-only edges.
        AttrInit mesh_a;
        mesh_a.kind  = NodeKind::Mesh;
        mesh_a.build = [](NodeId id) -> Node {
            return Mesh{id, std::string("dpo/refined_a"), 0};
        };
        AttrInit mesh_b;
        mesh_b.kind  = NodeKind::Mesh;
        mesh_b.build = [](NodeId id) -> Node {
            return Mesh{id, std::string("dpo/refined_b"), 0};
        };
        rule.side_effects.push_back(CreateNode{PatternNodeId{2}, std::move(mesh_a)});
        rule.side_effects.push_back(CreateNode{PatternNodeId{3}, std::move(mesh_b)});
        rule.side_effects.push_back(CreateEdge{PatternEdgeId{1}});
        rule.side_effects.push_back(CreateEdge{PatternEdgeId{2}});
        rule.side_effects.push_back(CreateEdge{PatternEdgeId{3}});
        return rule;
    }();
    return r;
}

}  // namespace aleph::dpo::rules
