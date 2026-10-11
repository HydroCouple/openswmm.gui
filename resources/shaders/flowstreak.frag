#version 440
layout(location=0) in vec4 vColor;
layout(location=1) in vec4 vTime;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; };
void main() {
    float head=mod(phase+vTime.w*(vTime.y+vTime.z),vTime.y+vTime.z);
    float age=head-vTime.x;
    if(age<0.0 || age>=vTime.z) discard;
    float a=vColor.a*opacity*mix(1.0,1.0-age/max(vTime.z,0.000001),fade);
    fragColor=vec4(vColor.rgb*a,a);
}
