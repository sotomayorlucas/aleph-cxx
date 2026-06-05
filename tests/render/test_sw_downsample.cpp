#include "doctest.h"
#include <vector>
import aleph.render.sw;
import aleph.render.common;
import aleph.math;
using aleph::math::Vec3;
using aleph::render::common::Film;

TEST_CASE("downsample_box ss=2 averages each 2x2 block (linear)") {
    // 4x4 src, 2x2 dst. Block (0,0) = {0,2,4,6}->avg 3; fill blocks with known means.
    std::vector<Vec3> s(16), d(4);
    auto at=[&](int x,int y)->Vec3&{ return s[static_cast<std::size_t>(y*4+x)]; };
    // top-left 2x2 block all = 1; top-right all = 2; bottom-left = 3; bottom-right = 4
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ at(x,y)=Vec3{1,1,1}; at(x+2,y)=Vec3{2,2,2}; at(x,y+2)=Vec3{3,3,3}; at(x+2,y+2)=Vec3{4,4,4}; }
    Film src{s.data(),4,4,4}; Film dst{d.data(),2,2,2};
    aleph::render::sw::downsample_box(src, dst, 2);
    CHECK(d[0].x==doctest::Approx(1.0f)); CHECK(d[1].x==doctest::Approx(2.0f));
    CHECK(d[2].x==doctest::Approx(3.0f)); CHECK(d[3].x==doctest::Approx(4.0f));
}
TEST_CASE("downsample_box ss=2 averages a non-uniform block") {
    std::vector<Vec3> s(16,Vec3{0,0,0}), d(4);
    s[0]=Vec3{0,0,0}; s[1]=Vec3{2,0,0}; s[4]=Vec3{4,0,0}; s[5]=Vec3{6,0,0}; // block(0,0)
    Film src{s.data(),4,4,4}; Film dst{d.data(),2,2,2};
    aleph::render::sw::downsample_box(src, dst, 2);
    CHECK(d[0].x==doctest::Approx(3.0f));   // (0+2+4+6)/4
}
TEST_CASE("downsample_box ss=1 is identity copy") {
    std::vector<Vec3> s{Vec3{1,2,3},Vec3{4,5,6}}, d(2);
    Film src{s.data(),2,1,2}; Film dst{d.data(),2,1,2};
    aleph::render::sw::downsample_box(src, dst, 1);
    CHECK(d[0].x==doctest::Approx(1.0f)); CHECK(d[1].y==doctest::Approx(5.0f));
}
