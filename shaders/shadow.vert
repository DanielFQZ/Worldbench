#version 440
layout(location=0) in vec3 position;
layout(location=2) in vec4 color;
layout(location=3) in vec2 texcoord;
layout(location=0) out vec2 uv;
layout(location=1) out float alpha;
layout(std140,binding=0) uniform Shadow {mat4 mvp;} u;
void main(){uv=texcoord;alpha=color.a;gl_Position=u.mvp*vec4(position,1.0);}
