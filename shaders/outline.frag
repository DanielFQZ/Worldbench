#version 440
layout(binding=1) uniform sampler2D mask;
layout(location=0) out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy), last = textureSize(mask,0)-1;
    if (texelFetch(mask,p,0).r > .5) discard;
    float edge = 0.;
    for (int y=-2; y<=2; ++y) for (int x=-2; x<=2; ++x) {
        if (x*x+y*y<=5) edge = max(edge,texelFetch(mask,clamp(p+ivec2(x,y),ivec2(0),last),0).r);
    }
    if (edge < .5) discard;
    color = vec4(1.,.64,.15,1.);
}
