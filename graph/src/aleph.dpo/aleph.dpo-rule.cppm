module;
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module aleph.dpo:rule;

import aleph.types;
import :pattern;

export namespace aleph::dpo {

// ── AttrInit ────────────────────────────────────────────────────────
// Carries the data needed to materialise a *new* host Node for a
// CreateNode action. `kind` is the NodeKind the rule promises to create
// (used to keep RHS edges type-correct); `build` constructs the concrete
// Node given the freshly-allocated host NodeId. The builder must produce
// a Node whose kind_of() == kind.
struct AttrInit {
    aleph::types::NodeKind                              kind{};
    std::function<aleph::types::Node(aleph::types::NodeId)> build{};
};

// ── AttrSet ─────────────────────────────────────────────────────────
// A ModifyAttr mutation. `expect_kind` is asserted against the matched
// host node before applying; a mismatch is reported as
// ApplyError::AttrSetMismatch. `mutate` edits the node in place (kind is
// never changed — only attributes).
struct AttrSet {
    aleph::types::NodeKind                       expect_kind{};
    std::function<void(aleph::types::Node&)>     mutate{};
    std::string                                  description{};
};

// ── Rule actions ────────────────────────────────────────────────────
// Each action is expressed in terms of pattern-local ids. The matcher
// resolves L ids to host ids; apply resolves R-only ids as it creates
// nodes/edges, threading the resulting host ids into the RewriteRecord.

struct CreateNode { PatternNodeId local{}; AttrInit init{}; };  // R \ K node
struct CreateEdge { PatternEdgeId edge{};  };                   // references rhs.edges
struct DeleteNode { PatternNodeId local{}; };                   // L \ K node (cascades)
struct DeleteEdge { PatternEdgeId edge{};  };                   // references lhs.edges
struct ModifyAttr { PatternNodeId node{};  AttrSet set{}; };    // K node mutation

using RuleAction = std::variant<CreateNode, CreateEdge,
                                 DeleteNode, DeleteEdge, ModifyAttr>;

// ── Rule ────────────────────────────────────────────────────────────
// L <- K -> R. `interface` is K (the preserved pattern node ids, present
// in both lhs and rhs). `side_effects` is the ordered edit script the
// matcher's embedding is replayed through.
struct Rule {
    std::string_view              name{};        // mirrors Rule_<name> in dpo_rules.tla
    Pattern                       lhs{};
    std::vector<PatternNodeId>    interface{};   // K = preserved ids
    Pattern                       rhs{};
    std::vector<RuleAction>       side_effects{};
};

}  // namespace aleph::dpo
