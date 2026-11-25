#version 440
layout(location=0) in vec3 worldPos;
layout(location=1) in vec3 faceNormal;
layout(location=2) in vec4 baseColor;
layout(location=3) in vec2 textureUV;
layout(binding=1) uniform sampler2D atlas;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;vec4 skyEffects;float skyTextureEnabled;vec4 waterFog;vec4 waterFogColor;vec4 waterTint;vec4 waterReflectionTint;vec4 waterFoamColor;vec4 renderData;mat4 model;} u;

layout(binding=2) uniform sampler2D shadowNear;
layout(binding=3) uniform sampler2D shadowFar;
float pcf(sampler2D map, vec3 p, float bias, float texel) {
    // Compare the receiver plane at each sampled texel centre. A fixed depth
    // bias alone causes stripes on slopes when PCF taps cross different depths.
    vec3 dx=dFdx(p), dy=dFdy(p);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12 ? vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant : vec2(0);
    float sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 uv=(floor(p.xy/texel+vec2(x,y))+.5)*texel;
        float depth=p.z+clamp(dot(gradient,uv-p.xy),-.01,.01)-bias;
        sum+=depth<=texture(map,uv).r ? 1.0 : 0.0;
    }
    return sum/9.0;
}
float shadowVisibility(vec3 position, vec3 normal) {
    if(u.effects.x<.5)return 1.0;
    vec4 a=u.shadowNearMatrix*vec4(position,1), b=u.shadowFarMatrix*vec4(position,1);
    vec3 p=a.xyz/a.w, q=b.xyz/b.w;
    float slope=1.0+2.0*(1.0-max(0.0,dot(normalize(normal),u.sunDirection.xyz)));
    float farLight=1.0;
    if(all(greaterThan(q,vec3(0)))&&all(lessThan(q,vec3(1))))farLight=pcf(shadowFar,q,u.shadowData.y*slope,u.renderData.x);
    float margin=min(min(p.x,1.0-p.x),min(p.y,1.0-p.y));
    if(margin<=0.0||p.z<=0.0||p.z>=1.0)return farLight;
    float nearLight=pcf(shadowNear,p,u.shadowData.x*slope,u.effects.y);
    return mix(farLight,nearLight,smoothstep(.01,.08,margin));
}

void main(){
    if(u.effects.z>.5 && worldPos.y<u.water.w-.02)discard;
    vec2 size = vec2(textureSize(atlas, 0));
    vec2 dx = dFdx(textureUV) * size, dy = dFdy(textureUV) * size;
    float lod = .5 * log2(max(1., max(dot(dx, dx), dot(dy, dy))));
    // Block atlas has 32px aligned tiles. Terrain color pages have their own mip chain.
    if (all(equal(size, vec2(u.renderData.z)))) lod = min(lod, 5.);
    vec4 sampled=(textureUV.x<0.0 ? vec4(1.0) : textureLod(atlas, textureUV, lod))*baseColor;
    if(sampled.a<0.05)discard;
    float light=0.52+0.48*max(0.0,dot(faceNormal,normalize(vec3(-0.45,0.85,0.3))));
    vec3 col=sampled.rgb*light;
    vec2 uv=abs(faceNormal.y)>0.5?worldPos.xz:(abs(faceNormal.x)>0.5?worldPos.zy:worldPos.xy);
    vec2 cell=abs(fract(uv-0.5)-0.5)/max(fwidth(uv),vec2(0.0001));
    float edge=1.0-smoothstep(0.0,0.65,min(cell.x,cell.y));
    col*=1.0-edge*0.17*(1.0-smoothstep(100.0,300.0,distance(worldPos,u.camera.xyz)));
    if(u.sunColor.w>0.5){
        vec3 ambient=mix(u.groundColor.rgb,u.skyColor.rgb,faceNormal.y*.5+.5)*u.skyColor.w;
        vec3 direct=u.sunColor.rgb*max(0.0,dot(faceNormal,u.sunDirection.xyz))*u.sunDirection.w*shadowVisibility(worldPos,faceNormal);
        col=sampled.rgb*(ambient+direct)*u.groundColor.w;
        if(u.envFlags.z>0.5){
            // Keep low settings useful over large maps; high settings also work
            // at editing distance (100% has a 250-block e-folding distance).
            float haze=u.horizonColor.w;
            float density=(haze+63.0*pow(haze,4.0))/16000.0;
            float fog=1.0-exp(-distance(worldPos,u.camera.xyz)*density);
            vec3 ray=normalize(worldPos-u.camera.xyz);
            float glow=pow(max(0.0,dot(ray,u.sunDirection.xyz)),16.0)*u.sunDirection.w*.22;
            col=mix(col,(u.horizonColor.rgb+u.sunColor.rgb*glow)*u.groundColor.w,fog);
        }
        col=col/(vec3(1.0)+col*.35);
    }
    // Beer-Lambert extinction along the actual visible ray inside the water.
    // The terrain/structures below the surface are fogged, not the surface alpha.
    if(u.waterFog.z>.5 && min(worldPos.y,u.camera.y)<u.water.w){
        float segment=distance(u.camera.xyz,worldPos);
        if(u.camera.y>u.water.w)
            segment*=clamp((u.water.w-worldPos.y)/max(.001,u.camera.y-worldPos.y),0.0,1.0);
        else if(worldPos.y>u.water.w)
            segment*=clamp((u.water.w-u.camera.y)/max(.001,worldPos.y-u.camera.y),0.0,1.0);
        float transmission=exp(-u.waterFog.x*max(0.0,segment-u.waterFog.y));
        col=mix(u.waterFogColor.rgb,col,transmission);
    }
    if(u.params.x>0.5){
        vec3 delta=abs(worldPos-u.brush.xyz);
        float d=u.params.y<1.5?(u.params.z>0.5?max(delta.x,delta.z):length(delta.xz)):(u.params.z>0.5?max(delta.x,max(delta.y,delta.z)):length(delta));
        float pixel=max(fwidth(d),.08);
        float band=1.0-smoothstep(.6*pixel,1.5*pixel,abs(d-u.brush.w*u.params.w));
        float outer=1.0-smoothstep(.6*pixel,1.5*pixel,abs(d-u.brush.w));
        col=mix(col,vec3(0.62,0.87,0.91),outer*0.45);
        col=mix(col,vec3(0.30,0.88,0.98),band*0.95);
        if(d<u.brush.w)col=mix(col,vec3(0.25,0.75,0.85),0.10);
    }
    if(u.flags.x>0.5){
        // Derivatives keep the boundary readable in pixels across exact meshes
        // and far tiles. Apply after atmosphere so fog cannot hide the selection.
        vec2 pixelSize=max(fwidth(worldPos.xz),vec2(.0001));
        vec2 inset=min(worldPos.xz-u.selection.xy,u.selection.zw-worldPos.xz)/pixelSize;
        float edge=min(inset.x,inset.y);
        float coverage=smoothstep(-.75,.75,edge);
        float outline=1.0-smoothstep(1.0,2.5,edge);
        col=mix(col,vec3(1.0,0.68,0.2),coverage*(.12+.78*outline));
    }
    fragColor=vec4(col,sampled.a);
}
