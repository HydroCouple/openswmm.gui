#version 440
layout(location=0) in vec4 vColor;
layout(location=1) in vec4 vTime;
layout(location=2) in vec2 vEdge;
layout(location=3) in vec4 vMotion;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; float width; float headWidth; float displaySeconds; float minimumLifetime; float maximumLifetime; float speed; float lifetimeFade; };
void main() {
    float head=vMotion.x;
    float age=head-vTime.x;
    float pixelWidth=max(fwidth(vEdge.x),0.000001);
    // Integrate both sides of the strip over a pixel: even subpixel tails
    // retain their brightness as they move between pixel centres.
    float coverage=clamp((vEdge.y-vEdge.x)/pixelWidth+0.5,0.0,1.0)
        -clamp((-vEdge.y-vEdge.x)/pixelWidth+0.5,0.0,1.0);
    if(age<0.0 || age>=vTime.z) discard;
    // Match flowStreakOpacity / flowStreakAppearance in the painter.
    float birthDeath=lifetimeFade>0.0?smoothstep(0.0,lifetimeFade,vMotion.y)*smoothstep(0.0,lifetimeFade,vMotion.z):1.0;
    float soft=(1.0-age/max(vTime.z,0.000001))
        *smoothstep(0.0,max(0.000001,vTime.z*0.1),age)
        *birthDeath;
    float a=vColor.a*opacity*coverage*mix(1.0,soft*vMotion.w,fade);
    fragColor=vec4(vColor.rgb*a,a);
}
