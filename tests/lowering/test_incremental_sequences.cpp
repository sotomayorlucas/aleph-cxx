#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

import aleph.math;
import aleph.containers;
import aleph.types;
import aleph.graph;
import aleph.dpo;
import aleph.lowering;

#include "lowering_freeze.hpp"

using namespace aleph::types;
using aleph::graph::Graph;
using aleph::math::Mat4;
using aleph::math::Vec3;
using aleph_test_freeze::freeze;

namespace {

LocalTransform translate(float x, float y, float z) {
    return LocalTransform{Mat4::translate(Vec3{x, y, z})};
}

struct Lcg {
    std::uint64_t state;
    explicit Lcg(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((next() >> 33) % n);
    }
    float real(float range) {
        const auto q = static_cast<std::int32_t>(below(2001)) - 1000;
        return (static_cast<float>(q) / 1000.0f) * range;
    }
};

struct RandGraph {
    Graph g;
    NodeId root{}, mid{};
    NodeId a_mesh{};
    NodeId mid_xf{};
    std::vector<NodeId> meshes;
};

RandGraph make_random_graph(std::uint64_t seed) {
    Lcg rng(seed);
    RandGraph s;
    Graph& g = s.g;

    s.root = g.alloc_node_id();
    g.insert_node(Transform{s.root, 0, translate(0, 0, 0)});

    Camera cam{};
    cam.id        = g.alloc_node_id();
    cam.sensor_id = std::string("sensor");
    cam.look_from = Vec3{rng.real(5), rng.real(5), 8 + rng.real(2)};
    cam.look_at   = Vec3{0, 0, 0};
    cam.up        = Vec3{0, 1, 0};
    cam.vfov_deg  = 30.0f + rng.real(10);
    g.insert_node(std::move(cam));

    s.mid    = g.alloc_node_id();
    s.mid_xf = s.mid;
    g.insert_node(Transform{s.mid, 1, translate(rng.real(6), rng.real(6), rng.real(6))});

    (void)g.add_edge(EdgeKind::Contains, s.root, cam.id);
    (void)g.add_edge(EdgeKind::Contains, s.root, s.mid);

    const std::uint32_t n_mesh = 1 + rng.below(4);
    for (std::uint32_t i = 0; i < n_mesh; ++i) {
        const NodeId mesh_id = g.alloc_node_id();
        Mesh mesh{mesh_id, std::string("m"), 0};
        mesh.geometry = SphereLocal{Vec3{rng.real(4), rng.real(4), rng.real(4)},
                                    0.5f + (static_cast<float>(rng.below(20)) / 10.0f)};
        g.insert_node(std::move(mesh));

        const NodeId mat_id = g.alloc_node_id();
        const std::uint32_t mk = rng.below(3);
        Material mat{mat_id, mk == 0 ? MaterialKind::Lambertian
                                      : (mk == 1 ? MaterialKind::Metal
                                                 : MaterialKind::Dielectric)};
        mat.albedo = Vec3{static_cast<float>(rng.below(11)) / 10.0f,
                          static_cast<float>(rng.below(11)) / 10.0f,
                          static_cast<float>(rng.below(11)) / 10.0f};
        mat.fuzz = static_cast<float>(rng.below(10)) / 10.0f;
        mat.ior  = 1.0f + static_cast<float>(rng.below(10)) / 10.0f;
        mat.emit = Vec3{0, 0, 0};
        g.insert_node(std::move(mat));

        const NodeId parent = (rng.below(2) == 0) ? s.mid : s.root;
        (void)g.add_edge(EdgeKind::Contains, parent, mesh_id);
        (void)g.add_edge(EdgeKind::References, mesh_id, mat_id);

        s.meshes.push_back(mesh_id);
        if (s.a_mesh.value == 0) s.a_mesh = mesh_id;
    }

    const std::uint32_t n_light = rng.below(3);
    for (std::uint32_t i = 0; i < n_light; ++i) {
        const NodeId light_id = g.alloc_node_id();
        Light light{};
        light.id       = light_id;
        light.kind     = LightKind::Area;
        light.emit_ref = std::string("e");
        light.emission = Vec3{4, 4, 4};
        light.geometry = QuadLocal{Vec3{rng.real(3), 10, rng.real(3)},
                                   Vec3{2, 0, 0}, Vec3{0, 0, 2}};
        g.insert_node(std::move(light));
        (void)g.add_edge(EdgeKind::Contains, s.root, light_id);
    }

    return s;
}

aleph::lowering::Op make_random_op(Lcg& rng, RandGraph& s) {
    const std::uint32_t kind = rng.below(6);
    switch (kind) {
        case 0: {
            aleph::lowering::MaterialParams p{};
            p.kind   = MaterialKind::Metal;
            p.albedo = Vec3{0.3f, 0.6f, 0.9f};
            p.fuzz   = 0.2f;
            p.ior    = 1.4f;
            p.emit   = Vec3{0, 0, 0};
            return aleph::lowering::SetMaterial{s.a_mesh, p};
        }
        case 1:
            return aleph::lowering::SetTransform{s.mid_xf, translate(1.0f, -2.0f, 3.0f)};
        case 2: {
            aleph::lowering::AddObject add{};
            add.parent   = s.root;
            add.geometry = SphereLocal{Vec3{7, 7, 7}, 0.75f};
            add.material = aleph::lowering::MaterialParams{};
            return add;
        }
        case 3: {
            aleph::lowering::AddLight add{};
            add.parent   = s.root;
            add.kind     = LightKind::Point;
            add.emission = Vec3{3, 3, 3};
            add.geometry = SphereLocal{Vec3{0, 9, 0}, 0.2f};
            return add;
        }
        case 4:
            if (!s.meshes.empty()) {
                const NodeId target = s.meshes[rng.below(static_cast<std::uint32_t>(s.meshes.size()))];
                return aleph::lowering::DeleteObject{target};
            }
            [[fallthrough]];
        default:
            return aleph::lowering::ApplyRule{&aleph::dpo::rules::refine_cell()};
    }
}

void update_tracking_after_op(const aleph::lowering::Op& op,
                              const aleph::dpo::RewriteRecord& rec,
                              RandGraph& s) {
    if (const auto* del = std::get_if<aleph::lowering::DeleteObject>(&op)) {
        auto& v = s.meshes;
        v.erase(std::remove(v.begin(), v.end(), del->target), v.end());
        if (s.a_mesh == del->target) {
            s.a_mesh = v.empty() ? NodeId{} : v[0];
        }
    } else if (std::holds_alternative<aleph::lowering::AddObject>(op)) {
        for (const NodeId id : rec.created_nodes) {
            const Node* n = s.g.node(id);
            if (n != nullptr && kind_of(*n) == NodeKind::Mesh) {
                s.meshes.push_back(id);
                if (s.a_mesh.value == 0) s.a_mesh = id;
            }
        }
    }
}

}  // namespace

