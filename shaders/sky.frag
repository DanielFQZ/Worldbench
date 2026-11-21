#version 440
layout(location=0) in vec3 direction;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;} u;
void main(){
    vec3 ray=normalize(direction);
    float height=pow(clamp(ray.y,0.0,1.0),.45);
    vec3 color=u.envFlags.x>.5 ? mix(u.horizonColor.rgb,u.skyColor.rgb,height) : vec3(.06,.08,.11);
    if(ray.y<0.0 && u.envFlags.x>.5)color=mix(u.horizonColor.rgb,u.groundColor.rgb,clamp(-ray.y*3.0,0.0,1.0));
    if(u.envFlags.z>.5){
        float haze=u.horizonColor.w;
        float horizon=exp(-abs(ray.y)*mix(8.0,1.5,haze))*haze;
        float glow=pow(max(0.0,dot(ray,u.sunDirection.xyz)),16.0)*u.sunDirection.w*.22*haze;
        color=mix(color,u.horizonColor.rgb,horizon)+u.sunColor.rgb*glow;
    }
    if(u.envFlags.y>.5){
        float sun=smoothstep(.99965,.99985,dot(ray,u.sunDirection.xyz));
        color+=sun*u.sunColor.rgb*u.sunDirection.w*2.0;
    }
    color*=u.groundColor.w;
    fragColor=vec4(color/(vec3(1.0)+color*.35),1);
}
