#include "doctest.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

import aleph.types;
import aleph.graph;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.is_open(), "failed to open " << path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr const char* TLA_SCENE_GRAPH =
#ifdef ALEPH_TLA_SCENE_GRAPH_PATH
    ALEPH_TLA_SCENE_GRAPH_PATH;
#else
    "formal/scene_graph.tla";
#endif

// Extract a set literal of the form `NAME == { "a", "b", ... }` from the .tla.
// Handles multi-line content inside the braces.
std::vector<std::string> extract_string_set(const std::string& source,
                                              std::string_view name) {
    const std::regex re(std::string(name) + R"(\s*==\s*\{([^}]+)\})");
    std::smatch m;
    if (!std::regex_search(source, m, re)) return {};
    const std::string body = m[1];
    std::vector<std::string> out;
    const std::regex str_re(R"(\"([^\"]+)\")");
    auto it = std::sregex_iterator(body.begin(), body.end(), str_re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) out.push_back((*it)[1].str());
    return out;
}

// Extract EdgeTypeCompat == [ kind |-> { <<"a","b">>, ... }, ... ]
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>
extract_edge_type_compat(const std::string& source) {
    const std::regex header(R"(EdgeTypeCompat\s*==\s*\[)");
    std::smatch m;
    if (!std::regex_search(source, m, header)) return {};
    const std::size_t start = static_cast<std::size_t>(m.position(0)) + m.length(0);
    // Walk from `start` until the matching `]`, accounting for nested `[`.
    int depth = 1;
    std::size_t end = start;
    while (end < source.size() && depth > 0) {
        if (source[end] == '[') ++depth;
        else if (source[end] == ']') --depth;
        if (depth == 0) break;
        ++end;
    }
    if (end >= source.size()) return {};
    const std::string body = source.substr(start, end - start);

    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> out;
    const std::regex entry(R"((\w+)\s*\|->\s*\{([^}]+)\})");
    auto it = std::sregex_iterator(body.begin(), body.end(), entry);
    auto end_it = std::sregex_iterator();
    for (; it != end_it; ++it) {
        const std::string kind = (*it)[1].str();
        const std::string pairs_body = (*it)[2].str();
        const std::regex pair_re(R"(<<\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*>>)");
        auto pit = std::sregex_iterator(pairs_body.begin(), pairs_body.end(), pair_re);
        for (; pit != end_it; ++pit) {
            out[kind].push_back({(*pit)[1].str(), (*pit)[2].str()});
        }
    }
    return out;
}

// Extract the list of invariant names from the `Invariants == /\ Name1 /\ Name2 ...`
// block in the TLA+ spec.  The TLA+ spec uses `EdgeTypeCompatInv` for the operator
// name (to avoid shadowing the `EdgeTypeCompat` function), but INVARIANT_NAMES in
// C++ stores the canonical short name `"EdgeTypeCompat"`.  We apply that mapping.
std::vector<std::string> extract_invariant_names(const std::string& source) {
    // Find the `Invariants ==` keyword and collect everything until the next blank line
    // or a line that doesn't start with `/\`.
    const std::regex header(R"(^Invariants\s*==)", std::regex::multiline);
    std::smatch m;
    if (!std::regex_search(source, m, header)) return {};
    const std::size_t start = static_cast<std::size_t>(m.position(0)) + m.length(0);
    const std::string rest = source.substr(start);

    // Match every `/\ Word` token in the block.
    const std::regex item_re(R"(/\\\s+(\w+))");
    auto it = std::sregex_iterator(rest.begin(), rest.end(), item_re);
    auto end_it = std::sregex_iterator();

    std::vector<std::string> out;
    for (; it != end_it; ++it) {
        std::string name = (*it)[1].str();
        // The TLA+ operator is EdgeTypeCompatInv; the canonical C++ name is EdgeTypeCompat.
        if (name == "EdgeTypeCompatInv") name = "EdgeTypeCompat";
        out.push_back(std::move(name));
    }
    return out;
}

}  // namespace

TEST_CASE("tla_cxx_sync: NodeKind enum matches scene_graph.tla NodeKind set") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_kinds = extract_string_set(src, "NodeKind");
    REQUIRE(tla_kinds.size() == 7);

    std::vector<std::string> cxx_kinds;
    for (auto k : aleph::types::all_node_kinds()) {
        cxx_kinds.push_back(std::string(aleph::types::as_tla(k)));
    }
    std::sort(tla_kinds.begin(), tla_kinds.end());
    std::sort(cxx_kinds.begin(), cxx_kinds.end());
    CHECK(tla_kinds == cxx_kinds);
}

TEST_CASE("tla_cxx_sync: EdgeKind enum matches scene_graph.tla EdgeKind set") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_kinds = extract_string_set(src, "EdgeKind");
    REQUIRE(tla_kinds.size() == 4);

    std::vector<std::string> cxx_kinds;
    for (auto k : aleph::types::all_edge_kinds()) {
        cxx_kinds.push_back(std::string(aleph::types::as_tla(k)));
    }
    std::sort(tla_kinds.begin(), tla_kinds.end());
    std::sort(cxx_kinds.begin(), cxx_kinds.end());
    CHECK(tla_kinds == cxx_kinds);
}

TEST_CASE("tla_cxx_sync: EdgeTypeCompat matches aleph::types::allows") {
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_table = extract_edge_type_compat(src);
    REQUIRE(tla_table.size() == 4);

    auto build_cxx_pairs = [](aleph::types::EdgeKind k) {
        std::vector<std::pair<std::string, std::string>> v;
        for (auto a : aleph::types::all_node_kinds()) {
            for (auto b : aleph::types::all_node_kinds()) {
                if (aleph::types::allows(k, a, b)) {
                    v.push_back({std::string(aleph::types::as_tla(a)),
                                  std::string(aleph::types::as_tla(b))});
                }
            }
        }
        std::sort(v.begin(), v.end());
        return v;
    };

    auto end_it = std::sregex_iterator();
    (void)end_it;  // suppress unused-var warning

    for (auto k : aleph::types::all_edge_kinds()) {
        auto cxx_pairs = build_cxx_pairs(k);
        auto& tla_pairs = tla_table[std::string(aleph::types::as_tla(k))];
        std::sort(tla_pairs.begin(), tla_pairs.end());
        CHECK_MESSAGE(cxx_pairs == tla_pairs,
            "Edge kind " << aleph::types::as_tla(k) << " compat drift");
    }
}

TEST_CASE("tla_cxx_sync: INVARIANT_NAMES matches Invariants== block in scene_graph.tla") {
    // Parse the `Invariants == /\ Name1 /\ Name2 ...` block from the TLA+ spec.
    // EdgeTypeCompatInv in TLA+ is mapped to "EdgeTypeCompat" (the canonical C++ name).
    const std::string src = read_file(TLA_SCENE_GRAPH);
    auto tla_names = extract_invariant_names(src);
    REQUIRE(tla_names.size() == aleph::graph::INVARIANT_NAMES.size());
    for (std::size_t i = 0; i < tla_names.size(); ++i) {
        CHECK(tla_names[i] == std::string(aleph::graph::INVARIANT_NAMES[i]));
    }
}
