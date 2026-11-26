#version 440
layout(location=0) in vec3 worldPos;
layout(location=1) in vec3 faceNormal;
layout(location=2) in vec4 baseColor;
layout(location=3) in vec2 textureUV;
layout(binding=4) uniform sampler2D reflectionColor;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;vec4 skyEffects;float skyTextureEnabled;float textureAnisotropy;vec4 waterFog;vec4 waterFogColor;vec4 waterTint;vec4 waterReflectionTint;vec4 waterFoamColor;vec4 renderData;mat4 model;} u;

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

// World-anchored, non-periodic gradient field. Quintic interpolation keeps
// cell boundaries smooth; rotating each octave removes a common lattice axis.
float hashWater(vec2 cell) {
    vec2 p=mod(cell,4093.0);
    vec3 q=fract(vec3(p.x,p.y,p.x+p.y)*vec3(.1031,.11369,.13787));
    q+=dot(q,q.yzx+19.19);
    return fract((q.x+q.y)*q.z);
}
vec2 waterGradient(vec2 p) {
    vec2 c=floor(p), f=fract(p);
    vec2 u0=f*f*f*(f*(f*6.0-15.0)+10.0);
    vec2 du=30.0*f*f*(f*(f-2.0)+1.0);
    float a=hashWater(c), b=hashWater(c+vec2(1,0));
    float c0=hashWater(c+vec2(0,1)), d=hashWater(c+vec2(1,1));
    return vec2(mix(b-a,d-c0,u0.y)*du.x,mix(c0-a,d-b,u0.x)*du.y);
}

