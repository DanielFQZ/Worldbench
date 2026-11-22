#version 440
layout(location=0) in vec3 worldPos;
layout(location=1) in vec3 faceNormal;
layout(location=2) in vec4 baseColor;
layout(location=3) in vec2 textureUV;
layout(binding=4) uniform sampler2D reflectionColor;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Scene {mat4 mvp;vec4 camera;vec4 brush;vec4 params;vec4 selection;vec4 flags;vec4 sunDirection;vec4 skyColor;vec4 groundColor;vec4 sunColor;vec4 horizonColor;vec4 envFlags;mat4 inverseView;mat4 shadowNearMatrix;mat4 shadowFarMatrix;mat4 reflectionMatrix;vec4 water;vec4 effects;vec4 shadowData;} u;

layout(binding=2) uniform sampler2D shadowNear;
layout(binding=3) uniform sampler2D shadowFar;
float pcf(sampler2D map, vec3 p, float bias) {
    // Compare the receiver plane at each sampled texel centre. A fixed depth
    // bias alone causes stripes on slopes when PCF taps cross different depths.
    vec3 dx=dFdx(p), dy=dFdy(p);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12 ? vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/determinant : vec2(0);
    float sum=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 uv=(floor(p.xy/u.effects.y+vec2(x,y))+.5)*u.effects.y;
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
    if(all(greaterThan(q,vec3(0)))&&all(lessThan(q,vec3(1))))farLight=pcf(shadowFar,q,u.shadowData.y*slope);
    float margin=min(min(p.x,1.0-p.x),min(p.y,1.0-p.y));
    if(margin<=0.0||p.z<=0.0||p.z>=1.0)return farLight;
    float nearLight=pcf(shadowNear,p,u.shadowData.x*slope);
    return mix(farLight,nearLight,smoothstep(.01,.08,margin));
}

void main(){
    vec3 n=normalize(faceNormal);
    // Analytic band-limited normal waves: no CPU mesh rebuild, no movement of
    // Minecraft coastlines, and subpixel frequencies fade instead of shimmering.
    if(n.y>.5 && u.water.y>0.0){
        vec2 gradient=vec2(0);
        vec2 dirs[3]=vec2[3](vec2(.96,.28),vec2(-.64,.77),vec2(.23,-.97));
        float frequencies[3]=float[3](.72,1.57,3.1);
        for(int i=0;i<3;++i){
            float phase=dot(worldPos.xz,dirs[i])*frequencies[i]+u.water.x*(.9+float(i)*.39);
            float attenuation=1.0-smoothstep(.5,2.7,fwidth(phase));
            gradient+=dirs[i]*cos(phase)*attenuation*(.40/(1.0+float(i)))*u.water.y;
        }
        n=normalize(vec3(-gradient.x,1,-gradient.y));
    }
    vec3 view=normalize(u.camera.xyz-worldPos);
    float fresnel=.025+.975*pow(1.0-clamp(dot(n,view),0.0,1.0),5.0);
    float lit=shadowVisibility(worldPos,n);
    vec3 tint=clamp(baseColor.rgb,vec3(.015),vec3(.9));
    vec3 col=tint*.9;
    if(u.sunColor.w>.5){
        vec3 ambient=mix(u.groundColor.rgb,u.skyColor.rgb,.8)*u.skyColor.w;
        vec3 direct=u.sunColor.rgb*max(0.0,dot(n,u.sunDirection.xyz))*u.sunDirection.w*lit;
        col=tint*(ambient+direct*.55)*u.groundColor.w;
        vec3 halfVector=normalize(view+u.sunDirection.xyz);
        float spec=pow(max(0.0,dot(n,halfVector)),120.0)*u.sunDirection.w*lit;
        col+=u.sunColor.rgb*spec*1.8*u.groundColor.w;
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
        uv+=n.xz*.012;
        float edge=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));
        if(abs(worldPos.y-u.water.w)<.05 && projected.w>0.0 && edge>0.0)
            reflected=mix(reflected,texture(reflectionColor,uv).rgb,smoothstep(0.0,.025,edge));
        col=mix(col,reflected,.20+.70*fresnel);
    }
    if(u.sunColor.w>.5 && u.envFlags.z>.5){
        float haze=u.horizonColor.w;
        float density=(haze+63.0*pow(haze,4.0))/16000.0;
        float fog=1.0-exp(-distance(worldPos,u.camera.xyz)*density);
        vec3 fogColor=u.horizonColor.rgb*u.groundColor.w;
        fogColor=fogColor/(vec3(1)+fogColor*.35);
        col=mix(col,fogColor,fog);
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

    fragColor=vec4(col,.76+.18*fresnel);
}
