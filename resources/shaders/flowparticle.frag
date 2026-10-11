#version 440
layout(location=0) in vec4 trail;
layout(location=1) in vec2 coverage;
layout(location=2) in vec4 vColor;
layout(location=0) out vec4 fragColor;
void main() {
    if(trail.x<0.0 || trail.x>=trail.z)discard;
    float pixel=max(fwidth(coverage.x),0.000001);
    float area=clamp((coverage.y-coverage.x)/pixel+0.5,0.0,1.0)-clamp((-coverage.y-coverage.x)/pixel+0.5,0.0,1.0);
    float alpha=area*trail.y*trail.w*vColor.a;
    fragColor=vec4(vColor.rgb*alpha,alpha);
}
