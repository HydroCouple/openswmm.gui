#version 440
layout(location=0) in vec4 vColor;
layout(location=1) in vec4 vTime;
layout(location=2) in vec2 vEdge;
layout(location=3) in float vAppearedAt;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; float width; float headWidth; float displaySeconds; };
void main() {
    float head=mod(phase+vTime.w*(vTime.y+vTime.z),vTime.y+vTime.z);
    float age=head-vTime.x;
    float pixelWidth=max(fwidth(vEdge.x),0.000001);
    // Integrate both sides of the strip over a pixel: even subpixel tails
    // retain their brightness as they move between pixel centres.
    float coverage=clamp((vEdge.y-vEdge.x)/pixelWidth+0.5,0.0,1.0)
        -clamp((-vEdge.y-vEdge.x)/pixelWidth+0.5,0.0,1.0);
    if(age<0.0 || age>=vTime.z) discard;
    // Match flowStreakOpacity / flowStreakAppearance in the painter.
    float ramp=max(0.000001,min(vTime.y*0.15,vTime.z*0.25));
    float soft=(1.0-age/max(vTime.z,0.000001))
        *smoothstep(0.0,max(0.000001,vTime.z*0.1),age)
        *smoothstep(0.0,ramp,head)*smoothstep(0.0,ramp,vTime.y+vTime.z-head);
    float appearance=vAppearedAt<0.0?1.0:smoothstep(0.0,0.25,displaySeconds-vAppearedAt);
    float a=vColor.a*opacity*coverage*mix(1.0,soft*appearance,fade);
    fragColor=vec4(vColor.rgb*a,a);
}
