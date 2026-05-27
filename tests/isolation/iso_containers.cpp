import aleph.containers;

int main() {
    aleph::containers::SmallVector<int, 4> v;
    v.push_back(7);
    return v[0] - 7;
}
