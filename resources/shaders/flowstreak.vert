#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec4 pathTime;
layout(location=3) in vec4 color;
layout(location=4) in vec2 stroke;
layout(location=0) out vec4 vColor;
layout(location=1) out vec4 vTime;
layout(location=2) out vec2 vEdge;
layout(location=3) out float vAppearedAt;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; float width; float headWidth; float displaySeconds; };
void main() {
    float head=mod(phase+pathTime.w*(pathTime.y+pathTime.z),pathTime.y+pathTime.z);
    float age=head-pathTime.x;
    float a=clamp(1.0-age/max(pathTime.z,0.000001),0.15,1.0);
    // Match FlowStreakStyle::widthAtFraction; equal widths preserve the old taper.
    float w=width*mix(1.0,a,taper)+(headWidth-width)*smoothstep(0.75,1.0,a);
    // One logical pixel outside each edge provides room for analytic coverage.
    gl_Position=matrix*vec4(position+normalOffset*(w+2.0),0.0,1.0);
    vEdge=vec2(stroke.x*(w+2.0)*0.5,w*0.5);vAppearedAt=stroke.y;
    vColor=color;vTime=pathTime;
}
