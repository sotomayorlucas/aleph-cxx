import aleph.io;

int main() {
    auto r = aleph::io::MappedFile::open_read("/does/not/exist");
    return r.has_value() ? 1 : 0;
}
