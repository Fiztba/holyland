#version 450
// The scanout of a frame-mode picture read a field at a time, with --deinterlace (port/gs/native/gsn.cpp,
// "The scanout"), drawn over the whole picture with msaa.vert's triangle. u_now is the displayed buffer
// at this vblank, u_before1..3 the same at the three vblanks before (whole buffers, lines * sy rows).
// The lines of this vblank's field are taken as they are. Each line of the other field is the line the
// vblank before showed (the two fields woven) where the picture holds still, and the mean of this
// field's lines above and below it (the field alone, its lines doubled) where it moves; between the two
// a ramp. The motion-adaptive weave of FastMAD (the deinterlacer paraLLEl-GS uses, with its thresholds,
// so that the two renderers' pictures agree): the picture moves where this field's lines above and
// below both changed since this field was last shown, or the line itself since its field was.
layout(set = 0, binding = 0) uniform sampler2D u_now;
layout(set = 0, binding = 1) uniform sampler2D u_before1;
layout(set = 0, binding = 2) uniform sampler2D u_before2;
layout(set = 0, binding = 3) uniform sampler2D u_before3;
layout(push_constant) uniform Push {
    int parity; // the lines of this vblank's field: line & 1 == parity
    int sy;     // rows of pixels a line
    int lines;  // the picture's lines
} push;
layout(location = 0) out vec4 o_color;

vec3 row(sampler2D image, int x, int line, int sub) {
    return texelFetch(image, ivec2(x, line * push.sy + sub), 0).rgb;
}

float brightness(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    int x = int(gl_FragCoord.x), y = int(gl_FragCoord.y);
    int line = y / push.sy, sub = y - line * push.sy;
    if ((line & 1) == push.parity) {
        o_color = vec4(row(u_now, x, line, sub), 1.0);
        return;
    }
    vec3 woven = row(u_before1, x, line, sub);
    if (line == 0 || line == push.lines - 1) {
        o_color = vec4(woven, 1.0); // (a field's line above or below is missing)
        return;
    }
    vec3 above = row(u_now, x, line - 1, sub), below = row(u_now, x, line + 1, sub);
    float above_moved = abs(brightness(above - row(u_before2, x, line - 1, sub)));
    float below_moved = abs(brightness(below - row(u_before2, x, line + 1, sub)));
    float line_moved = abs(brightness(woven - row(u_before3, x, line, sub)));
    float moved = max(min(above_moved, below_moved), line_moved);
    o_color = vec4(mix(woven, (above + below) * 0.5, smoothstep(0.04, 0.06, moved)), 1.0);
}
