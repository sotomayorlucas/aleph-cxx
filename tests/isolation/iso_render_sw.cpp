import aleph.render.sw;

int main() {
    aleph::render::sw::SceneRT sr;
    aleph::render::sw::add_floor(sr, {0,0,0}, 1.0f, aleph::render::sw::tex_floor);
    return sr.faces.size() == 1 ? 0 : 1;
}
