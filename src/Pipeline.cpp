#include "VulkanRenderer.hpp"
#include "EmbeddedResources.hpp"
#include <cstring> // Para funções de manipulação de memória, como std::memcpy e std::strcmp.
#include <cstddef> // Para definições de tamanho, como std::size_t e offsetof.
#include <fstream> // Para operações de leitura e escrita em arquivos.
#include <iostream> // Para operações de entrada e saída, como std::cout.

namespace simulator {

    // Funções relacionadas à criação e gerenciamento de pipelines Vulkan.
    VkShaderModule VulkanRenderer::shaderModule(const char* name) {
        int id = 0;

        // Determina o ID do recurso incorporado correspondente ao nome do shader.
        if (std::strcmp(name, "tessellation.vert.spv") == 0) id = IDR_TESS_VERT; // Vertex shader (Usado para a etapa de vértices)
        else if (std::strcmp(name, "tessellation.tesc.spv") == 0) id = IDR_TESS_TESC; // Tessellation control shader (Usado para a etapa de controle de tesselação)
        else if (std::strcmp(name, "tessellation.tese.spv") == 0) id = IDR_TESS_TESE; // Tessellation evaluation shader (Usado para a etapa de avaliação de tesselação)
        else if (std::strcmp(name, "tessellation.frag.spv") == 0) id = IDR_TESS_FRAG; // Fragment shader (Usado para a etapa de fragmento)
        else throw std::runtime_error(std::string("Shader desconhecido: ") + name);

        // Obtém o recurso incorporado correspondente ao ID determinado acima.
        auto resource = getEmbeddedResource(id);

        // Verifica se o recurso incorporado é um shader SPIR-V válido.
        if (resource.size < 20 || resource.size % 4 != 0) 
            throw std::runtime_error("Shader SPIR-V interno invalido.");

        // Converte os dados do recurso incorporado em palavras de 32 bits para validação SPIR-V.
        std::vector<uint32_t> words(resource.size / sizeof(uint32_t));
        // Copia os dados do recurso incorporado para o vetor de palavras de 32 bits.
        std::memcpy(words.data(), resource.data, resource.size);

        // Verifica se a primeira palavra corresponde à assinatura SPIR-V esperada.
        if (words[0] != 0x07230203u)
            throw std::runtime_error("Assinatura SPIR-V invalida.");

        // Cria a estrutura de informações para o módulo de shader Vulkan.
        VkShaderModuleCreateInfo info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO // Tipo da estrutura de criação do módulo de shader
        };

        // Define o tamanho do código SPIR-V em bytes.
        info.codeSize = resource.size;

        // Define o ponteiro para o código SPIR-V.
        info.pCode = words.data();

        // Cria o módulo de shader Vulkan a partir das informações definidas acima.
        VkShaderModule module = VK_NULL_HANDLE; // Inicializa o módulo de shader como nulo

        // Inicializa o módulo de shader como nulo antes de criar.
        vkCheck(
            // Chama a função Vulkan para criar o módulo de shader e verifica se ocorreu algum erro.
            vkCreateShaderModule(device_, &info, nullptr, &module),
            "criar shader module"
        );

        // Exibe uma mensagem indicando que o shader interno foi carregado com sucesso.
        std::cout << "Shader interno carregado: "
            << name << '\n';

