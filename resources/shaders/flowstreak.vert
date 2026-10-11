#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec4 pathTime;
layout(location=3) in vec4 color;
layout(location=4) in vec4 stroke;
layout(location=5) in vec4 life;
layout(location=0) out vec4 vColor;
layout(location=1) out vec4 vTime;
layout(location=2) out vec2 vEdge;
layout(location=3) out vec4 vMotion;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float phase; float fade; float taper; float width; float headWidth; float displaySeconds; float minimumLifetime; float maximumLifetime; float speed; float lifetimeFade; };
float lifetime(float generation,float minimum,float maximum) {
    // Match flowStreakLifetime. Paired random durations make elapsed-time
    // evaluation constant work, even after a long pause between redraws.
    float pair=floor(generation*0.5);
    float h=mod(mod(pathTime.w,65536.0)*109.0+mod(pair,65536.0)*73.0+floor(pathTime.w/65536.0)*239.0,65536.0);
    h=mod(h*251.0+mod(floor(pair/65536.0),256.0)*199.0,65536.0);
    float first=minimum+(maximum-minimum)*h/65536.0;
    return mod(generation,2.0)>0.0?minimum+maximum-first:first;
}
vec3 motion() {
    float delta=max(0.0,displaySeconds-stroke.z);
    if(delta<life.z) return vec3(life.x+phase-stroke.w,life.y+delta,life.z-delta);
    float maximum=max(0.000001,min(maximumLifetime,(pathTime.y+pathTime.z)/max(speed,0.000001)));
    float minimum=min(minimumLifetime,maximum);
    float elapsed=delta-life.z;
    float generation=life.w+1.0;
    if(mod(generation,2.0)>0.0) {
        float duration=lifetime(generation,minimum,maximum);
        if(elapsed<duration) return vec3(phase-stroke.w-(delta-elapsed)*speed,elapsed,duration-elapsed);
        elapsed-=duration;generation+=1.0;
    }
    float pair=minimum+maximum;
    generation+=floor(elapsed/pair)*2.0;elapsed=mod(elapsed,pair);
    float first=lifetime(generation,minimum,maximum),duration=first;
    if(elapsed>=first) {elapsed-=first;duration=pair-first;}
    return vec3(phase-stroke.w-(delta-elapsed)*speed,elapsed,duration-elapsed);
}
void main() {
    vec3 particle=motion();float head=particle.x;
    float age=head-pathTime.x;
    float a=clamp(1.0-age/max(pathTime.z,0.000001),0.15,1.0);
    // Match FlowStreakStyle::widthAtFraction; equal widths preserve the old taper.
    float w=width*mix(1.0,a,taper)+(headWidth-width)*smoothstep(0.75,1.0,a);
    // One logical pixel outside each edge provides room for analytic coverage.
    gl_Position=matrix*vec4(position+normalOffset*(w+2.0),0.0,1.0);
    vEdge=vec2(stroke.x*(w+2.0)*0.5,w*0.5);
    float appearance=stroke.y<0.0?1.0:smoothstep(0.0,0.25,displaySeconds-stroke.y);
    vMotion=vec4(particle,appearance);
    vColor=color;vTime=pathTime;
}
