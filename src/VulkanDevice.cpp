#include "VulkanRenderer.hpp" // Inclui a definição do VulkanRenderer e suas dependências.
#include <algorithm> // Para funções de algoritmo como std::any_of.
#include <cstring> // Para funções de manipulação de strings como std::strcmp.
#include <iostream> // Para entrada e saída padrão, como std::cerr.
#include <set> // Para estruturas de dados do tipo conjunto.

namespace simulator {
    // Construtor e destrutor do VulkanRenderer.
    // Parâmetros:
    // - window: ponteiro para a janela GLFW.
    // - options: opções de configuração do VulkanRenderer.
    // window_: ponteiro para a janela GLFW utilizada pelo VulkanRenderer.
    // options_: opções de configuração do VulkanRenderer.
    VulkanRenderer::VulkanRenderer(GLFWwindow* window,Options options):window_(window),options_(std::move(options)) {}
    // Destrutor do VulkanRenderer.
    // Chama a função shutdown() para liberar os recursos do Vulkan.
    VulkanRenderer::~VulkanRenderer() { shutdown(); }
    // Função de callback para mensagens de debug do Vulkan.
    // É chamada pelo Vulkan quando uma mensagem de debug é gerada.
    // Pode ser usada para registrar mensagens de validação e erros.
    // Parâmetros:
    // Parâmetros:
    // - severity: severidade da mensagem de debug.
    // - type: tipo da mensagem de debug.
    // - data: dados da mensagem de debug.
    // - user: ponteiro para o usuário, neste caso o VulkanRenderer.
    // Retorna VK_FALSE para continuar a execução.
    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user) {
        auto* renderer=static_cast<VulkanRenderer*>(user); // Converte o ponteiro do usuário para VulkanRenderer.
        if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++renderer->validationErrors_; // Incrementa o contador de erros de validação se a severidade for erro.
        std::cerr<<"[Vulkan validation] "<<data->pMessage<<'\n'; // Imprime a mensagem de validação do Vulkan no stderr.
        return VK_FALSE; // Retorna VK_FALSE para continuar a execução.
    }
    // Cria a instância do Vulkan.
    // Lança uma exceção se a camada de validação requerida não estiver disponível.
    // Configura as extensões necessárias do GLFW e a extensão de debug se a validação estiver habilitada.
    void VulkanRenderer::createInstance() {
        // Contador de camadas disponíveis no Vulkan.
        uint32_t layerCount=0; 
        // Obtém o número de camadas disponíveis.
        vkCheck(vkEnumerateInstanceLayerProperties(&layerCount,nullptr),"enumerar camadas"); 
        // Vetor para armazenar as propriedades das camadas.
        std::vector<VkLayerProperties> layers(layerCount); 
        // Lê as propriedades das camadas disponíveis.
        vkCheck(vkEnumerateInstanceLayerProperties(&layerCount,layers.data()),"ler camadas");
        // Nome da camada de validação requerida.
        const char* validation="VK_LAYER_KHRONOS_validation"; 
        // Verifica se a camada de validação está disponível.
        bool available=std::any_of(layers.begin(),layers.end(),[&](auto& l){return std::strcmp(l.layerName,validation)==0;});
        // Habilita a validação se a opção estiver ativada e a camada estiver disponível.
        validationEnabled_=options_.validation && available; 
        // Lança exceção se a validação for obrigatória e não estiver disponível.
        if(options_.requireValidation && !available) throw std::runtime_error("Camada VK_LAYER_KHRONOS_validation ausente. Instale o Vulkan SDK/validation layers."); // Lança exceção se a validação for obrigatória e não estiver disponível.
        // Emite um aviso se a validação estiver habilitada mas não disponível.
        if(options_.validation && !available) std::cerr<<"Validacao indisponivel; continuando. Use --require-validation para exigir a camada.\n"; // Aviso se a validação estiver habilitada mas não disponível.
        // Obtém as extensões necessárias do GLFW.
        uint32_t count=0;
        const char** required=glfwGetRequiredInstanceExtensions(&count);
        // Lança exceção se o GLFW não fornecer extensões Vulkan.
        if(!required || !count) throw std::runtime_error("GLFW nao encontrou extensoes Vulkan para esta janela.");
        // Adiciona a extensão de debug se a validação estiver habilitada.
        std::vector<const char*> extensions(required,required+count);
        // Constrói a lista final de extensões a serem habilitadas na instância Vulkan.
        if(validationEnabled_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        // Configura as informações da aplicação Vulkan.
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        // Define o nome da aplicação e a versão.
        app.pApplicationName="Eruption - Simulador 3D / Tessellation"; app.applicationVersion=VK_MAKE_VERSION(1,0,0);
        // Define o nome do motor e a versão.
        app.pEngineName="Eruption academic simulator"; app.engineVersion=VK_MAKE_VERSION(1,0,0); app.apiVersion=VK_API_VERSION_1_3;
        // Configura as informações do messenger de debug.
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        // Define a severidade e os tipos de mensagens que o messenger irá capturar.
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        // Define os tipos de mensagens que o messenger irá capturar.
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        // Define o callback e os dados do usuário para o messenger de debug.
        debug.pfnUserCallback=debugCallback; debug.pUserData=this;
        // Configura as informações de criação da instância Vulkan.
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        // Inicializa a estrutura de criação da instância com as informações padrão.
        info.pApplicationInfo=&app; info.enabledExtensionCount=uint32_t(extensions.size()); info.ppEnabledExtensionNames=extensions.data();
        // Se a validação estiver habilitada, configura as camadas e o ponteiro para o debug messenger.
        if(validationEnabled_) { info.enabledLayerCount=1; info.ppEnabledLayerNames=&validation; info.pNext=&debug; }
        // Cria a instância Vulkan com as configurações definidas.
        vkCheck(vkCreateInstance(&info,nullptr,&instance_),"criar instancia Vulkan 1.3");
        // Se a validação estiver habilitada, cria o messenger de debug.
        if(validationEnabled_) {
            // Obtém o ponteiro para a função de criação do messenger de debug.
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkCreateDebugUtilsMessengerEXT"));
            // Lança uma exceção se a função não estiver disponível.
            if(!create) throw std::runtime_error("VK_EXT_debug_utils indisponivel");
            // Cria o messenger de debug com as configurações definidas.
            vkCheck(create(instance_,&debug,nullptr,&messenger_),"criar debug messenger");
        }
        // A superficie depende da plataforma; GLFW fornece Win32/X11/Wayland sem OpenGL.
        // Cria a superficie de apresentação para a janela GLFW.
        vkCheck(glfwCreateWindowSurface(instance_,window_,nullptr,&surface_),"criar superficie de apresentacao");
    }
    // Seleciona a GPU física mais adequada para o renderer Vulkan.
    void VulkanRenderer::selectDevice() {
        // Obtém a contagem de GPUs físicas disponíveis.
        uint32_t count=0;
        vkCheck(vkEnumeratePhysicalDevices(instance_,&count,nullptr),"enumerar GPUs");
        // Obtém a lista de GPUs físicas disponíveis.
        std::vector<VkPhysicalDevice> devices(count);
        // Lê as GPUs físicas disponíveis.
        vkCheck(vkEnumeratePhysicalDevices(instance_,&count,devices.data()),"ler GPUs");
        // Inicializa a variável para armazenar o índice da melhor GPU encontrada.
        int best=-1;
        // Itera sobre cada GPU candidata para avaliar sua compatibilidade e pontuação.
        for(auto candidate:devices) {
            // Obtém as propriedades e recursos da GPU candidata.
            VkPhysicalDeviceProperties props{}; VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceProperties(candidate,&props); vkGetPhysicalDeviceFeatures(candidate,&features);
            // Verifica se a GPU suporta a versão mínima do Vulkan e o shader de tesselação.
            if(props.apiVersion<VK_API_VERSION_1_3 || !features.tessellationShader) continue;
            // Verifica se a GPU suporta a extensão de swapchain.
            uint32_t extCount=0; vkCheck(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extCount,nullptr),"enumerar extensoes GPU");
            // Lê as extensões suportadas pela GPU candidata.
            std::vector<VkExtensionProperties> exts(extCount);
            // Verifica se a extensão de swapchain está presente entre as extensões suportadas.
            vkCheck(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extCount,exts.data()),"ler extensoes GPU");
            // Obtém as propriedades das filas de comando da GPU candidata.
            if(!std::any_of(exts.begin(),exts.end(),[](auto& e){return std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0;})) continue;
            // Procura por filas de comando que suportem gráficos e apresentação.
            uint32_t families=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            // Lê as propriedades das filas de comando da GPU candidata.
            std::vector<VkQueueFamilyProperties> queues(families); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            // Procura pelos índices das filas de comando que suportam gráficos e apresentação.
            std::optional<uint32_t> graphic,present;
            // Itera sobre as filas de comando para encontrar aquelas que suportam gráficos e apresentação.
            for(uint32_t i=0;i<families;++i) {
                // Verifica se a fila de comando atual suporta apresentação na superfície.
                VkBool32 supported=VK_FALSE; vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface_,&supported),"consultar fila de apresentacao");
                // Marca a fila de comando como gráfica se suportar gráficos e tiver contagem de filas.
                if(queues[i].queueCount && (queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)) graphic=i;
                // Marca a fila de comando como de apresentação se suportar apresentação na superfície.
                if(queues[i].queueCount && supported) present=i;
                // Se ambas as filas de comando gráfica e de apresentação forem encontradas e forem iguais, interrompe a busca.
                if(graphic && present && *graphic==*present) break;
            }
            // Obtém os formatos e modos de apresentação suportados pela superfície.
            uint32_t formats=0,modes=0;
            vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate,surface_,&formats,nullptr),"consultar formatos");
            // Consulta os modos de apresentação suportados pela superfície.
            vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate,surface_,&modes,nullptr),"consultar modos");
            // Se não houver filas de comando gráfica ou de apresentação, ou se não houver formatos ou modos suportados, pula para a próxima GPU candidata.
            if(!graphic || !present || !formats || !modes) continue;
            // Calcula a pontuação da GPU candidata com base no tipo de dispositivo.
            int score=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?1000:props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?500:100;
            // Se a pontuação for melhor que a melhor encontrada até agora, atualiza as informações da GPU selecionada.
            if(score>best) { best=score; physical_=candidate; properties_=props; graphicsFamily_=*graphic; presentFamily_=*present; wireframeEnabled_=features.fillModeNonSolid; anisotropyEnabled_=features.samplerAnisotropy; }
        }
        // Se nenhuma GPU compatível foi encontrada, lança uma exceção.
        if(!physical_) throw std::runtime_error("Nenhuma GPU compativel: requer Vulkan 1.3, tessellationShader e apresentacao nesta janela.");
        // Procura por um formato de profundidade compatível.
        bool depthFound=false;
        // Itera sobre os formatos de profundidade possíveis e verifica se são suportados pela GPU.
        for(auto format:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM}) {
            // Obtém as propriedades do formato de profundidade atual.
            VkFormatProperties p{}; vkGetPhysicalDeviceFormatProperties(physical_,format,&p);
            // Verifica se o formato de profundidade suporta anexos de profundidade e stencil com tiling ótimo.
            if(p.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {depthFormat_=format; depthFound=true; break;}
        }
        // Se nenhum formato de profundidade compatível foi encontrado, lança uma exceção.
        if(!depthFound) throw std::runtime_error("GPU sem formato de profundidade compativel.");
        // Verifica se a GPU suporta os formatos de textura necessários para o simulador.
        for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB}) {
            // Obtém as propriedades do formato de textura atual.
            VkFormatProperties p{}; vkGetPhysicalDeviceFormatProperties(physical_,format,&p);
            // Define os recursos necessários para o formato de textura.
            VkFormatFeatureFlags needed=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            // Verifica se o formato de textura suporta os recursos necessários.
            if((p.optimalTilingFeatures&needed)!=needed) throw std::runtime_error("GPU sem suporte aos formatos de textura do simulador.");
        }
        // Exibe informações sobre a GPU selecionada e suas filas de comando.
        std::cout<<"GPU: "<<properties_.deviceName<<" | filas graphics="<<graphicsFamily_<<" present="<<presentFamily_<<" | tess max="<<properties_.limits.maxTessellationGenerationLevel<<'\n';
    }
    // Cria o dispositivo lógico e obtém as filas de comando necessárias.
    void VulkanRenderer::createDevice() {
        // Define a prioridade das filas de comando e cria as informações das filas.
        float priority=1.0f;
        // Cria um conjunto com as famílias de filas necessárias (gráficos e apresentação).
        std::set<uint32_t> families{graphicsFamily_,presentFamily_};
        // Cria um vetor para armazenar as informações das filas de comando.
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        // Para cada família de filas necessária, cria as informações da fila de comando correspondente.
        for(auto family:families) { 
            // Cria as informações da fila de comando para cada família de filas necessária.
            VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex=family; q.queueCount=1; q.pQueuePriorities=&priority; queueInfos.push_back(q); 
        }
        // Define os recursos e extensões do dispositivo lógico.
        VkPhysicalDeviceFeatures features{};
        // Habilita os recursos necessários para o dispositivo lógico.
        features.tessellationShader=VK_TRUE; features.fillModeNonSolid=wireframeEnabled_; features.samplerAnisotropy=anisotropyEnabled_;
        // Define as extensões necessárias para o dispositivo lógico.
        const char* extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        // Cria a estrutura de criação do dispositivo lógico.
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        // Preenche as informações da criação do dispositivo lógico com as filas, extensões e recursos definidos.
        info.queueCreateInfoCount=uint32_t(queueInfos.size()); info.pQueueCreateInfos=queueInfos.data();
        // Define as extensões e recursos habilitados para o dispositivo lógico.
        info.enabledExtensionCount=1; info.ppEnabledExtensionNames=extensions; info.pEnabledFeatures=&features;
        // Cria o dispositivo lógico com as informações definidas.
        vkCheck(vkCreateDevice(physical_,&info,nullptr,&device_),"criar dispositivo logico");
        // Obtém as filas de comando para gráficos e apresentação.
        vkGetDeviceQueue(device_,graphicsFamily_,0,&graphicsQueue_); vkGetDeviceQueue(device_,presentFamily_,0,&presentQueue_);
    }
    // Inicializa o renderizador Vulkan com os recursos e configurações necessários.
    void VulkanRenderer::initialize(const Simulation& simulation) {
        // Cria a instância Vulkan, seleciona o dispositivo físico e cria o dispositivo lógico.
        createInstance(); selectDevice(); createDevice(); createCommandResources();
        // Cria a geometria, texturas e descritores necessários para a simulação.
        createGeometry(simulation.geometry); createTextures(simulation); createDescriptors();
        // Cria a swapchain, render pass, framebuffers e pipelines.
        createSwapchain(); createRenderPass(); createFramebuffers(); createPipelines();
        // Informa que o pipeline ativo inclui os estágios de vertex, tessellation control, tessellation evaluation e fragment.
        std::cout<<"Pipeline ativo: Vertex -> Tessellation Control -> Tessellation Evaluation -> Fragment.\n";
    }
    // Encerra o renderizador Vulkan e libera todos os recursos alocados.
    void VulkanRenderer::shutdown() {
        // Aguarda a GPU finalizar todas as operações antes de destruir os recursos.
        if(device_) {
            // Destrói os recursos alocados pelo renderizador Vulkan.
            vkDeviceWaitIdle(device_);
            // Destrói a swapchain e os recursos associados.
            destroySwapchain();
            // Se houver layout de pipeline, destrói-o.
            if(pipelineLayout_) vkDestroyPipelineLayout(device_,pipelineLayout_,nullptr);
            // Se houver pool de descritores, destrói-o.
            if(descriptorPool_) vkDestroyDescriptorPool(device_,descriptorPool_,nullptr);
            // Se houver layouts de descritor, destrói-os.
            if(frameLayout_) vkDestroyDescriptorSetLayout(device_,frameLayout_,nullptr);
            // Se houver layout de textura, destrói-o.
            if(textureLayout_) vkDestroyDescriptorSetLayout(device_,textureLayout_,nullptr);
            // Se houver samplers, destrói-os.
            if(heightSampler_) vkDestroySampler(device_,heightSampler_,nullptr);
            // Se houver sampler de altura, destrói-o.
            if(albedoSampler_) vkDestroySampler(device_,albedoSampler_,nullptr);
            // Se houver sampler de Abrams, destrói-o.
            if(abramsSampler_) vkDestroySampler(device_,abramsSampler_,nullptr);
            // Se houver sampler de albedo, destrói-o.
            destroyImage(heightmap_); destroyImage(albedo_); destroyImage(abramsTexture_); destroyBuffer(vertices_); destroyBuffer(indices_);
            // Para cada frame, destrói os buffers e sincronizadores associados.
            for(auto& f:frames_) { 
                // Destrói o buffer uniforme do frame.
                destroyBuffer(f.uniform); 
                // Se existir semaphore adquirido, destrói-o.
                if(f.acquired) vkDestroySemaphore(device_,f.acquired,nullptr); 
                // Se existir fence, destrói-o.
                if(f.fence) vkDestroyFence(device_,f.fence,nullptr); 
            }
            // Se houver pool de comandos, destrói-o.
            if(commandPool_) vkDestroyCommandPool(device_,commandPool_,nullptr);
            // Destrói o dispositivo Vulkan.
            vkDestroyDevice(device_,nullptr); device_=VK_NULL_HANDLE;
        }
        // Se houver superfície, destrói-a.
        if(surface_) {vkDestroySurfaceKHR(instance_,surface_,nullptr); surface_=VK_NULL_HANDLE;}
        // Se houver messenger de debug, destrói-o.
        if(messenger_) { auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkDestroyDebugUtilsMessengerEXT")); if(destroy) destroy(instance_,messenger_,nullptr); messenger_=VK_NULL_HANDLE; }
        // Se houver instância Vulkan, destrói-a.
        if(instance_) {vkDestroyInstance(instance_,nullptr); instance_=VK_NULL_HANDLE;}
    }
}
