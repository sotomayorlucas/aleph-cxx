import aleph.alloc;

int main() {
    alignas(16) static unsigned char buf[1024];
    aleph::alloc::Arena a{buf, sizeof(buf)};
    return a.allocate(16, 16) ? 0 : 1;
}
