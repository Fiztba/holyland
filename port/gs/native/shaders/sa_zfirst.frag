#version 450
// A tie's Z-first draw in the scene where no pixel of it can fail the alpha test (gsn_standalone.inc, "The Z-first
// draws by depth alone"): no colour (its write mask is 0), no discard, the depth the vertex shader's, so the GPU tests
// and writes it before shading (gs.frag's discard, there though never taken, made each such draw test late, and the
// colour draw after it over the same pixels wait for it).
layout(early_fragment_tests) in;

void main() {
}
