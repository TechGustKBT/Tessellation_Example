#include "VulkanRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include "EmbeddedResources.hpp"

namespace simulator {
uint32_t VulkanRenderer::memoryType(uint32_t mask,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(physical_,&properties);
    for(uint32_t i=0;i<properties.memoryTypeCount;++i) if((mask&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    throw std::runtime_error("Tipo de memoria Vulkan nao encontrado.");
}
Buffer VulkanRenderer::createBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,bool map) {
    Buffer b; b.size=size;
    try {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=size; info.usage=usage; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateBuffer(device_,&info,nullptr,&b.handle),"criar buffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device_,b.handle,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=requirements.size; alloc.memoryTypeIndex=memoryType(requirements.memoryTypeBits,flags);
        vkCheck(vkAllocateMemory(device_,&alloc,nullptr,&b.memory),"alocar memoria de buffer");
        vkCheck(vkBindBufferMemory(device_,b.handle,b.memory,0),"associar memoria de buffer");
        if(map) vkCheck(vkMapMemory(device_,b.memory,0,size,0,&b.mapped),"mapear buffer");
    } catch(...) { destroyBuffer(b); throw; }
    return b;
}
VkImageView VulkanRenderer::imageView(VkImage image,VkFormat format,uint32_t levels,VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; info.image=image; info.viewType=VK_IMAGE_VIEW_TYPE_2D; info.format=format;
    info.subresourceRange={aspect,0,levels,0,1};
    VkImageView view=VK_NULL_HANDLE; vkCheck(vkCreateImageView(device_,&info,nullptr,&view),"criar image view"); return view;
}
Image VulkanRenderer::createImage(uint32_t w,uint32_t h,uint32_t levels,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
    Image image; image.levels=levels;
    try {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D; info.extent={w,h,1}; info.mipLevels=levels; info.arrayLayers=1;
        info.format=format; info.tiling=VK_IMAGE_TILING_OPTIMAL; info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; info.usage=usage; info.samples=VK_SAMPLE_COUNT_1_BIT; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateImage(device_,&info,nullptr,&image.handle),"criar imagem");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device_,image.handle,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=requirements.size; alloc.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkCheck(vkAllocateMemory(device_,&alloc,nullptr,&image.memory),"alocar memoria de imagem");
        vkCheck(vkBindImageMemory(device_,image.handle,image.memory,0),"associar memoria de imagem");
        image.view=imageView(image.handle,format,levels,aspect);
    } catch(...) { destroyImage(image); throw; }
    return image;
}
void VulkanRenderer::destroyBuffer(Buffer& b) {
    if(b.mapped) vkUnmapMemory(device_,b.memory);
    if(b.handle) vkDestroyBuffer(device_,b.handle,nullptr);
    if(b.memory) vkFreeMemory(device_,b.memory,nullptr);
    b={};
}
void VulkanRenderer::destroyImage(Image& image) {
    if(image.view) vkDestroyImageView(device_,image.view,nullptr);
    if(image.handle) vkDestroyImage(device_,image.handle,nullptr);
    if(image.memory) vkFreeMemory(device_,image.memory,nullptr);
    image={};
}
void VulkanRenderer::createCommandResources() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex=graphicsFamily_; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCheck(vkCreateCommandPool(device_,&pool,nullptr,&commandPool_),"criar command pool");
    std::array<VkCommandBuffer,framesInFlight> commands{};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool=commandPool_; alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount=uint32_t(commands.size());
    vkCheck(vkAllocateCommandBuffers(device_,&alloc,commands.data()),"alocar command buffers");
    for(size_t i=0;i<framesInFlight;++i) {
        auto& f=frames_[i]; f.commands=commands[i];
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; vkCheck(vkCreateSemaphore(device_,&sem,nullptr,&f.acquired),"criar semaphore acquire");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        vkCheck(vkCreateFence(device_,&fence,nullptr,&f.fence),"criar fence de frame");
        f.uniform=createBuffer(sizeof(FrameUBO),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
    }
}
VkCommandBuffer VulkanRenderer::beginUpload() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool=commandPool_; alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount=1;
    VkCommandBuffer command=VK_NULL_HANDLE; vkCheck(vkAllocateCommandBuffers(device_,&alloc,&command),"alocar comando de upload");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(command,&begin),"iniciar upload"); return command;
}
void VulkanRenderer::endUpload(VkCommandBuffer command) {
    vkCheck(vkEndCommandBuffer(command),"finalizar upload");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
    vkCheck(vkQueueSubmit(graphicsQueue_,1,&submit,VK_NULL_HANDLE),"submeter upload");
    vkCheck(vkQueueWaitIdle(graphicsQueue_),"aguardar upload");
    vkFreeCommandBuffers(device_,commandPool_,1,&command);
}
void VulkanRenderer::createGeometry(const Geometry& geometry) {
    auto upload=[&](const void* bytes,VkDeviceSize size,VkBufferUsageFlags usage,Buffer& output) {
        Buffer staging=createBuffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
        try {
            std::memcpy(staging.mapped,bytes,size_t(size)); output=createBuffer(size,usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            auto command=beginUpload(); VkBufferCopy copy{0,0,size}; vkCmdCopyBuffer(command,staging.handle,output.handle,1,&copy);
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask=usage==VK_BUFFER_USAGE_VERTEX_BUFFER_BIT?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:VK_ACCESS_INDEX_READ_BIT;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.buffer=output.handle; barrier.size=VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&barrier,0,nullptr); endUpload(command);
        } catch(...) { destroyBuffer(staging); throw; }
        destroyBuffer(staging);
    };
    upload(geometry.vertices.data(),geometry.vertices.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices_);
    upload(geometry.indices.data(),geometry.indices.size()*sizeof(uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices_);
}
void VulkanRenderer::uploadTexture(Image& image,const std::vector<uint8_t>& bytes,int size,bool existing,bool srgb) {
    std::vector<uint8_t> data=bytes,previous=bytes;
    std::vector<VkBufferImageCopy> regions;
    int dimension=size; VkDeviceSize offset=0;
    auto linear=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
    auto encoded=[](float c){return c<=.0031308f?12.92f*c:1.055f*std::pow(c,1.f/2.4f)-.055f;};
    for(uint32_t level=0;level<image.levels;++level) {
        if(level) {
            int next=std::max(1,dimension/2); std::vector<uint8_t> reduced(size_t(next)*next*4);
            for(int y=0;y<next;++y) for(int x=0;x<next;++x) for(int c=0;c<4;++c) {
                float sum=0;
                for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {float v=previous[(std::min(2*y+dy,dimension-1)*dimension+std::min(2*x+dx,dimension-1))*4+c]/255.f; sum+=srgb && c<3?linear(v):v;}
                float v=sum*.25f; if(srgb && c<3) v=encoded(v);
                reduced[(y*next+x)*4+c]=uint8_t(std::clamp(std::lround(v*255),0l,255l));
            }
            previous=std::move(reduced); dimension=next; data.insert(data.end(),previous.begin(),previous.end());
        }
        VkBufferImageCopy region{}; region.bufferOffset=offset; region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1}; region.imageExtent={uint32_t(dimension),uint32_t(dimension),1}; regions.push_back(region);
        offset+=VkDeviceSize(dimension)*dimension*4;
    }
    Buffer staging=createBuffer(data.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
    try {
        std::memcpy(staging.mapped,data.data(),data.size()); auto command=beginUpload();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.image=image.handle; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.levels,0,1};
        barrier.oldLayout=existing?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask=existing?VK_ACCESS_SHADER_READ_BIT:0; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        VkPipelineStageFlags shaderStages=VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        VkPipelineStageFlags sourceStages=existing?shaderStages:VkPipelineStageFlags(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        vkCmdPipelineBarrier(command,sourceStages,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        vkCmdCopyBufferToImage(command,staging.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(regions.size()),regions.data());
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,shaderStages,0,0,nullptr,0,nullptr,1,&barrier); endUpload(command);
    } catch(...) {destroyBuffer(staging); throw;}
    destroyBuffer(staging);
}
void VulkanRenderer::createTextures(const Simulation& simulation) {
    constexpr uint32_t size=Simulation::textureSize;
    uint32_t levels=1; for(uint32_t d=size;d>1;d/=2) ++levels;
    auto usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    heightmap_=createImage(size,size,1,VK_FORMAT_R8G8B8A8_UNORM,usage,VK_IMAGE_ASPECT_COLOR_BIT);
    albedo_=createImage(size,size,levels,VK_FORMAT_R8G8B8A8_SRGB,usage,VK_IMAGE_ASPECT_COLOR_BIT);
    uploadTexture(heightmap_,simulation.heights,size,false,false); uploadTexture(albedo_,simulation.albedo,size,false,true);
    // Textura 2048 x 2048 gerada diretamente das imagens embutidas no GLB.

    constexpr int abramsSize = 2048;

    auto resource = getEmbeddedResource(IDR_ABRAMS_TEX);

    if (resource.size != size_t(abramsSize) * abramsSize * 4)
        throw std::runtime_error("Textura interna do Abrams invalida.");

    std::vector<uint8_t> rgba(
        resource.data,
        resource.data + resource.size
    );

    uint32_t abramsLevels = 1;
    for (int n = abramsSize; n > 1; n /= 2) ++abramsLevels;
    abramsTexture_ = createImage(abramsSize,abramsSize,abramsLevels,
                                  VK_FORMAT_R8G8B8A8_SRGB,usage,VK_IMAGE_ASPECT_COLOR_BIT);
    uploadTexture(abramsTexture_,rgba,abramsSize,false,true);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR; sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.minLod=0; sampler.maxLod=0; sampler.maxAnisotropy=1;
    vkCheck(vkCreateSampler(device_,&sampler,nullptr,&heightSampler_),"criar sampler de altura");
    sampler.maxLod=float(levels-1); sampler.anisotropyEnable=anisotropyEnabled_; sampler.maxAnisotropy=anisotropyEnabled_?std::min(4.f,properties_.limits.maxSamplerAnisotropy):1;
    vkCheck(vkCreateSampler(device_,&sampler,nullptr,&albedoSampler_),"criar sampler trilinear");
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = float(abramsLevels - 1);
    vkCheck(vkCreateSampler(device_,&sampler,nullptr,&abramsSampler_),"criar sampler Abrams");
}
void VulkanRenderer::createDescriptors() {
    constexpr VkShaderStageFlags stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding uniform{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stages,nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=1; layout.pBindings=&uniform;
    vkCheck(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&frameLayout_),"criar layout UBO");
    std::array<VkDescriptorSetLayoutBinding,3> textures={{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}}};
    layout.bindingCount=3; layout.pBindings=textures.data(); vkCheck(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&textureLayout_),"criar layout texturas");
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,uint32_t(framesInFlight)},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=uint32_t(framesInFlight)+1; pool.poolSizeCount=2; pool.pPoolSizes=sizes;
    vkCheck(vkCreateDescriptorPool(device_,&pool,nullptr,&descriptorPool_),"criar descriptor pool");
    for(auto& f:frames_) {
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; alloc.descriptorPool=descriptorPool_; alloc.descriptorSetCount=1; alloc.pSetLayouts=&frameLayout_;
        vkCheck(vkAllocateDescriptorSets(device_,&alloc,&f.set),"alocar descritor de frame");
        VkDescriptorBufferInfo buffer{f.uniform.handle,0,sizeof(FrameUBO)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet=f.set; write.dstBinding=0; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo=&buffer;
        vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    }
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; alloc.descriptorPool=descriptorPool_; alloc.descriptorSetCount=1; alloc.pSetLayouts=&textureLayout_;
    vkCheck(vkAllocateDescriptorSets(device_,&alloc,&textureSet_),"alocar descritor de texturas");
    VkDescriptorImageInfo images[]={{heightSampler_,heightmap_.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{albedoSampler_,albedo_.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{abramsSampler_,abramsTexture_.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    for(uint32_t i=0;i<3;++i) {VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet=textureSet_; write.dstBinding=i; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&images[i]; vkUpdateDescriptorSets(device_,1,&write,0,nullptr);}
    VkDescriptorSetLayout layouts[]={frameLayout_,textureLayout_}; VkPushConstantRange push{stages,0,sizeof(DrawPush)};
    VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipeline.setLayoutCount=2; pipeline.pSetLayouts=layouts; pipeline.pushConstantRangeCount=1; pipeline.pPushConstantRanges=&push;
    vkCheck(vkCreatePipelineLayout(device_,&pipeline,nullptr,&pipelineLayout_),"criar pipeline layout");
}
}
