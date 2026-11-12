#version 440

// Filled contour bands on the fast vertex surface: pass each vertex's depth to the
// fragment stage, which classifies the linearly interpolated value (scalarband.frag).

layout(location = 0) in vec4 pos;
layout(location = 1) in float scalar;

layout(location = 0) out float value;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float lowerLimit;
    float cutoff;
    float interiorCount;
    vec4  levels[256];
    vec4  colors[256];
} ubuf;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    value       = scalar;
    gl_Position = ubuf.qt_Matrix * pos;
}
