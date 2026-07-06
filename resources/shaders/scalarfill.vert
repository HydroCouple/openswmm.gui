#version 440

// QSG-2D-1M Phase 8 — scalar-fill vertex stage. Normalizes the per-vertex
// scalar with the same clamp contract as ScalarRampLut::normalize and
// hands the ramp position to the fragment stage.

layout(location = 0) in vec4 pos;
layout(location = 1) in float scalar;

layout(location = 0) out float rampPos;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float vMin;
    float vInvRange;
} ubuf;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    rampPos     = clamp((scalar - ubuf.vMin) * ubuf.vInvRange, 0.0, 1.0);
    gl_Position = ubuf.qt_Matrix * pos;
}
