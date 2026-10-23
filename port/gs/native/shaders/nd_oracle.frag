#version 450
// The vertex oracle's (gsn_nd_oracle.inc): nd_000_oracle.vert writes out what it makes; nothing is drawn
// of it that anyone reads
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(0.0);
}
