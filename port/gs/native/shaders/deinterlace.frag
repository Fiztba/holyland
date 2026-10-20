#version 450
// The scanout of a frame-mode picture read a field at a time, with --deinterlace (port/gs/native/gsn.cpp,
// "The scanout"), drawn over the whole picture with msaa.vert's triangle. u_field0 is this vblank's
// field (the buffer's lines of one parity, one under the other, sy rows each), u_field1..3 the fields
// of the three vblanks before (the other parity, this one, the other).
// The lines of this vblank's field are taken as they are. Each line of the other field is the line the
// vblank before showed (the two fields woven) where the picture holds still, and the mean of this
// field's lines above and below it (the field alone, interpolated) where it moves; between the two a
// ramp. The motion-adaptive weave of FastMAD (the deinterlacer paraLLEl-GS uses, with its thresholds,
// so that the two renderers' pictures agree): the picture moves where this field's lines above and
// below both changed since this field was last shown, or the line itself since its field was.
layout(set = 0, binding = 0) uniform sampler2D u_field0;
layout(set = 0, binding = 1) uniform sampler2D u_field1;
layout(set = 0, binding = 2) uniform sampler2D u_field2;
layout(set = 0, binding = 3) uniform sampler2D u_field3;
layout(push_constant) uniform Push {
    int parity; // the lines of this vblank's field: line & 1 == parity
    int sy;     // rows of pixels a line
    int lines;  // the picture's lines
} push;
layout(location = 0) out vec4 o_color;

// a field's n-th line (the picture's line 2n + its parity)
vec3 field_line(sampler2D field, int x, int n, int sub) {
    return texelFetch(field, ivec2(x, n * push.sy + sub), 0).rgb;
}

float brightness(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    int x = int(gl_FragCoord.x), y = int(gl_FragCoord.y);
    int line = y / push.sy, sub = y - line * push.sy;
    if ((line & 1) == push.parity) {
        o_color = vec4(field_line(u_field0, x, line >> 1, sub), 1.0);
        return;
    }
    vec3 woven = field_line(u_field1, x, line >> 1, sub);
    if (line == 0 || line == push.lines - 1) {
        o_color = vec4(woven, 1.0); // (a field's line above or below is missing)
        return;
    }
    int above_n = (line - 1) >> 1; // this field's lines above and below: its n-th and n + 1-th
    vec3 above = field_line(u_field0, x, above_n, sub), below = field_line(u_field0, x, above_n + 1, sub);
    float above_moved = abs(brightness(above - field_line(u_field2, x, above_n, sub)));
    float below_moved = abs(brightness(below - field_line(u_field2, x, above_n + 1, sub)));
    float line_moved = abs(brightness(woven - field_line(u_field3, x, line >> 1, sub)));
    float moved = max(min(above_moved, below_moved), line_moved);
    o_color = vec4(mix(woven, (above + below) * 0.5, smoothstep(0.04, 0.06, moved)), 1.0);
}
