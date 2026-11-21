#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location=0) in vec3 inWorldPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(set=1,binding=1) uniform sampler2D albedoMap;
layout(location=0) out vec4 outColor;
void main() {
    // texture() usa derivadas de tela para escolher mip; sampler trilinear, clamp-to-edge.
    vec4 base=draw.parameters.y<0.5 ? texture(albedoMap,clamp(inUV,vec2(0),vec2(1)))*draw.tint : draw.tint;
    if(base.a<0.01) discard;
    vec3 n=safeNormal(inNormal,vec3(0,1,0));
    if(draw.parameters.w>0.5) { outColor=vec4(n*0.5+0.5,1); return; }
    if(draw.parameters.y>1.5) { outColor=vec4(clamp(base.rgb,0.0,1.0),base.a); return; }
    vec3 l=safeNormal(frame.lightDir.xyz,vec3(0,1,0));
    vec3 v=safeNormal(frame.cameraPos.xyz-inWorldPos,vec3(0,1,0));
    vec3 h=safeNormal(l+v,n);
    float diffuse=max(dot(n,l),0.0);
    float specular=diffuse>0.0 ? pow(max(dot(n,h),0.0),clamp(frame.material.w,1.0,256.0))*0.15 : 0.0;
    float ambient=clamp(frame.material.z,0.0,1.0);
    vec3 lit=base.rgb*ambient+(base.rgb*diffuse+vec3(specular))*frame.lightColor.rgb*max(frame.lightColor.w,0.0);
    // Saida linear; a attachment SRGB converte para a tela, sem aplicar gamma duas vezes.
    outColor=vec4(clamp(lit,0.0,1.0),clamp(base.a,0.0,1.0));
}
