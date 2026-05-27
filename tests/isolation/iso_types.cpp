import aleph.types;

int main() {
    using namespace aleph::types;
    Material m{NodeId{42}};
    return kind_of(m) == NodeKind::Material ? 0 : 1;
}