        return module;
    }
    // Cria os pipelines gráficos necessários para o renderizador Vulkan.
    void VulkanRenderer::createPipelines() {
        // Cria os módulos de shader necessários para o pipeline gráfico.
        std::array<VkShaderModule,4> modules{};
        try {
            // Carrega os módulos de shader a partir dos arquivos SPIR-V correspondentes.
            const char* names[]={
                "tessellation.vert.spv",
                "tessellation.tesc.spv",
                "tessellation.tese.spv",
                "tessellation.frag.spv"
            };
            // Itera sobre os nomes dos arquivos de shader e cria os módulos correspondentes.
            VkShaderStageFlagBits types[] = {
                VK_SHADER_STAGE_VERTEX_BIT, // VK_SHADER_STAGE_VERTEX_BIT: indica que este estágio do pipeline é o vertex shader
                VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, // VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: indica que este estágio do pipeline é o tessellation control shader
                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, // VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: indica que este estágio do pipeline é o tessellation evaluation shader
                VK_SHADER_STAGE_FRAGMENT_BIT // VK_SHADER_STAGE_FRAGMENT_BIT: indica que este estágio do pipeline é o fragment shader
            };

            // Cria os estágios do pipeline gráfico a partir dos módulos de shader.
            std::array<VkPipelineShaderStageCreateInfo,4> stages{};
            // Itera sobre os módulos de shader e cria os estágios correspondentes.
            for(size_t i=0;i<4;++i) {
                // Cria o módulo de shader a partir do arquivo correspondente.
                modules[i]=shaderModule(names[i]); 
                // Inicializa a estrutura de criação do estágio do pipeline.
                //VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO: indica que esta estrutura é usada para criar um estágio de shader no pipeline gráfico.
                stages[i]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                // Define o tipo de shader para este estágio do pipeline.
                stages[i].stage=types[i];
                // Associa o módulo de shader ao estágio do pipeline.
                stages[i].module=modules[i]; 
                // Define o nome da função principal do shader.
                stages[i].pName="main";
            }
            // Configurações de entrada do pipeline de vértices.
            VkVertexInputBindingDescription binding{
                0, // Binding index
                sizeof(Vertex), // Tamanho do vértice
                VK_VERTEX_INPUT_RATE_VERTEX // Input rate por vértice
            };
            // Descrições dos atributos de vértice.
            // Cada atributo define a localização, binding, formato e offset dentro do vértice.
            VkVertexInputAttributeDescription attributes[]={
                {
                    0, // Localização
                    0, // Binding
                    VK_FORMAT_R32G32B32_SFLOAT, // VK_FORMAT_R32G32B32_SFLOAT: formato de 3 componentes de 32 bits em ponto flutuante
                    uint32_t(offsetof(Vertex,position)) // Offset dentro do vértice
                },
                {
                    1, // Localização
                    0, // Binding
                    VK_FORMAT_R32G32B32_SFLOAT, // VK_FORMAT_R32G32B32_SFLOAT: formato de 3 componentes de 32 bits em ponto flutuante
                    uint32_t(offsetof(Vertex,normal)) // Offset dentro do vértice
                },
                {
                    2, // Localização
                    0, // Binding
                    VK_FORMAT_R32G32_SFLOAT, // VK_FORMAT_R32G32_SFLOAT: formato de 2 componentes de 32 bits em ponto flutuante
                    uint32_t(offsetof(Vertex,uv)) // Offset dentro do vértice
                }
            };
            // Descrição do estado de entrada do vértice, incluindo bindings e atributos.
            // VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar o estado de entrada de vértices do pipeline gráfico.
            VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            // Configuração do estado de entrada do vértice
            vertex.vertexBindingDescriptionCount=1; 
            // Número de descrições de bindings de vértice
            vertex.pVertexBindingDescriptions=&binding; 
            // Número de descrições de atributos de vértice
            vertex.vertexAttributeDescriptionCount=3; 
            // Ponteiro para as descrições de atributos de vértice
            vertex.pVertexAttributeDescriptions=attributes;
            // Configuração do estado de montagem de primitivas
            // VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar o estado de montagem de primitivas do pipeline gráfico.
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; 
            // Configuração do estado de montagem de primitivas
            assembly.topology=VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            // Configuração do estado de tesselação
            // Sem PATCH_LIST + patchControlPoints + tessellationShader, TCS/TES nao executam.
            // VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar o estado de tesselação do pipeline gráfico.
            VkPipelineTessellationStateCreateInfo tessellation{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO}; 
            // Número de pontos de controle por patch
            tessellation.patchControlPoints=3;
            // VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar o estado de viewport do pipeline gráfico.
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            // Configuração do estado de viewport
            viewport.viewportCount=1; 
            // Número de viewports e scissors
            viewport.scissorCount=1;
            // VK_DYNAMIC_STATE_VIEWPORT: indica que o estado de viewport será dinâmico.
            // VK_DYNAMIC_STATE_SCISSOR: indica que o estado de scissor será dinâmico.
            VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            // Estados dinâmicos (viewport e scissor)
            // VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar os estados dinâmicos do pipeline gráfico. (de novo)
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; 
            // Número de estados dinâmicos
            dynamic.dynamicStateCount=2;
            // Ponteiros para os estados dinâmicos
            dynamic.pDynamicStates=dynamicStates;
            // Configuração do estado de rasterização do pipeline gráfico, que define como os polígonos serão rasterizados na tela.
            // VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO: indica que esta estrutura é usada para configurar o estado de rasterização do pipeline gráfico.
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; 
            // Configuração do modo de polígono.
            // VK_POLYGON_MODE_FILL: define o modo de polígono como preenchido.
            raster.polygonMode=VK_POLYGON_MODE_FILL; 
            // Configuração do culling de faces.
            // VK_CULL_MODE_NONE: desativa o culling de faces.
            raster.cullMode=VK_CULL_MODE_NONE; 
            // Configuração da face frontal.
            // VK_FRONT_FACE_COUNTER_CLOCKWISE: define a face frontal como sendo no sentido anti-horário.
            raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; 
            // Configuração da largura da linha.
            // Largura da linha
            raster.lineWidth=1;
            // Configuração da face frontal (repetida, possivelmente desnecessária).
            // VK_FRONT_FACE_COUNTER_CLOCKWISE: define a face frontal como sendo no sentido anti-horário.
            raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; 
            // Largura da linha
            raster.lineWidth=1;
            // Configuração do estado de multisampling.
            // VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO: define a estrutura de criação do estado de multisampling.
            VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; 
            // Configuração do estado de multisampling.
            // VK_SAMPLE_COUNT_1_BIT: define o número de amostras por pixel como 1 (sem multisampling).
            multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            // Configuração do estado de depth stencil.
            // VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO: define a estrutura de criação do estado de depth stencil.
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; 
            // Habilita o teste de profundidade.
            // VK_TRUE: habilita o teste de profundidade. Constante usada pelo Vulkan para representar verdadeiro.
            depth.depthTestEnable=VK_TRUE; 
            // Habilita a escrita no buffer de profundidade.
            // VK_TRUE: habilita a escrita no buffer de profundidade.
            depth.depthWriteEnable=VK_TRUE; 
            // Configura a operação de comparação de profundidade.
            // VK_COMPARE_OP_LESS_OR_EQUAL: passa o teste se a profundidade do fragmento for menor ou igual à profundidade armazenada.
            depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
            // Configuração do estado de blend de cores.
            VkPipelineColorBlendAttachmentState attachment{}; 
            // VK_COLOR_COMPONENT_R_BIT: habilita a escrita no componente de cor vermelha.
            // VK_COLOR_COMPONENT_G_BIT: habilita a escrita no componente de cor verde.
            // VK_COLOR_COMPONENT_B_BIT: habilita a escrita no componente de cor azul.
            // VK_COLOR_COMPONENT_A_BIT: habilita a escrita no componente de cor alfa.
            attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
            // Configuração do blend de cores.
            // VK_BLEND_FACTOR_SRC_ALPHA: fator de blend para a cor de origem baseado no alfa da origem.
            attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; 
            // Configuração do blend de cores para a cor de destino.
            // VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: fator de blend para a cor de destino baseado no inverso do alfa da origem.
            attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 
            // Configuração da operação de blend de cores.
            // VK_BLEND_OP_ADD: operação de blend que soma os valores ponderados da cor de origem e da cor de destino.
            attachment.colorBlendOp=VK_BLEND_OP_ADD;
            // Configuração do blend de cores para o canal alfa.
            // VK_BLEND_FACTOR_ONE: fator de blend para a cor de origem baseado no valor 1.
            attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
            // Configuração do blend de cores para o canal alfa da cor de destino.
            // VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: fator de blend para a cor de destino baseado no inverso do alfa da origem.
            attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 
            // Configuração da operação de blend de cores para o canal alfa.
            // VK_BLEND_OP_ADD: operação de blend que soma os valores ponderados da cor de origem e da cor de destino.
            attachment.alphaBlendOp=VK_BLEND_OP_ADD;
            // Configuração do estado de blend de cores do pipeline.
            // VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO: indica que a estrutura é do tipo de estado de blend de cores do pipeline.
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; 
            // Número de attachments de blend de cores e ponteiro para os attachments.
            blend.attachmentCount=1; 
            // Ponteiro para os attachments de blend de cores.
            blend.pAttachments=&attachment;
            // Configuração do estado de blend de cores do pipeline concluída.
            // VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO: indica que a estrutura é do tipo de criação de pipeline gráfico.
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; 
            // Configuração do estado do pipeline gráfico.
            info.stageCount=uint32_t(stages.size()); 
            // Número de estágios do pipeline gráfico.
            info.pStages=stages.data(); 
            // Ponteiro para os estágios do pipeline gráfico.
            info.pVertexInputState=&vertex; 
            // Configuração do estado de entrada de vértices.
            info.pInputAssemblyState=&assembly; 
            // Configuração do estado de montagem de primitivas.
            info.pTessellationState=&tessellation;
            // Configuração do estado de tesselação.
            info.pViewportState=&viewport; 
            // Configuração do estado de viewport.
            info.pRasterizationState=&raster; 
            // Configuração do estado de rasterização.
            info.pMultisampleState=&multisample; 
            // Configuração do estado de multisampling.
            info.pDepthStencilState=&depth; 
            // Configuração do estado de profundidade e stencil.
            info.pColorBlendState=&blend; 
            // Configuração do estado de blend de cores.
            info.pDynamicState=&dynamic;
            // Configuração do estado dinâmico.
            info.layout=pipelineLayout_; 
            // Layout do pipeline.
            info.renderPass=renderPass_; 
            // Render pass associado ao pipeline.
            info.subpass=0;
            // Subpass dentro do render pass.
            // VK_NULL_HANDLE: handle nulo para o pipeline cache, indicando que não será usado.
            vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&solid_),"criar pipeline solido com tessellation");
            // Se a opção de wireframe estiver habilitada, cria o pipeline wireframe.
            if(wireframeEnabled_) {
                // Configura o modo de polígono para wireframe.
                // VK_POLYGON_MODE_LINE: modo de polígono para wireframe.
                raster.polygonMode=VK_POLYGON_MODE_LINE; 
                // Criação do pipeline gráfico wireframe.
                vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&wire_),"criar pipeline wireframe");
            }
            // Configuração do pipeline para transparência.
            // VK_POLYGON_MODE_FILL: modo de polígono para preenchimento.
            raster.polygonMode=VK_POLYGON_MODE_FILL; 
            // Habilita o blend de cores para transparência.
            attachment.blendEnable=VK_TRUE; 
            // Desabilita a escrita no buffer de profundidade.
            depth.depthWriteEnable=VK_FALSE;
            // Criação do pipeline gráfico para transparência.
            vkCheck(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&info,nullptr,&transparent_),"criar pipeline de fumaca");
        } catch(...) {
            // Em caso de exceção, destrói todos os módulos de shader criados.
            for(auto m:modules) 
                if(m) 
                    vkDestroyShaderModule(device_,m,nullptr); throw;
        }
        // O pipeline ja reteve o codigo necessario; os modulos temporarios podem ser liberados.
        for(auto module:modules) 
            vkDestroyShaderModule(device_,module,nullptr);
    }
}
