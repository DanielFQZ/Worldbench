#version 440
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=0) out vec3 worldPos;
layout(location=1) out vec3 faceNormal;
layout(location=2) out vec4 baseColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;} u;
void main(){worldPos=position;faceNormal=normal;baseColor=color;gl_Position=u.mvp*vec4(position,1.0);}
