#version 440
layout(location=0) in vec2 uv;
layout(location=1) in float alpha;
layout(binding=1) uniform sampler2D atlas;
layout(location=0) out vec4 mask;
void main() {
    if (alpha * (uv.x < 0. ? 1. : textureLod(atlas, uv, 0.).a) < .05) discard;
    mask = vec4(1.);
}
