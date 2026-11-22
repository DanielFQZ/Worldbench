#version 440
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 texcoord;
layout(location=3) out vec2 textureUV;
layout(location=0) out vec3 worldPos;
layout(location=1) out vec3 faceNormal;
layout(location=2) out vec4 baseColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;vec4 skyEffects;} u;
void main(){textureUV=texcoord;worldPos=position;faceNormal=normal;baseColor=color;gl_Position=u.mvp*vec4(position,1.0);}
