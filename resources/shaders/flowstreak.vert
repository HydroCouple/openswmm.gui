#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec4 pathTime;
layout(location=3) in vec4 color;
layout(location=0) out vec4 vColor;
layout(location=1) out vec4 vTime;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; };
void main() {
    float head=mod(phase+pathTime.w*(pathTime.y+pathTime.z),pathTime.y+pathTime.z);
    float age=head-pathTime.x;
    float a=clamp(1.0-age/max(pathTime.z,0.000001),0.15,1.0);
    gl_Position=matrix*vec4(position+normalOffset*mix(1.0,a,taper),0.0,1.0);
    vColor=color;vTime=pathTime;
}
