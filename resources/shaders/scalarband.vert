#version 440

// Depth contours on the fast vertex surface: pass each vertex's depth to the fragment
// stage, which draws bands or isolines of the linearly interpolated value (scalarband.frag).

layout(location = 0) in vec4 pos;
layout(location = 1) in float scalar;

layout(location = 0) out float value;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float lowerLimit;
    float cutoff;
    float levelCount;
    float mode;         // 0 bands, 1 isolines
    vec4  levels[256];  // .x level (bands: interior breaks), .y isoline half width in pixels
    vec4  colors[256];  // premultiplied; isolines use colors[0]
} ubuf;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    value       = scalar;
    gl_Position = ubuf.qt_Matrix * pos;
}
