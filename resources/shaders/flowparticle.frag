#version 440
layout(location=0) in vec4 trail;
layout(location=1) in vec2 coverage;
layout(location=2) in vec4 vColor;
layout(location=0) out vec4 fragColor;
void main() {
    if(trail.x<0.0 || trail.x>=trail.z)discard;
    float pixel=max(fwidth(coverage.x),0.000001);
    float area=clamp((coverage.y-coverage.x)/pixel+0.5,0.0,1.0)-clamp((-coverage.y-coverage.x)/pixel+0.5,0.0,1.0);
    // A half-pixel halo keeps light trails readable on pale water and light
    // basemaps. Dark/classified colours retain their original coverage.
    float light=smoothstep(0.55,0.75,dot(vColor.rgb,vec3(0.2126,0.7152,0.0722)));
    float outer=clamp((coverage.y+0.5-coverage.x)/pixel+0.5,0.0,1.0)-clamp((-coverage.y-0.5-coverage.x)/pixel+0.5,0.0,1.0);
    float halo=max(0.0,outer-area)*light*0.65;
    float alpha=(area+halo)*trail.y*trail.w*vColor.a;
    fragColor=vec4((vColor.rgb*area+vec3(0.08,0.20,0.28)*halo)*trail.y*trail.w*vColor.a,alpha);
}
