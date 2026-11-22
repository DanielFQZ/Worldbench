#version 440
layout(location=0) in vec3 direction;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;vec4 skyEffects;} u;
float cloudHash(vec2 p){
    p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32);
    return fract(p.x*p.y);
}
float cloudNoise(vec2 p){
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    float a=cloudHash(i), b=cloudHash(i+vec2(1,0));
    float c=cloudHash(i+vec2(0,1)), d=cloudHash(i+vec2(1,1));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
float cloudField(vec2 p){
    float n=cloudNoise(p)*.62;
    n+=cloudNoise(p*2.07+vec2(17.2,-8.4))*.28;
    n+=cloudNoise(p*4.13+vec2(-4.7,23.1))*.10;
    return n;
}
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
    if(u.skyEffects.w>.5 && ray.y>-.02){
        // A two-dimensional projected cloud layer is much cheaper than a
        // volumetric march and remains stable in the reflected sky pass.
        float horizonFade=smoothstep(-.02,.28,ray.y);
        vec2 cloudUv=ray.xz*4.5/max(ray.y+.35,.12);
        cloudUv+=vec2(u.water.x*.0009*mix(.35,1.0,u.shadowData.w),
                     -u.water.x*.00055*mix(.35,1.0,u.shadowData.w));
        float density=clamp(u.shadowData.z,0.0,1.0);
        float field=cloudField(cloudUv);
        float coverage=smoothstep(.42-density*.24,.68-density*.12,field)*horizonFade;
        vec3 cloudColor=mix(vec3(.42,.48,.57),vec3(1.0),smoothstep(.42,.82,field));
        color=mix(color,cloudColor,coverage*.9);
    }
    color*=u.groundColor.w;
    fragColor=vec4(color/(vec3(1.0)+color*.35),1);
}
