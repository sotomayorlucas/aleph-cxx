import aleph.linalg.sparse;

int main() {
    aleph::linalg::sparse::DMatrix a(2, 2, 0.0);
    a.at(0, 0) = 1.0;
    a.at(1, 1) = 1.0;
    return a.rows() == 2 && a.cols() == 2 ? 0 : 1;
}
