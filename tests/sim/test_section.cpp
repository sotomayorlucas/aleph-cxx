#include "doctest.h"
#include <vector>
import aleph.sim;
import aleph.types;
import aleph.math;
using aleph::sim::Section;
using aleph::types::NodeId;
using aleph::math::Vec3;

TEST_CASE("Section<f64>: zeros/add(in & out of range)/at") {
    Section<double> f = Section<double>::zeros(std::vector<NodeId>{NodeId{10},NodeId{20},NodeId{30}});
    REQUIRE(f.size()==3); CHECK(f.data[1]==doctest::Approx(0.0));
    CHECK(f.add(NodeId{20}, 2.5)); CHECK(f.data[1]==doctest::Approx(2.5));
    CHECK(f.add(NodeId{20}, 0.5)); CHECK(f.data[1]==doctest::Approx(3.0));   // accumulates
    CHECK(!f.add(NodeId{999}, 1.0));
    REQUIRE(f.at(NodeId{20})); CHECK(*f.at(NodeId{20})==doctest::Approx(3.0));
    CHECK(f.at(NodeId{999})==nullptr);
}
TEST_CASE("Section<f64>: reproject survivor/new/deleted") {
    Section<double> f = Section<double>::zeros(std::vector<NodeId>{NodeId{1},NodeId{2}});
    f.data[0]=5.0; f.data[1]=7.0;
    f.reproject(std::vector<NodeId>{NodeId{1},NodeId{3}});
    REQUIRE(f.size()==2); CHECK(f.order[0]==NodeId{1}); CHECK(f.order[1]==NodeId{3});
    CHECK(f.data[0]==doctest::Approx(5.0)); CHECK(f.data[1]==doctest::Approx(0.0));
}
TEST_CASE("Section<Vec3>: T-genericity (storage/add/reproject)") {
    Section<Vec3> g = Section<Vec3>::zeros(std::vector<NodeId>{NodeId{1},NodeId{2}});
    CHECK(g.add(NodeId{1}, Vec3{1,2,3})); CHECK(g.add(NodeId{1}, Vec3{1,2,3}));
    CHECK(g.data[0].x==doctest::Approx(2.0)); CHECK(g.data[0].y==doctest::Approx(4.0));
    CHECK(g.data[0].z==doctest::Approx(6.0));
    CHECK(g.data[1].x==doctest::Approx(0.0)); CHECK(g.data[1].y==doctest::Approx(0.0));  // node 2 stays zero
    g.reproject(std::vector<NodeId>{NodeId{1}});
    REQUIRE(g.size()==1);
    CHECK(g.data[0].x==doctest::Approx(2.0)); CHECK(g.data[0].y==doctest::Approx(4.0));
    CHECK(g.data[0].z==doctest::Approx(6.0));
}
