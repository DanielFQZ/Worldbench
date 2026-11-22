#version 440
layout(location=0) in vec2 uv;
layout(location=1) in float alpha;
layout(binding=1) uniform sampler2D atlas;
layout(location=0) out float depth;
void main(){if(alpha*(uv.x<0.0?1.0:texture(atlas,uv).a)<.05)discard;depth=gl_FragCoord.z;}
