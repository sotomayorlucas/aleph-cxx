import aleph.linalg.gf2;

int main() {
    // Touch one symbol from each partition so the module links in isolation.
    auto m = aleph::linalg::gf2::BitMatrix::identity(2);
    return static_cast<int>(m.rank()) == 2 ? 0 : 1;
}
