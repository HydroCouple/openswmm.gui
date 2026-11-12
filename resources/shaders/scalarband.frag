#version 440

// Same classification as OpenSWMM::Contour::forEachTriangleIsoband: values below the first
// level or at/below the dry cutoff are not drawn; the band is the number of interior levels
// at or below the value; the last band saturates. Colours are premultiplied.

layout(location = 0) in float value;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float lowerLimit;
    float cutoff;
    float interiorCount;
    vec4  levels[256];  // interior breaks in .x
    vec4  colors[256];
} ubuf;

void main()
{
    if (value < ubuf.lowerLimit || value <= ubuf.cutoff)
        discard;
    int band = 0;
    int count = int(ubuf.interiorCount);
    for (int i = 0; i < 255; ++i) {
        if (i >= count)
            break;
        if (ubuf.levels[i].x <= value)
            band = i + 1;
    }
    fragColor = ubuf.colors[band] * ubuf.qt_Opacity;
}
