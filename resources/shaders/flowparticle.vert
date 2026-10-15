#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 normalOffset;
layout(location=2) in vec2 sampleParticle;
layout(location=3) in vec4 color;
layout(location=0) out vec4 trail;
layout(location=1) out vec2 coverage;
layout(location=2) out vec4 vColor;
layout(std140,binding=0) uniform buf { mat4 matrix; float opacity; float interpolation; float clock; float tail; float width; float headWidth; float fade; float softFade; float taper; vec2 anchorOffset; float timingRows; };
layout(binding=1) uniform sampler2D particleTiming;
float value(int column,int particle) {
    // Arithmetic decoding also bakes for the existing GLSL 100 ES/120
    // targets, which have neither texelFetch nor integer bit operations.
    vec4 bytes=floor(texture(particleTiming,(vec2(column,particle)+0.5)/vec2(8.0,timingRows))*255.0+0.5);
    float signBit=bytes.w>=128.0?-1.0:1.0;
    float exponent=mod(bytes.w,128.0)*2.0+floor(bytes.z/128.0);
    float mantissa=bytes.x+bytes.y*256.0+mod(bytes.z,128.0)*65536.0;
    return exponent==0.0?signBit*mantissa*exp2(-149.0):signBit*exp2(exponent-127.0)*(1.0+mantissa/8388608.0);
}
void main() {
    // GLSL 1.20 only has floating-point max(); clamp before converting.
    int particle=int(max(0.0,abs(sampleParticle.y)-1.0));
    float age=mix(value(0,particle),value(1,particle),interpolation);
    float lifetime=value(2,particle),stoppedAt=value(3,particle),tailStart=value(4,particle);
    float visibleTail=tailStart<0.0?tail:min(tail,max(age-tailStart,0.000001));
    float fraction=clamp(1.0-(age-sampleParticle.x)/visibleTail,0.0,1.0);
    float w=width*mix(1.0,max(0.25,fraction),taper)+(headWidth-width)*smoothstep(0.75,1.0,fraction);
    gl_Position=matrix*vec4(position+anchorOffset+normalOffset*(w+2.0),0.0,1.0);
    coverage=vec2(sign(sampleParticle.y)*(w+2.0)*0.5,w*0.5);
    float fadeTime=min(fade,lifetime*0.2);
    float life=fadeTime>0.0?smoothstep(0.0,fadeTime,age)*smoothstep(0.0,fadeTime,lifetime-age):1.0;
    float retired=stoppedAt<0.0?1.0:(fade>0.0?1.0-smoothstep(0.0,fade,clock-stoppedAt):0.0);
    float visibility=mix(value(5,particle),value(6,particle),interpolation);
    trail=vec4(age-sampleParticle.x,life*retired*visibility,visibleTail,opacity*mix(1.0,fraction,softFade));
    vColor=color;
}