void main(){
    vec3 n=normalize(faceNormal);
    if(n.y>.5 && u.water.y>0.0){
        // World units are Minecraft blocks (approximately one metre). Keep
        // the broad swell below a few dozen metres and add sub-block ripples
        // for close views; derivative coverage removes bands once they are
        // smaller than a pixel.
        float density=mix(1.10,.38,clamp(u.effects.w,0.0,1.0));
        const float scales[5]=float[5](28.0,10.0,3.5,1.2,.40);
        const float weights[5]=float[5](.14,.115,.08,.055,.032);
        vec2 gradient=vec2(0);
        mat2 rotation=mat2(.8,.6,-.6,.8);
        for(int i=0;i<5;++i){
            float cellSize=scales[i]*density;
            vec2 p=rotation*worldPos.xz/cellSize;
            // Attenuate before the normal field reaches the pixel Nyquist
            // limit; do not rely on blurring the final bright specular peaks.
            float footprint=max(length(dFdx(p)),length(dFdy(p)));
            float coverage=1.0-smoothstep(.12,.5,footprint);
            vec2 flow=vec2(.17+float(i)*.031,-.11+float(i)*.017)*u.water.x;
            gradient+=transpose(rotation)*waterGradient(p+flow+float(i)*31.7)*weights[i]*coverage;
            rotation=mat2(.362,.932,-.932,.362)*rotation;
        }
        gradient*=u.water.y;
        n=normalize(vec3(-gradient.x,1,-gradient.y));
    }
    // Normal variance widens the sun lobe at grazing/minified views instead
    // of letting a fixed narrow highlight sparkle between adjacent pixels.
    vec3 nx=dFdx(n), ny=dFdy(n);
    float variance=min(.3,dot(nx,nx)+dot(ny,ny));
    float exponent=2.0/(2.0/64.0+variance*4.0);
    vec3 view=normalize(u.camera.xyz-worldPos);
    float fresnel=.025+.975*pow(1.0-clamp(dot(n,view),0.0,1.0),5.0);
    float lit=shadowVisibility(worldPos,n);
    float depth=clamp(baseColor.a,0.0,1.0);
    // A thin colored interface; the scene below already contains path-length fog.
    vec3 tint=u.waterTint.rgb;
    vec3 col=tint*.9;
    if(u.sunColor.w>.5){
        vec3 ambient=mix(u.groundColor.rgb,u.skyColor.rgb,.8)*u.skyColor.w;
        vec3 direct=u.sunColor.rgb*max(0.0,dot(n,u.sunDirection.xyz))*u.sunDirection.w*lit;
        col=tint*(ambient+direct*.55)*u.groundColor.w;
        vec3 halfVector=normalize(view+u.sunDirection.xyz);
        float spec=pow(max(0.0,dot(n,halfVector)),exponent)*(exponent/64.0)*u.sunDirection.w*lit;
        col+=u.sunColor.rgb*spec*.95*u.groundColor.w;
        col=col/(vec3(1)+col*.35);
    }
    // Only the common horizontal sea plane has a geometric reflection capture.
    // Isolated voxel water at other elevations uses sky reflection, never a
    // knowingly wrong mirror plane. Glass and water do not enter the capture.
    if(u.water.z>.5 && faceNormal.y>.5){
        vec3 reflectedRay=reflect(-view,n);
        vec3 reflected=mix(u.horizonColor.rgb,u.skyColor.rgb,pow(clamp(reflectedRay.y,0.0,1.0),.45));
        if(u.sunColor.w>.5){reflected*=u.groundColor.w;reflected=reflected/(vec3(1)+reflected*.35);}
        vec4 projected=u.reflectionMatrix*vec4(worldPos,1);
        vec2 uv=projected.xy/projected.w;
        // Distort the captured scene with the animated surface normal. The
        // offset is deliberately larger at grazing angles so reflections do
        // not look like an unbroken planar mirror, while textureGrad keeps
        // high-frequency ripples filtered instead of producing moire.
        uv+=n.xz*(.012+.028*fresnel);
        vec2 reflectionDx=dFdx(uv), reflectionDy=dFdy(uv);
        float edge=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));
        if(abs(worldPos.y-u.water.w)<.05 && projected.w>0.0 && edge>0.0)
            reflected=mix(reflected,textureGrad(reflectionColor,uv,reflectionDx*1.5,reflectionDy*1.5).rgb,smoothstep(0.0,.025,edge));
        col=mix(col,reflected*u.waterReflectionTint.rgb,.55+.40*fresnel);
    }
    float foamWidth=max(.008,u.skyEffects.z);
    // Keep a narrow band just inside the shoreline. This prevents an entire
    // shallow ocean shelf from being mistaken for foam while still covering
    // the first few blocks next to land.
    float shoreline=smoothstep(0.0,foamWidth*.20,depth) *
                    (1.0-smoothstep(foamWidth*.55,foamWidth,depth));
    // Keep the edge broad and calm. Wave normals already provide animated
    // motion; adding a tiled bright pattern here would read as moire instead
    // of foam on large, shallow shelves.
    float foam=shoreline*clamp(u.skyEffects.y,0.0,1.0);
    col=mix(col,u.waterFoamColor.rgb,foam*.78);
    if(u.sunColor.w>.5 && u.envFlags.z>.5){
        float haze=u.horizonColor.w;
        float density=(haze+63.0*pow(haze,4.0))/16000.0;
        float fog=1.0-exp(-distance(worldPos,u.camera.xyz)*density);
        vec3 fogColor=u.horizonColor.rgb*u.groundColor.w;
        fogColor=fogColor/(vec3(1)+fogColor*.35);
        col=mix(col,fogColor,fog);
    }
    if(u.waterFog.z>.5 && u.camera.y<u.water.w){
        float fog=1.0-exp(-u.waterFog.x*max(0.0,distance(worldPos,u.camera.xyz)-u.waterFog.y));
        col=mix(col,u.waterFogColor.rgb,fog);
    }
    // The brush hit is resolved against the underwater terrain column. Do not
    // draw a second footprint on the water surface: it makes one brush look
    // like two concentric edits when the cursor crosses a shoreline.
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

    float alpha=mix(.18,.92,fresnel);
    alpha=clamp(alpha+foam*.18,0.0,1.0);
    fragColor=vec4(col,alpha);
}
