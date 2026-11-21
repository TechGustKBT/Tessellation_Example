#ifndef SIM_COMMON_GLSL
#define SIM_COMMON_GLSL
// std140, set 0: um UBO exclusivo por frame; offsets: 0,64,128,144,160,176,192.
layout(set=0,binding=0,std140) uniform FrameData {
    mat4 model;             // Transformacao global da cena.
    mat4 viewProj;          // Projecao Vulkan (Z em [0,1]) * view.
    vec4 cameraPos;         // Posicao mundial da camera, w nao usado.
    vec4 lodParams;         // Distancia perto/longe, tess minimo/maximo.
    vec4 material;          // Escala de altura, epsilon UV, ambiente, expoente especular.
    vec4 lightDir;          // Direcao da superficie para a luz.
    vec4 lightColor;        // RGB linear e intensidade em w.
} frame;
layout(push_constant,std430) uniform DrawData {
    mat4 objectModel;       // Transformacao local -> mundo por objeto.
    vec4 parameters;        // x deslocamento; y material 0/1/2; z tempo; w diagnostico.
    vec4 tint;              // Cor linear RGBA do objeto.
} draw;
bool finiteFloat(float x) { return !isnan(x) && !isinf(x); }
vec3 safeNormal(vec3 v,vec3 fallback) {
    float m=dot(v,v);
    return finiteFloat(m) && m>1e-12 ? v*inversesqrt(m) : fallback;
}
#endif
