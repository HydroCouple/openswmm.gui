#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec4 pathTime;
layout(location=3) in vec4 color;
layout(location=0) out vec4 vColor;
layout(location=1) out vec4 vTime;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; float width; float headWidth; };
void main() {
    float head=mod(phase+pathTime.w*(pathTime.y+pathTime.z),pathTime.y+pathTime.z);
    float age=head-pathTime.x;
    float a=clamp(1.0-age/max(pathTime.z,0.000001),0.15,1.0);
    // Match FlowStreakStyle::widthAtFraction; equal widths preserve the old taper.
    float w=width*mix(1.0,a,taper)+(headWidth-width)*smoothstep(0.75,1.0,a);
    gl_Position=matrix*vec4(position+normalOffset*w,0.0,1.0);
    vColor=color;vTime=pathTime;
}
