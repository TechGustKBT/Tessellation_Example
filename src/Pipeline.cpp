#include "VulkanRenderer.hpp"
#include <cstddef>
#include <fstream>
#include <iostream>

namespace simulator {
VkShaderModule VulkanRenderer::shaderModule(const char* name) {
    auto path=options_.shaderDir/name;
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("SPIR-V nao encontrado: "+path.string()+". Compile o alvo simulator_shaders.");
    std::streamoff length=file.tellg();
    if(length<20 || length%4!=0) throw std::runtime_error("Tamanho SPIR-V invalido: "+path.string());
    std::vector<uint32_t> words(size_t(length)/4); file.seekg(0); file.read(reinterpret_cast<char*>(words.data()),std::streamsize(length));
    if(!file || words[0]!=0x07230203u) throw std::runtime_error("Arquivo SPIR-V invalido: "+path.string());
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize=size_t(length); info.pCode=words.data();
    VkShaderModule module=VK_NULL_HANDLE; vkCheck(vkCreateShaderModule(device_,&info,nullptr,&module),"criar shader module");
    std::cout<<"SPIR-V carregado: "<<name<<" ("<<length<<" bytes)\n"; return module;
}
void VulkanRenderer::createPipelines() {
    std::array<VkShaderModule,4> modules{};
    try {
        const char* names[]={"tessellation.vert.spv","tessellation.tesc.spv","tessellation.tese.spv","tessellation.frag.spv"};
        VkShaderStageFlagBits types[]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,VK_SHADER_STAGE_FRAGMENT_BIT};
        std::array<VkPipelineShaderStageCreateInfo,4> stages{};
        for(size_t i=0;i<4;++i) {modules[i]=shaderModule(names[i]); stages[i]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; stages[i].stage=types[i]; stages[i].module=modules[i]; stages[i].pName="main";}
        VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,uint32_t(offsetof(Vertex,position))},{1,0,VK_FORMAT_R32G32B32_SFLOAT,uint32_t(offsetof(Vertex,normal))},{2,0,VK_FORMAT_R32G32_SFLOAT,uint32_t(offsetof(Vertex,uv))}};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vertex.vertexBindingDescriptionCount=1; vertex.pVertexBindingDescriptions=&binding; vertex.vertexAttributeDescriptionCount=3; vertex.pVertexAttributeDescriptions=attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        // Sem PATCH_LIST + patchControlPoints + tessellationShader, TCS/TES nao executam.
        VkPipelineTessellationStateCreateInfo tessellation{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO}; tessellation.patchControlPoints=3;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=1; viewport.scissorCount=1;
        VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamicStates;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthTestEnable=VK_TRUE; depth.depthWriteEnable=VK_TRUE; depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; attachment.colorBlendOp=VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; attachment.alphaBlendOp=VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount=1; blend.pAttachments=&attachment;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.stageCount=uint32_t(stages.size()); info.pStages=stages.data(); info.pVertexInputState=&vertex; info.pInputAssemblyState=&assembly; info.pTessellationState=&tessellation;
        info.pViewportState=&viewport; info.pRasterizationState=&raster; info.pMultisampleState=&multisample; info.pDepthStencilState=&depth; info.pColorBlendState=&blend; info.pDynamicState=&dynamic;
        info.layout=pipelineLayout_; info.renderPass=renderPass_; info.subpass=0;
        vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&solid_),"criar pipeline solido com tessellation");
        if(wireframeEnabled_) {raster.polygonMode=VK_POLYGON_MODE_LINE; vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&wire_),"criar pipeline wireframe");}
        raster.polygonMode=VK_POLYGON_MODE_FILL; attachment.blendEnable=VK_TRUE; depth.depthWriteEnable=VK_FALSE;
        vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&transparent_),"criar pipeline de fumaca");
    } catch(...) {for(auto m:modules) if(m) vkDestroyShaderModule(device_,m,nullptr); throw;}
    // O pipeline ja reteve o codigo necessario; os modulos temporarios podem ser liberados.
    for(auto module:modules) vkDestroyShaderModule(device_,module,nullptr);
}
}