TEST_CASE("incremental: property — 512 seeds, chained 3-8 ops, prev feeds next") {
    for (std::uint32_t i = 0; i < 512; ++i) {
        const std::uint64_t seed = 0xB06B000000000001ULL ^ (0x9E3779B97F4A7C15ULL * (i + 1));

        RandGraph s = make_random_graph(seed);
        Lcg       rng(seed ^ 0xDEADBEEFCAFE0000ULL);

        auto prev_exp = aleph::lowering::lower(s.g);
        REQUIRE(prev_exp.has_value());
        aleph::lowering::LoweredScene prev = std::move(*prev_exp);

        const std::uint32_t n_ops = 3 + rng.below(6);
        for (std::uint32_t step = 0; step < n_ops; ++step) {
            const aleph::lowering::Op op = make_random_op(rng, s);

            auto applied = aleph::lowering::apply_op(s.g, op);
            if (!applied.has_value()) continue;

            update_tracking_after_op(op, *applied, s);

            auto full = aleph::lowering::lower(s.g);
            REQUIRE(full.has_value());

            auto inc = aleph::lowering::lower_incremental(prev, s.g, op, *applied);
            REQUIRE(inc.has_value());

            INFO("seed=" << seed << " step=" << step);
            CHECK(freeze(*inc) == freeze(*full));

            prev = std::move(*inc);
        }

        auto final_full = aleph::lowering::lower(s.g);
        REQUIRE(final_full.has_value());
        CHECK(freeze(prev) == freeze(*final_full));
    }
}
