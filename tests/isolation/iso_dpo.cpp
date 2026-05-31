import aleph.dpo;

// Touch one exported symbol so the module is linked, not just imported.
int main() {
    const aleph::dpo::Rule& r = aleph::dpo::rules::spawn_light();
    return r.name.empty() ? 1 : 0;
}
