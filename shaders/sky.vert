#version 440
layout(location=0) out vec3 direction;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;vec4 skyEffects;float skyTextureEnabled;} u;
void main(){
    vec2 p=gl_VertexIndex==0?vec2(-1,-1):gl_VertexIndex==1?vec2(3,-1):vec2(-1,3);
    gl_Position=vec4(p,1,1);
    vec4 world=u.inverseView*vec4(p.x,p.y*u.envFlags.w,1,1);
    direction=world.xyz/world.w-u.camera.xyz;
}
