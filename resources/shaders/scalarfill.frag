#version 440

// QSG-2D-1M Phase 8 — scalar-fill fragment stage: sample the 256×1 ramp
// LUT at the interpolated ramp position. LUT texels are premultiplied
// (baked from ARGB32_Premultiplied), matching QSG's blending expectations.

layout(location = 0) in float rampPos;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float vMin;
    float vInvRange;
} ubuf;

layout(binding = 1) uniform sampler2D rampTex;

void main()
{
    fragColor = texture(rampTex, vec2(rampPos, 0.5)) * ubuf.qt_Opacity;
}
