#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(triangles,fractional_even_spacing,ccw) in;
layout(location=0) in TC_OUT { vec3 worldPos; vec3 worldNormal; vec2 uv; } te_in[];
layout(set=1,binding=0) uniform sampler2D heightmap;
layout(location=0) out vec3 outWorldPos;
layout(location=1) out vec3 outNormal;
layout(location=2) out vec2 outUV;
float heightAt(vec2 uv) {
    // TES nao tem derivadas de tela; escolhe explicitamente mip 0, filtro linear/clamp.
    float h=textureLod(heightmap,clamp(uv,vec2(0),vec2(1)),0.0).r;
    return finiteFloat(h) ? clamp(h,0.0,1.0) : 0.5;
}
void main() {
    vec3 b=max(gl_TessCoord,vec3(0));
    float sum=b.x+b.y+b.z;
    b=finiteFloat(sum) && sum>1e-8 ? b/sum : vec3(1,0,0);
    vec3 p=b.x*te_in[0].worldPos+b.y*te_in[1].worldPos+b.z*te_in[2].worldPos;
    vec3 n=safeNormal(b.x*te_in[0].worldNormal+b.y*te_in[1].worldNormal+b.z*te_in[2].worldNormal,vec3(0,1,0));
    vec2 uv=clamp(b.x*te_in[0].uv+b.y*te_in[1].uv+b.z*te_in[2].uv,vec2(0),vec2(1));
    float scale=finiteFloat(frame.material.x) ? frame.material.x*draw.parameters.x : 0.0;
    if(abs(scale)>1e-6) {
        p+=n*(heightAt(uv)-0.5)*scale;
        vec2 stepUV=max(1.0/vec2(textureSize(heightmap,0)),vec2(max(frame.material.y,1e-6)));
        vec2 leftUV=vec2(max(uv.x-stepUV.x,0.0),uv.y), rightUV=vec2(min(uv.x+stepUV.x,1.0),uv.y);
        vec2 downUV=vec2(uv.x,max(uv.y-stepUV.y,0.0)), upUV=vec2(uv.x,min(uv.y+stepUV.y,1.0));
        // Diferencas centrais; nas bordas usa distancia efetiva (diferenca unilateral).
        float dhdu=(heightAt(rightUV)-heightAt(leftUV))/max(rightUV.x-leftUV.x,1e-6);
        float dhdv=(heightAt(upUV)-heightAt(downUV))/max(upUV.y-downUV.y,1e-6);
        vec3 e1=te_in[1].worldPos-te_in[0].worldPos, e2=te_in[2].worldPos-te_in[0].worldPos;
        vec2 a=te_in[1].uv-te_in[0].uv, c=te_in[2].uv-te_in[0].uv;
        float determinantUV=a.x*c.y-a.y*c.x;
        if(finiteFloat(determinantUV) && abs(determinantUV)>1e-8) {
            // Mantem a escala fisica da malha; nao normaliza as derivadas prematuramente.
            vec3 dpdu=(e1*c.y-e2*a.y)/determinantUV;
            vec3 dpdv=(e2*a.x-e1*c.x)/determinantUV;
            vec3 displacedU=dpdu+n*scale*dhdu, displacedV=dpdv+n*scale*dhdv;
            vec3 candidate=safeNormal(cross(displacedU,displacedV),n);
            n=dot(candidate,n)<0.0 ? -candidate : candidate;
        }
    }
    outWorldPos=p; outNormal=n; outUV=uv;
    gl_Position=frame.viewProj*vec4(p,1.0); // Rasterizador calcula profundidade z/w em [0,1].
}
