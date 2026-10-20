#version 450
// A triangle over the whole of a render target, for the passes that copy between a target's image and
// its multisampled one (--native-msaa, port/gs/native/gsn.cpp): msaa_load.frag, msaa_load_depth.frag,
// msaa_depth.frag. Each pixel is drawn once, at its middle.
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
