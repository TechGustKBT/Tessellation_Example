#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Tessellation Control: um patch triangular tem tres pontos de controle.
layout(vertices=3) out;
layout(location=0) in VS_OUT { vec3 worldPos; vec3 worldNormal; vec2 uv; } tc_in[];
layout(location=0) out TC_OUT { vec3 worldPos; vec3 worldNormal; vec2 uv; } tc_out[];
float edgeLevel(vec3 a,vec3 b) {
    if(draw.parameters.x<0.5) return 2.0; // Objetos rigidos nao precisam de subdivisao alta.
    float d0=max(frame.lodParams.x,0.0);
    float d1=max(frame.lodParams.y,d0+0.001);
    float lo=clamp(frame.lodParams.z,1.0,64.0);
    float hi=clamp(frame.lodParams.w,lo,64.0); // CPU tambem limita ao maximo da GPU.
    float distanceToEdge=length(frame.cameraPos.xyz-(a+b)*0.5);
    float t=clamp((distanceToEdge-d0)/(d1-d0),0.0,1.0);
    float level=mix(hi,lo,t);
    return finiteFloat(level) ? clamp(level,lo,hi) : lo;
}
void main() {
    // GLSL exige gl_InvocationID diretamente no indice das saidas por vertice.
    tc_out[gl_InvocationID].worldPos=tc_in[gl_InvocationID].worldPos;
    tc_out[gl_InvocationID].worldNormal=tc_in[gl_InvocationID].worldNormal;
    tc_out[gl_InvocationID].uv=tc_in[gl_InvocationID].uv;
    gl_out[gl_InvocationID].gl_Position=gl_in[gl_InvocationID].gl_Position;
    if(gl_InvocationID==0) {
        // O nivel de cada aresta depende apenas de suas pontas: vizinhos concordam.
        gl_TessLevelOuter[0]=edgeLevel(tc_in[1].worldPos,tc_in[2].worldPos);
        gl_TessLevelOuter[1]=edgeLevel(tc_in[2].worldPos,tc_in[0].worldPos);
        gl_TessLevelOuter[2]=edgeLevel(tc_in[0].worldPos,tc_in[1].worldPos);
        gl_TessLevelInner[0]=(gl_TessLevelOuter[0]+gl_TessLevelOuter[1]+gl_TessLevelOuter[2])/3.0;
    }
    // Sem barrier: so lemos entradas imutaveis; nenhuma invocacao le a saida de outra.
}
