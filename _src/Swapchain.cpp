#include "VulkanRenderer.hpp"
#include <algorithm>
#include <limits>

namespace simulator {
void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_,surface_,&caps),"consultar superficie");
    uint32_t count=0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,nullptr),"contar formatos");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,formats.data()),"ler formatos");
    auto selected=std::find_if(formats.begin(),formats.end(),[](auto f){return (f.format==VK_FORMAT_B8G8R8A8_SRGB || f.format==VK_FORMAT_R8G8B8A8_SRGB) && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;});
    if(selected==formats.end()) throw std::runtime_error("A superficie nao oferece swapchain RGBA/BGRA SRGB.");
    swapFormat_=selected->format;
    if(caps.currentExtent.width!=std::numeric_limits<uint32_t>::max()) extent_=caps.currentExtent;
    else { int w=0,h=0; glfwGetFramebufferSize(window_,&w,&h); extent_={std::clamp(uint32_t(std::max(w,1)),caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(uint32_t(std::max(h,1)),caps.minImageExtent.height,caps.maxImageExtent.height)}; }
    uint32_t images=caps.minImageCount+1;
    if(caps.maxImageCount) images=std::min(images,caps.maxImageCount);
    captureSupported_=(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;
    if(!options_.capture.empty() && !captureSupported_) throw std::runtime_error("Superficie nao suporta captura por TRANSFER_SRC.");
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for(auto candidate:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) if(caps.supportedCompositeAlpha&candidate) {alpha=candidate; break;}
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface=surface_; info.minImageCount=images; info.imageFormat=swapFormat_; info.imageColorSpace=selected->colorSpace;
    info.imageExtent=extent_; info.imageArrayLayers=1; info.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|(captureSupported_?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    uint32_t families[]={graphicsFamily_,presentFamily_};
    if(graphicsFamily_!=presentFamily_) {info.imageSharingMode=VK_SHARING_MODE_CONCURRENT; info.queueFamilyIndexCount=2; info.pQueueFamilyIndices=families;}
    else info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform=caps.currentTransform; info.compositeAlpha=alpha;
    info.presentMode=VK_PRESENT_MODE_FIFO_KHR; // VSync FIFO e garantido pela especificacao.
    info.clipped=VK_TRUE;
    vkCheck(vkCreateSwapchainKHR(device_,&info,nullptr,&swapchain_),"criar swapchain");
    vkCheck(vkGetSwapchainImagesKHR(device_,swapchain_,&images,nullptr),"contar imagens swapchain");
    swapImages_.resize(images); vkCheck(vkGetSwapchainImagesKHR(device_,swapchain_,&images,swapImages_.data()),"obter imagens swapchain");
    imageFences_.assign(images,VK_NULL_HANDLE);
    for(auto image:swapImages_) {
        swapViews_.push_back(imageView(image,swapFormat_,1,VK_IMAGE_ASPECT_COLOR_BIT));
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore handle=VK_NULL_HANDLE;
        vkCheck(vkCreateSemaphore(device_,&semaphore,nullptr,&handle),"criar semaphore por imagem"); renderFinished_.push_back(handle);
    }
    if(!options_.capture.empty()) captureBuffer_=createBuffer(VkDeviceSize(extent_.width)*extent_.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
}
void VulkanRenderer::createRenderPass() {
    std::array<VkAttachmentDescription,2> attachments{};
    attachments[0].format=swapFormat_; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format=depthFormat_; attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color; subpass.pDepthStencilAttachment=&depth;
    VkSubpassDependency dependency{}; dependency.srcSubpass=VK_SUBPASS_EXTERNAL; dependency.dstSubpass=0;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask=dependency.srcStageMask;
    dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount=uint32_t(attachments.size()); info.pAttachments=attachments.data(); info.subpassCount=1; info.pSubpasses=&subpass; info.dependencyCount=1; info.pDependencies=&dependency;
    vkCheck(vkCreateRenderPass(device_,&info,nullptr,&renderPass_),"criar render pass");
}
void VulkanRenderer::createFramebuffers() {
    for(auto color:swapViews_) {
        // Profundidade separada por imagem permite manter dois frames em voo sem corrida.
        depthImages_.push_back(createImage(extent_.width,extent_.height,1,depthFormat_,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT));
        VkImageView views[]={color,depthImages_.back().view};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; info.renderPass=renderPass_; info.attachmentCount=2; info.pAttachments=views; info.width=extent_.width; info.height=extent_.height; info.layers=1;
        VkFramebuffer framebuffer=VK_NULL_HANDLE; vkCheck(vkCreateFramebuffer(device_,&info,nullptr,&framebuffer),"criar framebuffer"); framebuffers_.push_back(framebuffer);
    }
}
void VulkanRenderer::destroySwapchain() {
    for(auto pipeline:{solid_,wire_,transparent_}) if(pipeline) vkDestroyPipeline(device_,pipeline,nullptr);
    solid_=wire_=transparent_=VK_NULL_HANDLE;
    for(auto fb:framebuffers_) vkDestroyFramebuffer(device_,fb,nullptr);
    framebuffers_.clear();
    for(auto& image:depthImages_) destroyImage(image);
    depthImages_.clear();
    if(renderPass_) vkDestroyRenderPass(device_,renderPass_,nullptr);
    renderPass_=VK_NULL_HANDLE;
    for(auto view:swapViews_) vkDestroyImageView(device_,view,nullptr);
    swapViews_.clear();
    for(auto semaphore:renderFinished_) vkDestroySemaphore(device_,semaphore,nullptr);
    renderFinished_.clear();
    if(swapchain_) vkDestroySwapchainKHR(device_,swapchain_,nullptr);
    swapchain_=VK_NULL_HANDLE;
    swapImages_.clear(); imageFences_.clear(); destroyBuffer(captureBuffer_);
}
bool VulkanRenderer::recreateSwapchain() {
    int w=0,h=0; glfwGetFramebufferSize(window_,&w,&h);
    while(!w || !h) { if(glfwWindowShouldClose(window_)) return false; glfwWaitEvents(); glfwGetFramebufferSize(window_,&w,&h); }
    vkCheck(vkDeviceWaitIdle(device_),"aguardar GPU antes de resize");
    destroySwapchain(); createSwapchain(); createRenderPass(); createFramebuffers(); createPipelines(); resized_=false;
    return true;
}
}
