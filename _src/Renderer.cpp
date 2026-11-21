#include "VulkanRenderer.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace simulator {
void VulkanRenderer::updateUniform(Frame& f,const Simulation& simulation) {
    FrameUBO ubo;
    ubo.viewProj=simulation.viewProjection(float(extent_.width)/extent_.height);
    ubo.cameraPos=glm::vec4(simulation.cameraPosition(),1);
    float level=std::clamp(simulation.maxTess,2.f,float(std::min(64u,maxTessellation())));
    ubo.lodParams={6,60,simulation.adaptive?2.f:level,level};
    ubo.material={Simulation::heightScale,.0001f,.28f,32}; ubo.lightDir={-.5f,1,.35f,0}; ubo.lightColor={1,.94f,.85f,.8f};
    std::memcpy(f.uniform.mapped,&ubo,sizeof(ubo)); // Fence deste frame ja foi aguardada.
}
void VulkanRenderer::record(Frame& f,uint32_t image,const Simulation& simulation,bool capture) {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkCheck(vkBeginCommandBuffer(f.commands,&begin),"iniciar command buffer");
    std::array<VkClearValue,2> clear{}; clear[0].color={{.027f,.045f,.07f,1}}; clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; pass.renderPass=renderPass_; pass.framebuffer=framebuffers_[image]; pass.renderArea={{0,0},extent_}; pass.clearValueCount=uint32_t(clear.size()); pass.pClearValues=clear.data();
    vkCmdBeginRenderPass(f.commands,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,float(extent_.width),float(extent_.height),0,1}; VkRect2D scissor{{0,0},extent_};
    vkCmdSetViewport(f.commands,0,1,&viewport); vkCmdSetScissor(f.commands,0,1,&scissor);
    VkDeviceSize offset=0; vkCmdBindVertexBuffers(f.commands,0,1,&vertices_.handle,&offset); vkCmdBindIndexBuffer(f.commands,indices_.handle,0,VK_INDEX_TYPE_UINT32);
    VkDescriptorSet sets[]={f.set,textureSet_}; vkCmdBindDescriptorSets(f.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout_,0,2,sets,0,nullptr);
    constexpr VkShaderStageFlags stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT|VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    VkPipeline previous=VK_NULL_HANDLE;
    for(const auto& object:simulation.objects()) {
        VkPipeline pipeline=object.transparent?transparent_:(simulation.wireframe && wire_?wire_:solid_);
        if(pipeline!=previous) {vkCmdBindPipeline(f.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); previous=pipeline;}
        DrawPush push; push.objectModel=object.model; push.parameters={object.displacement,object.material,simulation.time,simulation.normals?1.f:0.f}; push.tint=object.tint;
        vkCmdPushConstants(f.commands,pipelineLayout_,stages,0,sizeof(push),&push);
        vkCmdDrawIndexed(f.commands,object.mesh.count,1,object.mesh.firstIndex,0,0);
    }
    vkCmdEndRenderPass(f.commands);
    if(capture && captureSupported_ && captureBuffer_.handle) {
        // Captura opcional da imagem realmente renderizada, antes da apresentacao.
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image=swapImages_[image]; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(f.commands,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={extent_.width,extent_.height,1};
        vkCmdCopyImageToBuffer(f.commands,swapImages_[image],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,captureBuffer_.handle,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(f.commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT; host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; host.buffer=captureBuffer_.handle; host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(f.commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    }
    vkCheck(vkEndCommandBuffer(f.commands),"finalizar command buffer");
}
bool VulkanRenderer::draw(Simulation& simulation,bool capture) {
    if(resized_ && !recreateSwapchain()) return false;
    Frame& frame=frames_[currentFrame_];
    constexpr uint64_t forever=std::numeric_limits<uint64_t>::max();
    vkCheck(vkWaitForFences(device_,1,&frame.fence,VK_TRUE,forever),"aguardar fence do frame");
    if(simulation.dirty) {
        // Atualizacao rara (impacto/reset): espera todos os leitores antes de mudar heightmap.
        // Simples e correto para o laboratorio; streaming assincrono pode substituir depois.
        vkCheck(vkDeviceWaitIdle(device_),"aguardar texturas em uso");
        uploadTexture(heightmap_,simulation.heights,Simulation::textureSize,true,false);
        uploadTexture(albedo_,simulation.albedo,Simulation::textureSize,true,true); simulation.dirty=false;
    }
    uint32_t image=0;
    VkResult acquired=vkAcquireNextImageKHR(device_,swapchain_,forever,frame.acquired,VK_NULL_HANDLE,&image);
    if(acquired==VK_ERROR_OUT_OF_DATE_KHR) {recreateSwapchain(); return false;}
    if(acquired!=VK_SUCCESS && acquired!=VK_SUBOPTIMAL_KHR) vkCheck(acquired,"adquirir imagem");
    if(imageFences_[image]) vkCheck(vkWaitForFences(device_,1,&imageFences_[image],VK_TRUE,forever),"aguardar imagem em uso");
    imageFences_[image]=frame.fence;
    updateUniform(frame,simulation);
    vkCheck(vkResetCommandBuffer(frame.commands,0),"reset command buffer"); record(frame,image,simulation,capture);
    // Reset somente depois de adquirir com sucesso. Evita deadlock ao minimizar/resize.
    vkCheck(vkResetFences(device_,1,&frame.fence),"reset fence");
    VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount=1; submit.pWaitSemaphores=&frame.acquired; submit.pWaitDstStageMask=&waitStage;
    submit.commandBufferCount=1; submit.pCommandBuffers=&frame.commands; submit.signalSemaphoreCount=1; submit.pSignalSemaphores=&renderFinished_[image];
    vkCheck(vkQueueSubmit(graphicsQueue_,1,&submit,frame.fence),"submeter comandos graficos");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount=1; present.pWaitSemaphores=&renderFinished_[image]; present.swapchainCount=1; present.pSwapchains=&swapchain_; present.pImageIndices=&image;
    VkResult result=vkQueuePresentKHR(presentQueue_,&present);
    if(capture) {vkCheck(vkWaitForFences(device_,1,&frame.fence,VK_TRUE,forever),"aguardar captura"); saveCapture();}
    currentFrame_=(currentFrame_+1)%framesInFlight;
    if(result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR || acquired==VK_SUBOPTIMAL_KHR || resized_) recreateSwapchain();
    else vkCheck(result,"apresentar imagem");
    return true;
}
void VulkanRenderer::saveCapture() {
    if(options_.capture.empty() || !captureBuffer_.mapped) return;
    if(options_.capture.has_parent_path()) std::filesystem::create_directories(options_.capture.parent_path());
    std::ofstream file(options_.capture,std::ios::binary); if(!file) throw std::runtime_error("Nao foi possivel salvar captura.");
    file<<"P6\n"<<extent_.width<<' '<<extent_.height<<"\n255\n";
    auto bytes=static_cast<const uint8_t*>(captureBuffer_.mapped); bool bgra=swapFormat_==VK_FORMAT_B8G8R8A8_SRGB;
    for(size_t i=0;i<size_t(extent_.width)*extent_.height;++i) { char rgb[]={char(bytes[i*4+(bgra?2:0)]),char(bytes[i*4+1]),char(bytes[i*4+(bgra?0:2)])}; file.write(rgb,3); }
    if(!file) throw std::runtime_error("Falha ao escrever captura.");
    std::cout<<"Captura: "<<options_.capture.string()<<'\n';
}
}
