#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec4 timing;
layout(location=3) in vec2 stroke;
layout(location=4) in vec3 color;
layout(location=0) out vec4 trail;
layout(location=1) out vec2 coverage;
layout(location=2) out vec3 vColor;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float interpolation; float clock; float tail; float width; float headWidth; float fade; };
void main() {
    float age=mix(timing.y,timing.z,interpolation);
    float fraction=clamp(1.0-(age-timing.x)/tail,0.0,1.0);
    float w=width*max(0.15,fraction)+(headWidth-width)*smoothstep(0.75,1.0,fraction);
    gl_Position=matrix*vec4(position+normalOffset*(w+2.0),0.0,1.0);
    coverage=vec2(stroke.x*(w+2.0)*0.5,w*0.5);
    float fadeTime=min(fade,timing.w*0.2);
    float life=fadeTime>0.0?smoothstep(0.0,fadeTime,age)*smoothstep(0.0,fadeTime,timing.w-age):1.0;
    float retired=stroke.y<0.0?1.0:(fade>0.0?1.0-smoothstep(0.0,fade,clock-stroke.y):0.0);
    trail=vec4(age-timing.x,life*retired,tail,opacity);
    vColor=color;
}
