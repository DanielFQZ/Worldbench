#version 440
layout(location=0) in vec3 position;
layout(location=2) in vec4 color;
layout(location=0) out vec4 lineColor;
layout(std140,binding=0) uniform Scene { mat4 mvp; } u;
void main() {
    lineColor = color;
    gl_Position = u.mvp * vec4(position, 1.0);
}
