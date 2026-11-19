#version 440
layout(location=0) in vec3 worldPos;
layout(location=1) in vec3 faceNormal;
layout(location=2) in vec4 baseColor;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;} u;
void main(){
    float light=0.52+0.48*max(0.0,dot(faceNormal,normalize(vec3(-0.45,0.85,0.3))));
    vec3 col=baseColor.rgb*light;
    vec2 uv=abs(faceNormal.y)>0.5?worldPos.xz:(abs(faceNormal.x)>0.5?worldPos.zy:worldPos.xy);
    vec2 cell=abs(fract(uv-0.5)-0.5)/max(fwidth(uv),vec2(0.0001));
    float edge=1.0-smoothstep(0.0,0.65,min(cell.x,cell.y));
    col*=1.0-edge*0.17;
    if(u.params.x>0.5){
        float d=u.params.y<1.5?length(worldPos.xz-u.brush.xz):length(worldPos-u.brush.xyz);
        float band=1.0-smoothstep(0.06,0.20,abs(d-u.brush.w));
        col=mix(col,vec3(0.30,0.88,0.98),band*0.95);
        if(d<u.brush.w)col=mix(col,vec3(0.25,0.75,0.85),0.10);
    }
    float fog=clamp((distance(worldPos,u.camera.xyz)-150.0)/1200.0,0.0,0.5);
    fragColor=vec4(mix(col,vec3(0.125,0.173,0.231),fog),baseColor.a);
}
