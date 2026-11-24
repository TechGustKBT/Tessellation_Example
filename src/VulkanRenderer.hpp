#pragma once // Evita inclusões múltiplas do mesmo arquivo.
#include <GLFW/glfw3.h> // Inclui a biblioteca GLFW para gerenciamento de janelas e entrada.
#include "Simulation.hpp" // Inclui a definição da simulação.
#include <array> // Inclui a biblioteca array para uso de std::array.
#include <atomic> // Inclui a biblioteca atomic para uso de std::atomic.
#include <filesystem> // Inclui a biblioteca filesystem para manipulação de caminhos de arquivos.
#include <optional> // Inclui a biblioteca optional para uso de std::optional.
#include <stdexcept> // Inclui a biblioteca stdexcept para exceções padrão.
#include <string> // Inclui a biblioteca string para uso de std::string.

namespace simulator {
    // vkCheck: Função auxiliar para verificar resultados de chamadas Vulkan.
    // Lança uma exceção std::runtime_error se o resultado não for VK_SUCCESS.
    // Parâmetros:
    // - result: resultado da chamada Vulkan a ser verificada.
    // - operation: descrição da operação que foi realizada.
    inline void vkCheck(VkResult result, const char* operation) {
        if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" falhou, VkResult="+std::to_string(result));
    }
    // Buffer: Estrutura auxiliar para representar um buffer Vulkan.
    // Contém o handle do buffer (VkBuffer), a memória associada (VkDeviceMemory), o tamanho e um ponteiro mapeado (void*).
    struct Buffer { 
        VkBuffer handle=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkDeviceSize size=0; void* mapped=nullptr; 
    };
    // Image: Estrutura auxiliar para representar uma imagem Vulkan.
    // Contém o handle da imagem (VkImage), a memória associada (VkDeviceMemory), a view (VkImageView) e o número de mip levels (uint32_t).
    struct Image { 
        VkImage handle=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; uint32_t levels=1; 
    };
    // Frame: Estrutura auxiliar para representar um frame de renderização.
    // Contém o command buffer (VkCommandBuffer), semáforo de aquisição (VkSemaphore), fence (VkFence), buffer uniforme (Buffer) e descriptor set (VkDescriptorSet).
    struct Frame {
        VkCommandBuffer commands=VK_NULL_HANDLE; // Command buffer do frame
        VkSemaphore acquired=VK_NULL_HANDLE; // Semáforo de aquisição do frame
        VkFence fence=VK_NULL_HANDLE; // Fence do frame
        Buffer uniform; // Buffer uniforme do frame
        VkDescriptorSet set=VK_NULL_HANDLE; // Descriptor set do frame
    };
    // FrameUBO: Estrutura auxiliar para representar o uniform buffer object do frame.
    struct alignas(16) FrameUBO { // Uniform buffer object do frame
        glm::mat4 model{1}, viewProj{1};
        glm::vec4 cameraPos{}, lodParams{}, material{}, lightDir{}, lightColor{};
    };
    // Verifica se o tamanho do FrameUBO corresponde ao esperado pelo layout std140 do GLSL. 
    // Isso garante que a memória alocada para o uniform buffer seja compatível com o shader.
    static_assert(sizeof(FrameUBO)==208,"FrameUBO deve corresponder ao std140 GLSL");
    // DrawPush: Estrutura auxiliar para representar os push constants usados no desenho.
    // Contém a matriz de transformação do objeto (objectModel) e dois vetores de parâmetros (parameters e tint).
    // Os push constants são uma forma eficiente de passar pequenas quantidades de dados para os shaders sem a necessidade de buffers uniformes.
    struct alignas(16) DrawPush { // Push constants para o desenho
        glm::mat4 objectModel{1};
        glm::vec4 parameters{}, tint{1};
    };
    // Verifica se o tamanho do DrawPush corresponde ao esperado pelo layout de push constants no shader.
    // Isso garante que a memória alocada para os push constants seja compatível com o shader.
    static_assert(sizeof(DrawPush)==96,"Layout incorreto das push constants");
    struct Options { bool validation=true, requireValidation=false, smoke=false; int frames=0; std::filesystem::path shaderDir; std::filesystem::path assetDir; std::filesystem::path capture; };

    // Classe principal responsável por gerenciar a renderização com Vulkan.
    class VulkanRenderer {
        public:
            VulkanRenderer(GLFWwindow* window, Options options); // Construtor que recebe a janela GLFW e as opções de inicialização.
            ~VulkanRenderer(); // Destrutor que libera os recursos alocados pelo VulkanRenderer.
            VulkanRenderer(const VulkanRenderer&)=delete; // Desabilita o construtor de cópia.
            VulkanRenderer& operator=(const VulkanRenderer&)=delete; // Desabilita o operador de atribuição de cópia.
            void initialize(const Simulation& simulation); // Inicializa o VulkanRenderer com a simulação fornecida.
            bool draw(Simulation& simulation, bool capture=false); // Desenha a simulação atual, opcionalmente capturando a imagem.
            void resize() { resized_=true; } // Marca a janela como redimensionada, para que o swapchain seja recriado.
            bool wireframeSupported() const { return wireframeEnabled_; } // Retorna se o modo wireframe é suportado.
            uint32_t maxTessellation() const { return properties_.limits.maxTessellationGenerationLevel; } // Retorna o nível máximo de tesselação suportado pela GPU.
            const char* gpuName() const { return properties_.deviceName; } // Retorna o nome da GPU.
            int validationErrors() const { return validationErrors_.load(); } // Retorna o número de erros de validação detectados.
            void shutdown(); // Encerra o VulkanRenderer e libera os recursos alocados.
        private:
            static constexpr size_t framesInFlight=2; // Número de frames que podem estar em voo simultaneamente.
            GLFWwindow* window_; // Ponteiro para a janela GLFW.
            Options options_; // Opções de inicialização do VulkanRenderer.
            VkInstance instance_=VK_NULL_HANDLE; // Instância do Vulkan.
            VkDebugUtilsMessengerEXT messenger_=VK_NULL_HANDLE; // Mensageiro de depuração do Vulkan.
            VkSurfaceKHR surface_=VK_NULL_HANDLE; // Superfície de apresentação do Vulkan.
            VkPhysicalDevice physical_=VK_NULL_HANDLE; // Dispositivo físico (GPU) selecionado.
            VkPhysicalDeviceProperties properties_{}; // Propriedades do dispositivo físico.
            VkDevice device_=VK_NULL_HANDLE; // Dispositivo lógico do Vulkan.
            uint32_t graphicsFamily_=0,presentFamily_=0; // Índices das famílias de filas de gráficos e apresentação.
            VkQueue graphicsQueue_=VK_NULL_HANDLE,presentQueue_=VK_NULL_HANDLE; // Filas de gráficos e apresentação.
            bool validationEnabled_=false,wireframeEnabled_=false,anisotropyEnabled_=false,resized_=false; // Flags de estado do VulkanRenderer.
            std::atomic<int> validationErrors_{0}; // Contador de erros de validação.
            VkSwapchainKHR swapchain_=VK_NULL_HANDLE; // Swapchain do Vulkan.
            VkFormat swapFormat_=VK_FORMAT_UNDEFINED,depthFormat_=VK_FORMAT_D32_SFLOAT; // Formatos do swapchain e do buffer de profundidade.
            VkExtent2D extent_{}; // Dimensões do swapchain.
            bool captureSupported_=false; // Indica se a captura de imagem é suportada.
            std::vector<VkImage> swapImages_; // Imagens do swapchain.
            std::vector<VkImageView> swapViews_; // Visualizações das imagens do swapchain.
            std::vector<Image> depthImages_; // Imagens de profundidade.
            std::vector<VkFramebuffer> framebuffers_; // Framebuffers do swapchain.
            // Um semaphore de apresentacao POR IMAGEM; nao reutilizar por indice de frame.
            std::vector<VkSemaphore> renderFinished_; // Semaphores de finalização de renderização.
            std::vector<VkFence> imageFences_; // Fences para sincronização de imagens.
            VkRenderPass renderPass_=VK_NULL_HANDLE; // Render pass do Vulkan.
            VkDescriptorSetLayout frameLayout_=VK_NULL_HANDLE,textureLayout_=VK_NULL_HANDLE; // Layouts de conjuntos de descritores.
            VkDescriptorPool descriptorPool_=VK_NULL_HANDLE; // Pool de descritores.
            VkDescriptorSet textureSet_=VK_NULL_HANDLE; // Conjunto de descritores de textura.
            VkPipelineLayout pipelineLayout_=VK_NULL_HANDLE; // Layout do pipeline.
            VkPipeline solid_=VK_NULL_HANDLE,wire_=VK_NULL_HANDLE,transparent_=VK_NULL_HANDLE; // Pipelines do Vulkan.
            VkCommandPool commandPool_=VK_NULL_HANDLE; // Pool de comandos.
            std::array<Frame,framesInFlight> frames_{}; // Estruturas de quadros em voo.
            size_t currentFrame_=0; // Índice do quadro atual.
            Buffer vertices_,indices_,captureBuffer_; // Buffers de vértices, índices e captura.
            Image heightmap_,albedo_,abramsTexture_; // Imagens de altura, albedo e textura Abrams.
            VkSampler heightSampler_=VK_NULL_HANDLE,albedoSampler_=VK_NULL_HANDLE,abramsSampler_=VK_NULL_HANDLE; // Samplers para as imagens.

            void createInstance(); // Cria a instância do Vulkan.
            void selectDevice(); // Seleciona o dispositivo físico.
            void createDevice(); // Cria o dispositivo lógico.
            void createSwapchain(); // Cria o swapchain.
            void createRenderPass(); // Cria o render pass.
            void createFramebuffers(); // Cria os framebuffers.
            void destroySwapchain(); // Destroi o swapchain.
            bool recreateSwapchain(); // Recria o swapchain.
            void createCommandResources(); // Cria os recursos de comando.
            void createGeometry(const Geometry& geometry); // Cria a geometria.
            void createTextures(const Simulation& simulation); // Cria as texturas.
            void uploadTexture(Image& image,const std::vector<uint8_t>& bytes,int size,bool existing,bool srgb); // Faz upload da textura.
            void createDescriptors(); // Cria os descritores.
            void createPipelines(); // Cria os pipelines.
            void updateUniform(Frame& frame,const Simulation& simulation); // Atualiza os uniformes.
            void record(Frame& frame,uint32_t image,const Simulation& simulation,bool capture); // Grava os comandos de renderização.
            void saveCapture(); // Salva a captura.
            uint32_t memoryType(uint32_t mask,VkMemoryPropertyFlags flags); // Retorna o tipo de memória.
            Buffer createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,bool map=false); // Cria um buffer.
            Image createImage(uint32_t w,uint32_t h,uint32_t levels,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect); // Cria uma imagem.
            VkImageView imageView(VkImage image,VkFormat format,uint32_t levels,VkImageAspectFlags aspect); // Cria uma view de imagem.
            void destroyBuffer(Buffer& buffer); // Destroi um buffer.
            void destroyImage(Image& image); // Destroi uma imagem.
            VkCommandBuffer beginUpload(); // Inicia o upload de comandos.
            void endUpload(VkCommandBuffer commands); // Finaliza o upload de comandos.
            VkShaderModule shaderModule(const char* name); // Cria um módulo de shader.
            // Callback de debug do Vulkan. (comando extenso hein)
            static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user);
    };
}
