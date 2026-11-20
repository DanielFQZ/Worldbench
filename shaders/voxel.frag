#version 440
layout(location=0) in vec3 worldPos;
layout(location=1) in vec3 faceNormal;
layout(location=2) in vec4 baseColor;
layout(location=3) in vec2 textureUV;
layout(binding=1) uniform sampler2D atlas;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;} u;
void main(){
    vec4 sampled=(textureUV.x<0.0 ? vec4(1.0) : texture(atlas,textureUV))*baseColor;
    if(sampled.a<0.05)discard;
    float light=0.52+0.48*max(0.0,dot(faceNormal,normalize(vec3(-0.45,0.85,0.3))));
    vec3 col=sampled.rgb*light;
    vec2 uv=abs(faceNormal.y)>0.5?worldPos.xz:(abs(faceNormal.x)>0.5?worldPos.zy:worldPos.xy);
    vec2 cell=abs(fract(uv-0.5)-0.5)/max(fwidth(uv),vec2(0.0001));
    float edge=1.0-smoothstep(0.0,0.65,min(cell.x,cell.y));
    col*=1.0-edge*0.17*(1.0-smoothstep(100.0,300.0,distance(worldPos,u.camera.xyz)));
    if(u.sunColor.w>0.5){
        vec3 ambient=mix(u.groundColor.rgb,u.skyColor.rgb,faceNormal.y*.5+.5)*u.skyColor.w;
        vec3 direct=u.sunColor.rgb*max(0.0,dot(faceNormal,u.sunDirection.xyz))*u.sunDirection.w;
        col=sampled.rgb*(ambient+direct)*u.groundColor.w;
        if(u.envFlags.z>0.5){
            float fog=1.0-exp(-distance(worldPos,u.camera.xyz)*u.horizonColor.w/16000.0);
            vec3 ray=normalize(worldPos-u.camera.xyz);
            float glow=pow(max(0.0,dot(ray,u.sunDirection.xyz)),16.0)*u.sunDirection.w*.22;
            col=mix(col,(u.horizonColor.rgb+u.sunColor.rgb*glow)*u.groundColor.w,fog);
        }
        col=col/(vec3(1.0)+col*.35);
    }
    if(u.params.x>0.5){
        vec3 delta=abs(worldPos-u.brush.xyz);
        float d=u.params.y<1.5?(u.params.z>0.5?max(delta.x,delta.z):length(delta.xz)):(u.params.z>0.5?max(delta.x,max(delta.y,delta.z)):length(delta));
        float band=1.0-smoothstep(0.06,0.20,abs(d-u.brush.w*u.params.w));
        float outer=1.0-smoothstep(0.06,0.20,abs(d-u.brush.w));
        col=mix(col,vec3(0.62,0.87,0.91),outer*0.45);
        col=mix(col,vec3(0.30,0.88,0.98),band*0.95);
        if(d<u.brush.w)col=mix(col,vec3(0.25,0.75,0.85),0.10);
    }
    if(u.flags.x>0.5 && worldPos.x>=u.selection.x-.1 && worldPos.x<=u.selection.z+.1 && worldPos.z>=u.selection.y-.1 && worldPos.z<=u.selection.w+.1){
        float edge=min(min(abs(worldPos.x-u.selection.x),abs(worldPos.x-u.selection.z)),min(abs(worldPos.z-u.selection.y),abs(worldPos.z-u.selection.w)));
        col=mix(col,vec3(1.0,0.68,0.2),(1.0-smoothstep(.05,.2,edge))*.85);
    }
    fragColor=vec4(col,sampled.a);
}
