#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Vertex Shader: recebe a malha-base, ainda sem subdivisao.
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=0) out VS_OUT { vec3 worldPos; vec3 worldNormal; vec2 uv; } vs_out;
void main() {
    mat4 world=frame.model*draw.objectModel;
    vec4 p=world*vec4(inPosition,1.0);
    // Uma escala nula tornaria a inversa indefinida: usa a normal original nesse caso.
    mat3 basis=mat3(world);
    float det=determinant(basis);
    vec3 n=finiteFloat(det) && abs(det)>1e-8 ? transpose(inverse(basis))*inNormal : inNormal;
    vs_out.worldPos=p.xyz;
    vs_out.worldNormal=safeNormal(n,vec3(0,1,0));
    vs_out.uv=clamp(inUV,vec2(0),vec2(1));
    gl_Position=vec4(p.xyz,1); // Posicao provisoria; a projecao ocorre no TES.
}
