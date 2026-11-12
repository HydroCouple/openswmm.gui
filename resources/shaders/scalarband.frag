#version 440

// Bands: the classification of OpenSWMM::Contour::forEachTriangleIsoband. Values below the
// first level or at/below the cutoff are not drawn; the band is the number of interior levels
// at or below the value; the last band saturates.
// Isolines: within a level's half width in screen pixels (from the value's screen-space
// gradient), antialiased over one pixel. Colours are premultiplied.

layout(location = 0) in float value;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float lowerLimit;
    float cutoff;
    float levelCount;
    float mode;
    vec4  levels[256];
    vec4  colors[256];
} ubuf;

void main()
{
    int count = int(ubuf.levelCount);
    if (ubuf.mode > 0.5) {
        float perPixel = max(length(vec2(dFdx(value), dFdy(value))), 1e-20);
        float coverage = 0.0;
        for (int i = 0; i < 256; ++i) {
            if (i >= count)
                break;
            float distancePx = abs(value - ubuf.levels[i].x) / perPixel;
            coverage = max(coverage, clamp(ubuf.levels[i].y + 0.5 - distancePx, 0.0, 1.0));
        }
        if (coverage <= 0.0)
            discard;
        fragColor = ubuf.colors[0] * (coverage * ubuf.qt_Opacity);
        return;
    }
    if (value < ubuf.lowerLimit || value <= ubuf.cutoff)
        discard;
    int band = 0;
    for (int i = 0; i < 255; ++i) {
        if (i >= count)
            break;
        if (ubuf.levels[i].x <= value)
            band = i + 1;
    }
    fragColor = ubuf.colors[band] * ubuf.qt_Opacity;
}
