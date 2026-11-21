#pragma once
#include <GLFW/glfw3.h>
#include "Simulation.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace simulator {
inline void vkCheck(VkResult result, const char* operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" falhou, VkResult="+std::to_string(result));
}
struct Buffer { VkBuffer handle=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkDeviceSize size=0; void* mapped=nullptr; };
struct Image { VkImage handle=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; uint32_t levels=1; };
struct Frame {
    VkCommandBuffer commands=VK_NULL_HANDLE;
    VkSemaphore acquired=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    Buffer uniform;
    VkDescriptorSet set=VK_NULL_HANDLE;
};
// std140: duas mat4 + cinco vec4 = 208 bytes, iguais ao bloco dos quatro shaders.
struct alignas(16) FrameUBO {
    glm::mat4 model{1}, viewProj{1};
    glm::vec4 cameraPos{}, lodParams{}, material{}, lightDir{}, lightColor{};
};
static_assert(sizeof(FrameUBO)==208,"FrameUBO deve corresponder ao std140 GLSL");
// std430 / push constants: mat4 (64) + duas vec4 (32) = 96 bytes; limite minimo Vulkan=128.
struct alignas(16) DrawPush { glm::mat4 objectModel{1}; glm::vec4 parameters{}, tint{1}; };
static_assert(sizeof(DrawPush)==96,"Layout incorreto das push constants");
struct Options { bool validation=true, requireValidation=false, smoke=false; int frames=0; std::filesystem::path shaderDir; std::filesystem::path capture; };

class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* window, Options options);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&)=delete;
    VulkanRenderer& operator=(const VulkanRenderer&)=delete;
    void initialize(const Simulation& simulation);
    bool draw(Simulation& simulation, bool capture=false);
    void resize() { resized_=true; }
    bool wireframeSupported() const { return wireframeEnabled_; }
    uint32_t maxTessellation() const { return properties_.limits.maxTessellationGenerationLevel; }
    const char* gpuName() const { return properties_.deviceName; }
    int validationErrors() const { return validationErrors_.load(); }
    void shutdown();
private:
    static constexpr size_t framesInFlight=2;
    GLFWwindow* window_;
    Options options_;
    VkInstance instance_=VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_=VK_NULL_HANDLE;
    VkSurfaceKHR surface_=VK_NULL_HANDLE;
    VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    VkDevice device_=VK_NULL_HANDLE;
    uint32_t graphicsFamily_=0,presentFamily_=0;
    VkQueue graphicsQueue_=VK_NULL_HANDLE,presentQueue_=VK_NULL_HANDLE;
    bool validationEnabled_=false,wireframeEnabled_=false,anisotropyEnabled_=false,resized_=false;
    std::atomic<int> validationErrors_{0};
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    VkFormat swapFormat_=VK_FORMAT_UNDEFINED,depthFormat_=VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent_{};
    bool captureSupported_=false;
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<Image> depthImages_;
    std::vector<VkFramebuffer> framebuffers_;
    // Um semaphore de apresentacao POR IMAGEM; nao reutilizar por indice de frame.
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> imageFences_;
    VkRenderPass renderPass_=VK_NULL_HANDLE;
    VkDescriptorSetLayout frameLayout_=VK_NULL_HANDLE,textureLayout_=VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_=VK_NULL_HANDLE;
    VkDescriptorSet textureSet_=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_=VK_NULL_HANDLE;
    VkPipeline solid_=VK_NULL_HANDLE,wire_=VK_NULL_HANDLE,transparent_=VK_NULL_HANDLE;
    VkCommandPool commandPool_=VK_NULL_HANDLE;
    std::array<Frame,framesInFlight> frames_{};
    size_t currentFrame_=0;
    Buffer vertices_,indices_,captureBuffer_;
    Image heightmap_,albedo_;
    VkSampler heightSampler_=VK_NULL_HANDLE,albedoSampler_=VK_NULL_HANDLE;

    void createInstance();
    void selectDevice();
    void createDevice();
    void createSwapchain();
    void createRenderPass();
    void createFramebuffers();
    void destroySwapchain();
    bool recreateSwapchain();
    void createCommandResources();
    void createGeometry(const Geometry& geometry);
    void createTextures(const Simulation& simulation);
    void uploadTexture(Image& image,const std::vector<uint8_t>& bytes,int size,bool existing,bool srgb);
    void createDescriptors();
    void createPipelines();
    void updateUniform(Frame& frame,const Simulation& simulation);
    void record(Frame& frame,uint32_t image,const Simulation& simulation,bool capture);
    void saveCapture();
    uint32_t memoryType(uint32_t mask,VkMemoryPropertyFlags flags);
    Buffer createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,bool map=false);
    Image createImage(uint32_t w,uint32_t h,uint32_t levels,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect);
    VkImageView imageView(VkImage image,VkFormat format,uint32_t levels,VkImageAspectFlags aspect);
    void destroyBuffer(Buffer& buffer);
    void destroyImage(Image& image);
    VkCommandBuffer beginUpload();
    void endUpload(VkCommandBuffer commands);
    VkShaderModule shaderModule(const char* name);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user);
};
}
