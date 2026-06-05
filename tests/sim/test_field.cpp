#include "doctest.h"
#include <vector>
import aleph.sim;
import aleph.types;

using aleph::sim::ScalarField;
using aleph::types::NodeId;

TEST_CASE("ScalarField::zeros sizes buffers and kick targets the right node") {
    std::vector<NodeId> order{NodeId{10}, NodeId{20}, NodeId{30}};
    ScalarField f = ScalarField::zeros(order);
    REQUIRE(f.size() == 3);
    CHECK(f.phi.size() == 3);
    CHECK(f.phi_dot.size() == 3);
    CHECK(f.phi_dot[1] == doctest::Approx(0.0));

    CHECK(f.kick(NodeId{20}, 2.5) == true);
    CHECK(f.phi_dot[1] == doctest::Approx(2.5));
    CHECK(f.phi_dot[0] == doctest::Approx(0.0));
    CHECK(f.kick(NodeId{999}, 1.0) == false);   // not in order -> no-op
}

TEST_CASE("ScalarField::reproject carries survivors, zeros new, drops deleted") {
    ScalarField f = ScalarField::zeros(std::vector<NodeId>{NodeId{1}, NodeId{2}});
    f.phi[0] = 5.0; f.phi_dot[0] = -1.0;   // node 1
    f.phi[1] = 7.0; f.phi_dot[1] =  2.0;   // node 2 (will be deleted)

    f.reproject(std::vector<NodeId>{NodeId{1}, NodeId{3}});
    REQUIRE(f.size() == 2);
    CHECK(f.order[0] == NodeId{1});
    CHECK(f.order[1] == NodeId{3});
    CHECK(f.phi[0]     == doctest::Approx(5.0));   // survivor carried
    CHECK(f.phi_dot[0] == doctest::Approx(-1.0));
    CHECK(f.phi[1]     == doctest::Approx(0.0));   // new node zeroed
    CHECK(f.phi_dot[1] == doctest::Approx(0.0));
}
