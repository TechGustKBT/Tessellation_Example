#include "VulkanRenderer.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>

namespace simulator {
VulkanRenderer::VulkanRenderer(GLFWwindow* window,Options options):window_(window),options_(std::move(options)) {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user) {
    auto* renderer=static_cast<VulkanRenderer*>(user);
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++renderer->validationErrors_;
    std::cerr<<"[Vulkan validation] "<<data->pMessage<<'\n';
    return VK_FALSE;
}
void VulkanRenderer::createInstance() {
    uint32_t layerCount=0;
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount,nullptr),"enumerar camadas");
    std::vector<VkLayerProperties> layers(layerCount);
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount,layers.data()),"ler camadas");
    const char* validation="VK_LAYER_KHRONOS_validation";
    bool available=std::any_of(layers.begin(),layers.end(),[&](auto& l){return std::strcmp(l.layerName,validation)==0;});
    validationEnabled_=options_.validation && available;
    if(options_.requireValidation && !available) throw std::runtime_error("Camada VK_LAYER_KHRONOS_validation ausente. Instale o Vulkan SDK/validation layers.");
    if(options_.validation && !available) std::cerr<<"Validacao indisponivel; continuando. Use --require-validation para exigir a camada.\n";
    uint32_t count=0;
    const char** required=glfwGetRequiredInstanceExtensions(&count);
    if(!required || !count) throw std::runtime_error("GLFW nao encontrou extensoes Vulkan para esta janela.");
    std::vector<const char*> extensions(required,required+count);
    if(validationEnabled_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="Eruption - Simulador 3D / Tessellation"; app.applicationVersion=VK_MAKE_VERSION(1,0,0);
    app.pEngineName="Eruption academic simulator"; app.engineVersion=VK_MAKE_VERSION(1,0,0); app.apiVersion=VK_API_VERSION_1_3;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback=debugCallback; debug.pUserData=this;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo=&app; info.enabledExtensionCount=uint32_t(extensions.size()); info.ppEnabledExtensionNames=extensions.data();
    if(validationEnabled_) { info.enabledLayerCount=1; info.ppEnabledLayerNames=&validation; info.pNext=&debug; }
    vkCheck(vkCreateInstance(&info,nullptr,&instance_),"criar instancia Vulkan 1.3");
    if(validationEnabled_) {
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkCreateDebugUtilsMessengerEXT"));
        if(!create) throw std::runtime_error("VK_EXT_debug_utils indisponivel");
        vkCheck(create(instance_,&debug,nullptr,&messenger_),"criar debug messenger");
    }
    // A superficie depende da plataforma; GLFW fornece Win32/X11/Wayland sem OpenGL.
    vkCheck(glfwCreateWindowSurface(instance_,window_,nullptr,&surface_),"criar superficie de apresentacao");
}
void VulkanRenderer::selectDevice() {
    uint32_t count=0;
    vkCheck(vkEnumeratePhysicalDevices(instance_,&count,nullptr),"enumerar GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    vkCheck(vkEnumeratePhysicalDevices(instance_,&count,devices.data()),"ler GPUs");
    int best=-1;
    for(auto candidate:devices) {
        VkPhysicalDeviceProperties props{}; VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceProperties(candidate,&props); vkGetPhysicalDeviceFeatures(candidate,&features);
        if(props.apiVersion<VK_API_VERSION_1_3 || !features.tessellationShader) continue;
        uint32_t extCount=0; vkCheck(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extCount,nullptr),"enumerar extensoes GPU");
        std::vector<VkExtensionProperties> exts(extCount);
        vkCheck(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extCount,exts.data()),"ler extensoes GPU");
        if(!std::any_of(exts.begin(),exts.end(),[](auto& e){return std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0;})) continue;
        uint32_t families=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
        std::optional<uint32_t> graphic,present;
        for(uint32_t i=0;i<families;++i) {
            VkBool32 supported=VK_FALSE; vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface_,&supported),"consultar fila de apresentacao");
            if(queues[i].queueCount && (queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)) graphic=i;
            if(queues[i].queueCount && supported) present=i;
            if(graphic && present && *graphic==*present) break;
        }
        uint32_t formats=0,modes=0;
        vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate,surface_,&formats,nullptr),"consultar formatos");
        vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate,surface_,&modes,nullptr),"consultar modos");
        if(!graphic || !present || !formats || !modes) continue;
        int score=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?1000:props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?500:100;
        if(score>best) { best=score; physical_=candidate; properties_=props; graphicsFamily_=*graphic; presentFamily_=*present; wireframeEnabled_=features.fillModeNonSolid; anisotropyEnabled_=features.samplerAnisotropy; }
    }
    if(!physical_) throw std::runtime_error("Nenhuma GPU compativel: requer Vulkan 1.3, tessellationShader e apresentacao nesta janela.");
    bool depthFound=false;
    for(auto format:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM}) {
        VkFormatProperties p{}; vkGetPhysicalDeviceFormatProperties(physical_,format,&p);
        if(p.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {depthFormat_=format; depthFound=true; break;}
    }
    if(!depthFound) throw std::runtime_error("GPU sem formato de profundidade compativel.");
    for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB}) {
        VkFormatProperties p{}; vkGetPhysicalDeviceFormatProperties(physical_,format,&p);
        VkFormatFeatureFlags needed=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if((p.optimalTilingFeatures&needed)!=needed) throw std::runtime_error("GPU sem suporte aos formatos de textura do simulador.");
    }
    std::cout<<"GPU: "<<properties_.deviceName<<" | filas graphics="<<graphicsFamily_<<" present="<<presentFamily_<<" | tess max="<<properties_.limits.maxTessellationGenerationLevel<<'\n';
}
void VulkanRenderer::createDevice() {
    float priority=1.0f;
    std::set<uint32_t> families{graphicsFamily_,presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for(auto family:families) { VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex=family; q.queueCount=1; q.pQueuePriorities=&priority; queueInfos.push_back(q); }
    VkPhysicalDeviceFeatures features{};
    features.tessellationShader=VK_TRUE; features.fillModeNonSolid=wireframeEnabled_; features.samplerAnisotropy=anisotropyEnabled_;
    const char* extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount=uint32_t(queueInfos.size()); info.pQueueCreateInfos=queueInfos.data();
    info.enabledExtensionCount=1; info.ppEnabledExtensionNames=extensions; info.pEnabledFeatures=&features;
    vkCheck(vkCreateDevice(physical_,&info,nullptr,&device_),"criar dispositivo logico");
    vkGetDeviceQueue(device_,graphicsFamily_,0,&graphicsQueue_); vkGetDeviceQueue(device_,presentFamily_,0,&presentQueue_);
}
void VulkanRenderer::initialize(const Simulation& simulation) {
    createInstance(); selectDevice(); createDevice(); createCommandResources();
    createGeometry(simulation.geometry); createTextures(simulation); createDescriptors();
    createSwapchain(); createRenderPass(); createFramebuffers(); createPipelines();
    std::cout<<"Pipeline ativo: Vertex -> Tessellation Control -> Tessellation Evaluation -> Fragment.\n";
}
void VulkanRenderer::shutdown() {
    if(device_) {
        vkDeviceWaitIdle(device_); // Nenhum recurso e destruido enquanto a GPU o utiliza.
        destroySwapchain();
        if(pipelineLayout_) vkDestroyPipelineLayout(device_,pipelineLayout_,nullptr);
        if(descriptorPool_) vkDestroyDescriptorPool(device_,descriptorPool_,nullptr);
        if(frameLayout_) vkDestroyDescriptorSetLayout(device_,frameLayout_,nullptr);
        if(textureLayout_) vkDestroyDescriptorSetLayout(device_,textureLayout_,nullptr);
        if(heightSampler_) vkDestroySampler(device_,heightSampler_,nullptr);
        if(albedoSampler_) vkDestroySampler(device_,albedoSampler_,nullptr);
        destroyImage(heightmap_); destroyImage(albedo_); destroyBuffer(vertices_); destroyBuffer(indices_);
        for(auto& f:frames_) { destroyBuffer(f.uniform); if(f.acquired) vkDestroySemaphore(device_,f.acquired,nullptr); if(f.fence) vkDestroyFence(device_,f.fence,nullptr); }
        if(commandPool_) vkDestroyCommandPool(device_,commandPool_,nullptr);
        vkDestroyDevice(device_,nullptr); device_=VK_NULL_HANDLE;
    }
    if(surface_) {vkDestroySurfaceKHR(instance_,surface_,nullptr); surface_=VK_NULL_HANDLE;}
    if(messenger_) { auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkDestroyDebugUtilsMessengerEXT")); if(destroy) destroy(instance_,messenger_,nullptr); messenger_=VK_NULL_HANDLE; }
    if(instance_) {vkDestroyInstance(instance_,nullptr); instance_=VK_NULL_HANDLE;}
}
}
