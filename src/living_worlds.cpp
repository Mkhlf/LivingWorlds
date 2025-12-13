#include "living_worlds.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <random>
#include <ctime>

#define VK_CHECK(x)                                                 \
    do {                                                            \
        VkResult err = x;                                           \
        if (err) {                                                  \
            std::cerr << "Detected Vulkan error: " << err << "\n";  \
            abort();                                                \
        }                                                           \
    } while (0)

// Change default pattern here for testing
const Pattern DEFAULT_PATTERN = Pattern::GosperGliderGun; 

void LivingWorlds::run() {
    init();
    initialize_grid_pattern(DEFAULT_PATTERN); 
    main_loop();
    cleanup();
}

void LivingWorlds::init() {
    init_window();
    init_vulkan();
    init_swapchain();
    init_commands();
    
    // 2.5D Resources must be created before framebuffers (depth)
    create_grid_mesh();
    create_vertex_buffer();
    create_index_buffer();
    create_uniform_buffers();
    create_depth_resources(); // Init depth resources here

    init_default_renderpass(); // Now uses depth format
    init_framebuffers();       // Now uses depth image view
    init_sync_structures();

    // Compute setup
    init_storage_images();
    init_descriptors();
    
    init_noise_pipeline();
    dispatch_noise_init(); // <-- WAS MISSING
    
    init_biome_pipeline();
    init_erosion_pipeline();
    init_biome_growth_pipeline();
    init_biome_ca_pipeline(); // Week 5.5
    init_terrain_pipeline();
    
    dispatch_biome_init(); // Run once (temp/hum)
    dispatch_biome_ca_init(); // Week 5.5: Initialize discrete biomes
    
    // Viz Pipeline
    init_viz_pipeline();
}

void LivingWorlds::init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Living Worlds", nullptr, nullptr);
    
    // Input Setup
    glfwSetWindowUserPointer(window, this);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouse_callback);
}

void LivingWorlds::init_vulkan() {
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("Living Worlds")
                        .request_validation_layers(true)
                        .require_api_version(1, 3, 0)
                        .use_default_debug_messenger()
                        .build();

    if (!inst_ret) {
        std::cerr << "Failed to create Vulkan instance: " << inst_ret.error().message() << "\n";
        abort();
    }
    instance = inst_ret.value();
    debug_messenger = instance.debug_messenger;

    glfwCreateWindowSurface(instance.instance, window, nullptr, &surface);

    vkb::PhysicalDeviceSelector selector{instance};
    auto phys_ret = selector.set_surface(surface)
                        .set_minimum_version(1, 2)
                        .select();
    
    if (!phys_ret) {
        std::cerr << "Failed to select physical device: " << phys_ret.error().message() << "\n";
        abort();
    }
    physical_device = phys_ret.value();

    vkb::DeviceBuilder device_builder{physical_device};
    auto dev_ret = device_builder.build();
    
    if (!dev_ret) {
        std::cerr << "Failed to create logical device: " << dev_ret.error().message() << "\n";
        abort();
    }
    device = dev_ret.value();

    graphics_queue = device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical_device.physical_device;
    allocatorInfo.device = device.device;
    allocatorInfo.instance = instance.instance;
    vmaCreateAllocator(&allocatorInfo, &allocator);
}

void LivingWorlds::init_swapchain() {
    vkb::SwapchainBuilder swapchain_builder{device};
    auto swap_ret = swapchain_builder
        .set_old_swapchain(swapchain)
        .set_desired_extent(width, height)
        .set_desired_format(VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) 
        // Request TRANSFER_DST for Blit
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
        .build();

    if (!swap_ret) {
        std::cerr << "Failed to create swapchain: " << swap_ret.error().message() << "\n";
        abort();
    }

    swapchain = swap_ret.value();
    swapchain_images = swapchain.get_images().value();
    swapchain_image_views = swapchain.get_image_views().value();
    swapchain_image_format = swapchain.image_format;
}

void LivingWorlds::init_commands() {
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = graphics_queue_family;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device.device, &commandPoolInfo, nullptr, &command_pool));

    command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = (uint32_t)command_buffers.size();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(device.device, &cmdAllocInfo, command_buffers.data()));
}

void LivingWorlds::init_default_renderpass() {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = swapchain_image_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = find_depth_format();
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device.device, &render_pass_info, nullptr, &render_pass));
}

void LivingWorlds::init_framebuffers() {
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 2; // Color + Depth
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    const uint32_t swapchain_imagecount = swapchain_images.size();
    framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

    for (uint32_t i = 0; i < swapchain_imagecount; i++) {
        std::array<VkImageView, 2> attachments = {
            swapchain_image_views[i],
            depthImageView
        };

        fb_info.pAttachments = attachments.data();
        VK_CHECK(vkCreateFramebuffer(device.device, &fb_info, nullptr, &framebuffers[i]));
    }
}

void LivingWorlds::init_sync_structures() {
    image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores.resize(swapchain_image_views.size()); // Per Swapchain Image
    in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &image_available_semaphores[i]));
        VK_CHECK(vkCreateFence(device.device, &fenceInfo, nullptr, &in_flight_fences[i]));
    }
    
    // Per Image Semaphores
    for (size_t i = 0; i < render_finished_semaphores.size(); i++) {
        VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &render_finished_semaphores[i]));
    }
}

// ================= COMPUTE =================

void LivingWorlds::create_storage_image(VkImage& image, VmaAllocation& alloc, VkImageView& view, VkFormat format) {
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; // TRANSFER_DST for clearing
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &alloc, nullptr));

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device.device, &viewInfo, nullptr, &view));
}

void LivingWorlds::init_storage_images() {
    create_storage_image(storage_images[0], storage_image_allocations[0], storage_image_views[0], VK_FORMAT_R8G8B8A8_UNORM);
    create_storage_image(storage_images[1], storage_image_allocations[1], storage_image_views[1], VK_FORMAT_R8G8B8A8_UNORM);
    
    // Week 3 Heightmaps - Switch to RGBA8 for compatibility (Debug Fix)
    create_storage_image(heightmap_images[0], heightmap_allocations[0], heightmap_views[0], VK_FORMAT_R8G8B8A8_UNORM);
    create_storage_image(heightmap_images[1], heightmap_allocations[1], heightmap_views[1], VK_FORMAT_R8G8B8A8_UNORM);
    
    std::cout << "Init Storage Images:\n";
    std::cout << "  Created Heightmap View 0: " << heightmap_views[0] << "\n";
    std::cout << "  Created Heightmap View 1: " << heightmap_views[1] << "\n";
    
    // Week 4 Biomes
    create_storage_image(temp_images[0], temp_allocations[0], temp_views[0], VK_FORMAT_R32_SFLOAT);
    create_storage_image(temp_images[1], temp_allocations[1], temp_views[1], VK_FORMAT_R32_SFLOAT);
    create_storage_image(humidity_images[0], humidity_allocations[0], humidity_views[0], VK_FORMAT_R32_SFLOAT);
    create_storage_image(humidity_images[1], humidity_allocations[1], humidity_views[1], VK_FORMAT_R32_SFLOAT);
    
    // Week 5.5 Discrete Biome (R8_UINT)
    create_storage_image(biome_images[0], biome_allocations[0], biome_views[0], VK_FORMAT_R8_UINT);
    create_storage_image(biome_images[1], biome_allocations[1], biome_views[1], VK_FORMAT_R8_UINT);
    
    // Transition ALL images to VK_IMAGE_LAYOUT_GENERAL
    for(int i=0; i<2; i++) {
        transition_image_layout(storage_images[i], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transition_image_layout(heightmap_images[i], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transition_image_layout(temp_images[i], VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transition_image_layout(humidity_images[i], VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transition_image_layout(biome_images[i], VK_FORMAT_R8_UINT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
}

void LivingWorlds::init_descriptors() {
    VkDescriptorSetLayoutBinding bindings[10] = {}; // GOL(2) + Height(2) + Temp(2) + Hum(2) + Biome(2)
    
    for(int i=0; i<10; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 10;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &compute_descriptor_layout));

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 20; // 2 sets * 10 bindings

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2; // Only 2 sets for ping-pong, but each is BIGGER now.

    VK_CHECK(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &descriptor_pool));

    compute_descriptor_sets.resize(2);
    std::vector<VkDescriptorSetLayout> layouts(2, compute_descriptor_layout);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptor_pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    VK_CHECK(vkAllocateDescriptorSets(device.device, &allocInfo, compute_descriptor_sets.data()));

    // Prepare info for all resources
    VkDescriptorImageInfo gol0 = {VK_NULL_HANDLE, storage_image_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo gol1 = {VK_NULL_HANDLE, storage_image_views[1], VK_IMAGE_LAYOUT_GENERAL};
    
    VkDescriptorImageInfo h0 = {VK_NULL_HANDLE, heightmap_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo h1 = {VK_NULL_HANDLE, heightmap_views[1], VK_IMAGE_LAYOUT_GENERAL};
    
    VkDescriptorImageInfo t0 = {VK_NULL_HANDLE, temp_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo t1 = {VK_NULL_HANDLE, temp_views[1], VK_IMAGE_LAYOUT_GENERAL};
    
    VkDescriptorImageInfo hum0 = {VK_NULL_HANDLE, humidity_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo hum1 = {VK_NULL_HANDLE, humidity_views[1], VK_IMAGE_LAYOUT_GENERAL};
    
    // Set 0: Current=0, Next=1
    // Bindings: 0:GOL0, 1:GOL1, 2:H0, 3:H1, 4:T0, 5:T1, 6:Hum0, 7:Hum1
    std::vector<VkWriteDescriptorSet> writes;
    auto add_write = [&](VkDescriptorSet set, int binding, VkDescriptorImageInfo* info) {
        VkWriteDescriptorSet w = {};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.descriptorCount = 1;
        w.pImageInfo = info;
        writes.push_back(w);
    };

    // Set 0
    add_write(compute_descriptor_sets[0], 0, &gol0);
    add_write(compute_descriptor_sets[0], 1, &gol1);
    add_write(compute_descriptor_sets[0], 2, &h0);
    add_write(compute_descriptor_sets[0], 3, &h1);
    add_write(compute_descriptor_sets[0], 4, &t0);
    add_write(compute_descriptor_sets[0], 5, &t1);
    add_write(compute_descriptor_sets[0], 6, &hum0);
    add_write(compute_descriptor_sets[0], 7, &hum1);

    // Biome bindings (8, 9)
    VkDescriptorImageInfo bio0 = {VK_NULL_HANDLE, biome_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo bio1 = {VK_NULL_HANDLE, biome_views[1], VK_IMAGE_LAYOUT_GENERAL};
    add_write(compute_descriptor_sets[0], 8, &bio0);
    add_write(compute_descriptor_sets[0], 9, &bio1);

    // Set 1: Current=1, Next=0
    // Bindings: 0:GOL1, 1:GOL0, 2:H1, 3:H0, 4:T1, 5:T0, 6:Hum1, 7:Hum0, 8:Bio1, 9:Bio0
    add_write(compute_descriptor_sets[1], 0, &gol1);
    add_write(compute_descriptor_sets[1], 1, &gol0);
    add_write(compute_descriptor_sets[1], 2, &h1);
    add_write(compute_descriptor_sets[1], 3, &h0);
    add_write(compute_descriptor_sets[1], 4, &t1);
    add_write(compute_descriptor_sets[1], 5, &t0);
    add_write(compute_descriptor_sets[1], 6, &hum1);
    add_write(compute_descriptor_sets[1], 7, &hum0);
    add_write(compute_descriptor_sets[1], 8, &bio1);
    add_write(compute_descriptor_sets[1], 9, &bio0);

    vkUpdateDescriptorSets(device.device, writes.size(), writes.data(), 0, nullptr);
}

bool LivingWorlds::load_shader_module(const char* filePath, VkShaderModule* outShaderModule) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    if (vkCreateShaderModule(device.device, &createInfo, nullptr, outShaderModule) != VK_SUCCESS) {
        return false;
    }
    return true;
}

void LivingWorlds::init_compute_pipeline() {
    VkShaderModule computeShaderModule;
    if (!load_shader_module("shaders/game_of_life.comp.spv", &computeShaderModule)) {
        std::cerr << "Failed to load compute shader: shaders/game_of_life.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &compute_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = compute_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compute_pipeline));

    vkDestroyShaderModule(device.device, computeShaderModule, nullptr);
}

void LivingWorlds::init_noise_pipeline() {
    VkShaderModule noiseShaderModule;
    if (!load_shader_module("shaders/noise_init.comp.spv", &noiseShaderModule)) {
        std::cerr << "Failed to load compute shader: shaders/noise_init.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = noiseShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConsts);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &noise_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = noise_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &noise_pipeline));

    vkDestroyShaderModule(device.device, noiseShaderModule, nullptr);
}

void LivingWorlds::init_biome_pipeline() {
    VkShaderModule biomeShader;
    if (!load_shader_module("shaders/biome_init.comp.spv", &biomeShader)) {
        std::cerr << "Failed to load shaders/biome_init.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = biomeShader;
    shaderStageInfo.pName = "main";
    
    // Layout: We need bindings for Temp/Humidity Output.
    // We will reuse `compute_descriptor_layout` but update it to have more bindings?
    // OR create a new layout for Biome init.
    // Let's modify `compute_descriptor_layout` to have Binding 4 (Temp Out), Binding 5 (Humidity Out)?
    // Or just make a dedicated layout for biome init.
    // Dedicated is cleaner.
    
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0; // Temp Out
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    bindings[1].binding = 1; // Humidity Out
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    
    // We will leak this layout instance for simplicity or reuse noise_pipeline logic?
    // For now let's use the class member `biome_pipeline_layout` but create a descriptor layout just for it?
    // No, `pipelineLayout` needs a descriptor set layout.
    // Okay, I will modify `living_worlds.hpp` later to add it. For now I'll create it here and store in `compute_descriptor_layout`? No.
    // I'll stick to a simpler plan: Add bindings to the GLOBAL `compute_descriptor_layout` if I can.
    // But `compute_descriptor_layout` is already created in `init_descriptors`.
    // I will rewrite `init_descriptors` to have ALL bindings: GOL, Height, Temp, Humidity.
    // 
    // Plan B: Use a local DescriptorSetLayout for Biome Init.
    // I need to create the layout, then the pipeline layout.
    // And I need to allocate a descriptor set for it.
    // ... This is getting verbose.
    // 
    // Let's stick to Plan A: Extend `compute_descriptor_layout` to support 8 bindings (GOL In/Out, Height In/Out, Temp In/Out, Hum In/Out).
    // This makes everything uniform.
    // I will effectively Rewrite `init_descriptors` in a separate step.
    // So `init_biome_pipeline` just uses `compute_descriptor_layout`.
    
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConsts);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &biome_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = biome_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &biome_pipeline));

    vkDestroyShaderModule(device.device, biomeShader, nullptr);
}

void LivingWorlds::init_biome_growth_pipeline() {
    VkShaderModule biomeGrowthShader;
    if (!load_shader_module("shaders/biome_growth.comp.spv", &biomeGrowthShader)) {
        std::cerr << "Failed to load shaders/biome_growth.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = biomeGrowthShader;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout; // Reuse the main compute descriptor layout

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &biome_growth_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = biome_growth_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &biome_growth_pipeline));

    vkDestroyShaderModule(device.device, biomeGrowthShader, nullptr);
}

void LivingWorlds::dispatch_biome_init() {
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkAllocateCommandBuffers(device.device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_pipeline);
    
    // Push Seed
    PushConsts push;
    push.seed = SEED;
    vkCmdPushConstants(cmd, biome_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConsts), &push);
    
    // We need to bind descriptor sets.
    // We need to update `compute_descriptor_sets` to have Temp/humidity bindings.
    // I assume `init_descriptors` will be updated to include bindings 4,5,6,7.
    // Temp[0] In(4), Out(5). Humidity[0] In(6), Out(7).
    // We are initializing, so we write to Temp[0] and Humidity[0].
    // Let's assume Output bindings are 5 and 7 for set[0].
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_pipeline_layout, 0, 1, &compute_descriptor_sets[1], 0, nullptr);
    
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device.device, command_pool, 1, &cmd);
}

void LivingWorlds::init_erosion_pipeline() {
    VkShaderModule erosionShader;
    if (!load_shader_module("shaders/erosion.comp.spv", &erosionShader)) {
        std::cerr << "Failed to load shaders/erosion.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = erosionShader;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &erosion_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = erosion_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &erosion_pipeline));

    vkDestroyShaderModule(device.device, erosionShader, nullptr);
}

// Week 5.5: Discrete Biome CA Pipeline
void LivingWorlds::init_biome_ca_pipeline() {
    VkShaderModule biomeCaShader;
    if (!load_shader_module("shaders/biome_ca.comp.spv", &biomeCaShader)) {
        std::cerr << "Failed to load shaders/biome_ca.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = biomeCaShader;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BiomePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &compute_descriptor_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &biome_ca_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = biome_ca_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &biome_ca_pipeline));

    vkDestroyShaderModule(device.device, biomeCaShader, nullptr);
}

void LivingWorlds::dispatch_biome_ca_init() {
    // Initialize discrete biome layer: All land = GRASS (2), Water = WATER (0)
    // First clear the biome images to 0
    
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkAllocateCommandBuffers(device.device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Clear both biome images to 0 (WATER) first
    VkClearColorValue clearColor = {};
    clearColor.uint32[0] = 0; // WATER = 0
    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    
    vkCmdClearColorImage(cmd, biome_images[0], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
    vkCmdClearColorImage(cmd, biome_images[1], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
    
    // Memory barrier after clear
    VkMemoryBarrier memBarrier = {};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    
    // Now run biome CA to set height-based biomes
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_ca_pipeline);
    
    // Push constants
    biomePushConstants.time = 0.0f;
    vkCmdPushConstants(cmd, biome_ca_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BiomePushConstants), &biomePushConstants);
    
    // Dispatch to both biome images
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_ca_pipeline_layout, 0, 1, &compute_descriptor_sets[0], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_ca_pipeline_layout, 0, 1, &compute_descriptor_sets[1], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device.device, command_pool, 1, &cmd);
}

void LivingWorlds::init_viz_pipeline() {
    VkDescriptorSetLayoutBinding bindings[4] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &viz_descriptor_layout));
    
    // Allocate 2 sets (one for each heightmap)
    // We can reuse the main descriptor pool if it has space, but we didn't allocate enough.
    // Let's create a small local pool for Viz.
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 8; // 2 sets * 4 bindings

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    
    // VkDescriptorPool viz_pool; // We will leak this handle for now as it's destroyed with device, technically clean would be member.
    // Ideally we add viz_descriptor_pool to class members. But for speed, let's just make it static or assume we won't resize.
    // Actually, `descriptor_pool` in init_descriptors is a member. I can just USE IT if I resize it?
    // No, existing sets are allocated. 
    // I'll create a new pool and store it in a static variable (ugly) or just leak the pool handle (it gets destroyed when device is destroyed? No, need explicit destroy).
    // Let's just add `VkDescriptorPool viz_descriptor_pool` to header via another edit to be clean.
    // Wait, I can't do multiple file edits easily.
    // Fine, I'll use a globally defined pool in this translation unit or just `descriptor_pool` if I hadn't capped it.
    // Re-reading `init_descriptors`: `poolInfo.maxSets = 2;`. It is capped.
    // OK, I will perform a separate replace_file_content to add `viz_descriptor_pool` to header. 
    VK_CHECK(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &viz_descriptor_pool));

    // Allocate 2 sets (One for each Heightmap ping-pong state)
    viz_descriptor_sets.resize(2);
    std::vector<VkDescriptorSetLayout> layouts(2, viz_descriptor_layout);
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = viz_descriptor_pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    VK_CHECK(vkAllocateDescriptorSets(device.device, &allocInfo, viz_descriptor_sets.data()));

    // Update Descriptors
    // We bind Storage[0] as the Output for both sets (it acts as a temporary display buffer)
    VkDescriptorImageInfo storageInfo = {VK_NULL_HANDLE, storage_image_views[0], VK_IMAGE_LAYOUT_GENERAL};
    
    for(int i=0; i<2; i++) {
        VkDescriptorImageInfo heightInfo = {VK_NULL_HANDLE, heightmap_views[i], VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo tempInfo = {VK_NULL_HANDLE, temp_views[i], VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo humInfo = {VK_NULL_HANDLE, humidity_views[i], VK_IMAGE_LAYOUT_GENERAL};
        
        VkWriteDescriptorSet writes[4] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = viz_descriptor_sets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &heightInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = viz_descriptor_sets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &storageInfo; // Output image

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = viz_descriptor_sets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &tempInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = viz_descriptor_sets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &humInfo;
        
        vkUpdateDescriptorSets(device.device, 4, writes, 0, nullptr);
    }
    
    // Pipeline
    VkShaderModule vizShader;
    if (!load_shader_module("shaders/heightmap_viz.comp.spv", &vizShader)) {
        std::cerr << "Failed to load shaders/heightmap_viz.comp.spv\n";
        abort();
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = vizShader;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &viz_descriptor_layout;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &viz_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = viz_pipeline_layout;

    VK_CHECK(vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &viz_pipeline));
    
    vkDestroyShaderModule(device.device, vizShader, nullptr);
}

void LivingWorlds::update_viz_descriptors() {
     // Check if sets are allocated? We need to allocate them first. 
     // Im implementing allocation inside init_viz_pipeline, but I need to fix pool size first.
     
     VkDescriptorImageInfo inputInfo = {VK_NULL_HANDLE, heightmap_views[0], VK_IMAGE_LAYOUT_GENERAL}; // Just view 0 for now? Or PingPong?
    // Unused in 2.5D rendering
}

void LivingWorlds::dispatch_noise_init() {
    // Run once at init to populate heightmap_images[0] (and 1?)
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkAllocateCommandBuffers(device.device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Use Compute Shader for Noise Generation
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline);
    
    // Push Seed
    PushConsts push;
    push.seed = currentSeed;
    vkCmdPushConstants(cmd, noise_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConsts), &push);
    
    // Bind Set 1 (Writes to heightmap_images[0] which is binding 3)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline_layout, 0, 1, &compute_descriptor_sets[1], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    // Also dispatch to Set 0 (Writes to heightmap_images[1] which is binding 3)
    VkMemoryBarrier memBarrier = {};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;  
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline_layout, 0, 1, &compute_descriptor_sets[0], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    // Global Barrier to ensure visibility to Graphics
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE));
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device.device, command_pool, 1, &cmd);
}

void LivingWorlds::initialize_grid_pattern(Pattern pattern) {
    size_t bufferSize = width * height * 4; // RGBA8
    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferAlloc;
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingBufferAlloc, nullptr);

    // Write Data
    unsigned char* data;
    vmaMapMemory(allocator, stagingBufferAlloc, (void**)&data);
    memset(data, 0, bufferSize); // Clear all

    auto set_cell = [&](int x, int y) {
        if(x>=0 && x<(int)width && y>=0 && y<(int)height) {
            size_t idx = (y * width + x) * 4;
            data[idx] = 255;
            data[idx+1] = 255;
            data[idx+2] = 255;
            data[idx+3] = 255;
        }
    };

    if (pattern == Pattern::Glider) {
        // Glider Pattern
        // 0 1 0
        // 0 0 1
        // 1 1 1
        int cx = 50; int cy = 50;
        set_cell(cx, cy-1);
        set_cell(cx+1, cy);
        set_cell(cx-1, cy+1);
        set_cell(cx, cy+1);
        set_cell(cx+1, cy+1);
    } 
    else if (pattern == Pattern::GosperGliderGun) {
        // Gosper Glider Gun (Top-Left)
        int cx = 50; int cy = 50;
        
        // Left Square
        set_cell(cx, cy+4); set_cell(cx+1, cy+4);
        set_cell(cx, cy+5); set_cell(cx+1, cy+5);

        // Right Gun
        set_cell(cx+10, cy+4); set_cell(cx+10, cy+5); set_cell(cx+10, cy+6);
        set_cell(cx+11, cy+3); set_cell(cx+11, cy+7);
        set_cell(cx+12, cy+2); set_cell(cx+12, cy+8);
        set_cell(cx+13, cy+2); set_cell(cx+13, cy+8);
        set_cell(cx+14, cy+5);
        set_cell(cx+15, cy+3); set_cell(cx+15, cy+7);
        set_cell(cx+16, cy+4); set_cell(cx+16, cy+5); set_cell(cx+16, cy+6);
        set_cell(cx+17, cy+5);

        // Left Gun
        set_cell(cx+20, cy+2); set_cell(cx+20, cy+3); set_cell(cx+20, cy+4);
        set_cell(cx+21, cy+2); set_cell(cx+21, cy+3); set_cell(cx+21, cy+4);
        set_cell(cx+22, cy+1); set_cell(cx+22, cy+5);
        
        set_cell(cx+24, cy); set_cell(cx+24, cy+1); set_cell(cx+24, cy+5); set_cell(cx+24, cy+6);

        // Right Square
        set_cell(cx+34, cy+2); set_cell(cx+34, cy+3);
        set_cell(cx+35, cy+2); set_cell(cx+35, cy+3);
    }
    else if (pattern == Pattern::Random) {
        std::srand(std::time(nullptr));
        for(size_t i=0; i<width*height; i++) {
            if ((std::rand() % 100) < 50) { // 50% density
                data[i*4] = 255;
                data[i*4+1] = 255;
                data[i*4+2] = 255;
                data[i*4+3] = 255;
            }
        }
    }
    else if (pattern == Pattern::RPentomino) {
        int cx = width/2; int cy = height/2;
        set_cell(cx+1, cy);
        set_cell(cx+2, cy);
        set_cell(cx, cy+1);
        set_cell(cx+1, cy+1);
        set_cell(cx+1, cy+2);
    }

    vmaUnmapMemory(allocator, stagingBufferAlloc);

    // Copy buffer to image[0]
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkAllocateCommandBuffers(device.device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition Image[0] to TRANSFER_DST
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; 
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = storage_images[0];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, storage_images[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to GENERAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; 

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device.device, command_pool, 1, &cmd);

    vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAlloc);
}

void LivingWorlds::draw() {
    VK_CHECK(vkWaitForFences(device.device, 1, &in_flight_fences[current_frame], true, 1000000000));
    
    uint32_t swapchain_image_index;
    VkResult result = vkAcquireNextImageKHR(device.device, swapchain.swapchain, 1000000000, 
                                            image_available_semaphores[current_frame], nullptr, &swapchain_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return;
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) abort();

    VK_CHECK(vkResetFences(device.device, 1, &in_flight_fences[current_frame]));

    VkCommandBuffer cmd = command_buffers[current_frame];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    
    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // 1. COMPUTE DISPATCH (Ping Pong)
    uint32_t use_set_index = current_sim_output_index; 
    
    VkImageMemoryBarrier computeBarriers[2];
    for(int i=0; i<2; i++) {
        computeBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        computeBarriers[i].pNext = nullptr;
        computeBarriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        computeBarriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        computeBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarriers[i].image = storage_images[i];
        computeBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        computeBarriers[i].subresourceRange.baseMipLevel = 0;
        computeBarriers[i].subresourceRange.levelCount = 1;
        computeBarriers[i].subresourceRange.baseArrayLayer = 0;
        computeBarriers[i].subresourceRange.layerCount = 1;
        computeBarriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        computeBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                         0, 0, nullptr, 0, nullptr, 2, computeBarriers);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                         0, 0, nullptr, 0, nullptr, 2, computeBarriers);

    // ---------------------------------------------------------
    // COMPUTE DISPATCH (Simulation Loop)
    // ---------------------------------------------------------
    // Calculate Delta Time
    double currentTime = glfwGetTime();
    if (lastFrameTime == 0.0) lastFrameTime = currentTime;
    float dt = (float)(currentTime - lastFrameTime);
    lastFrameTime = currentTime;
    
    simAccumulator += dt;
    
    // Process Input
    process_input(dt);

    // ---------------------------------------------------------
    // COMPUTE DISPATCH (Simulation Loop)
    // ---------------------------------------------------------
    int erosion_output_idx = current_heightmap_index;
    
    bool run_simulation = false;
    if (simAccumulator >= simInterval) {
        run_simulation = true;
        simAccumulator -= simInterval;
        if(simAccumulator > simInterval) simAccumulator = 0.0f;
    }

    if (run_simulation) {
        erosion_output_idx = (current_heightmap_index + 1) % 2;
        
        // 1. EROSION
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, erosion_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, erosion_pipeline_layout, 0, 1, &compute_descriptor_sets[current_heightmap_index], 0, nullptr);
        vkCmdDispatch(cmd, width/16, height/16, 1);
        
        // Barrier for Erosion Output -> Biome Input
        VkImageMemoryBarrier erosionBarrier = {};
        erosionBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        erosionBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        erosionBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        erosionBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        erosionBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        erosionBarrier.image = heightmap_images[erosion_output_idx];
        erosionBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        erosionBarrier.subresourceRange.baseMipLevel = 0;
        erosionBarrier.subresourceRange.levelCount = 1;
        erosionBarrier.subresourceRange.baseArrayLayer = 0;
        erosionBarrier.subresourceRange.layerCount = 1;
        erosionBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        erosionBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        // Barrier for Erosion Output -> Biome CA
        VkMemoryBarrier memBar = {};
        memBar.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBar, 0, nullptr, 0, nullptr);
        
        // 2. DISCRETE BIOME CA
        static uint32_t simStep = 0;
        simStep++;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_ca_pipeline);
        biomePushConstants.time = static_cast<float>(simStep); // Use step counter for consistent hash
        vkCmdPushConstants(cmd, biome_ca_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BiomePushConstants), &biomePushConstants);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_ca_pipeline_layout, 0, 1, &compute_descriptor_sets[current_heightmap_index], 0, nullptr);
        vkCmdDispatch(cmd, width/16, height/16, 1);
    }

    // ---------------------------------------------------------
    // GRAPHICS BARRIERS (Transition for Reading)
    // ---------------------------------------------------------
    // Barrier for Biome Output -> Viz Input
    
    // Barrier for Biome Output -> Graphics Input (Vertex/Fragment)
    VkImageMemoryBarrier biomeBarrier[2] = {};
    for (int i=0; i<2; i++) {
        biomeBarrier[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        biomeBarrier[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        biomeBarrier[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        biomeBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        biomeBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        biomeBarrier[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        // Read in Vertex (height) and Fragment (color)
        biomeBarrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        biomeBarrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        biomeBarrier[i].subresourceRange.baseMipLevel = 0;
        biomeBarrier[i].subresourceRange.levelCount = 1;
        biomeBarrier[i].subresourceRange.baseArrayLayer = 0;
        biomeBarrier[i].subresourceRange.layerCount = 1;
        biomeBarrier[i].image = (i==0) ? temp_images[erosion_output_idx] : humidity_images[erosion_output_idx];
    }
    // Also include Heightmap in this barrier? 
    // Heightmap was barriered in erosion step for Compute Read. 
    // We should barrier it again for Graphics Read if we want to be safe, or extend that barrier.
    // Let's add heightmap to this barrier array.
    
    VkImageMemoryBarrier graphicsBarriers[3];
    graphicsBarriers[0] = biomeBarrier[0]; // Temp
    graphicsBarriers[1] = biomeBarrier[1]; // Hum
    
    // Heightmap
    graphicsBarriers[2] = graphicsBarriers[0];
    graphicsBarriers[2].image = heightmap_images[erosion_output_idx];
    
    vkCmdPipelineBarrier(cmd, 
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 3, graphicsBarriers);

    // 3. 2.5D VISUALIZATION (Graphics Pipeline)
    update_uniform_buffer(current_frame);
    
    // Begin Render Pass
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = render_pass;
    renderPassInfo.framebuffer = framebuffers[swapchain_image_index];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain.extent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // Background color
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain_pipeline);

    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Bind Descriptors
    // Set 0: UBO (per frame)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain_pipeline_layout, 
                            0, 1, &ubo_descriptor_sets[current_frame], 0, nullptr);
    
    // Set 1: Textures (per simulation step)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain_pipeline_layout, 
                            1, 1, &texture_descriptor_sets[erosion_output_idx], 0, nullptr);

    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);

    // Update State for Next Frame
    current_heightmap_index = erosion_output_idx;

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_semaphores[current_frame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_semaphores[swapchain_image_index]; // Per Image
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit, in_flight_fences[current_frame]));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &render_finished_semaphores[swapchain_image_index]; // Per Image
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.pImageIndices = &swapchain_image_index;

    vkQueuePresentKHR(graphics_queue, &presentInfo);

    current_sim_output_index = (current_sim_output_index + 1) % 2;
    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    
    // FPS Counter
    double current_time = glfwGetTime();
    frames_this_second++;
    if (current_time - last_timestamp >= 1.0) {
        std::cout << "FPS: " << frames_this_second << " (" << (1000.0f / frames_this_second) << " ms/frame)\r" << std::flush;
        frames_this_second = 0;
        last_timestamp = current_time;
    }
}

void LivingWorlds::main_loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        draw();
    }
    vkDeviceWaitIdle(device.device);
    std::cout << "\nTerminating...\n";
}

void LivingWorlds::cleanup() {
    vkDeviceWaitIdle(device.device); 

    // Compute
    vkDestroyPipeline(device.device, compute_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, compute_pipeline_layout, nullptr);
    vkDestroyPipeline(device.device, noise_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, noise_pipeline_layout, nullptr);
    vkDestroyPipeline(device.device, biome_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, biome_pipeline_layout, nullptr);
    vkDestroyPipeline(device.device, biome_growth_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, biome_growth_pipeline_layout, nullptr);
    vkDestroyPipeline(device.device, erosion_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, erosion_pipeline_layout, nullptr);
    
    // Terrain Pipeline Cleanup
    vkDestroyPipeline(device.device, terrain_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, terrain_pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device.device, ubo_descriptor_layout, nullptr);
    vkDestroyDescriptorPool(device.device, ubo_descriptor_pool, nullptr);
    
    vkDestroyDescriptorSetLayout(device.device, texture_descriptor_layout, nullptr);
    vkDestroyDescriptorPool(device.device, texture_descriptor_pool, nullptr);
    vkDestroySampler(device.device, textureSampler, nullptr);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vmaUnmapMemory(allocator, uniformBuffersAllocation[i]);
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformBuffersAllocation[i]);
    }
    
    vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
    vmaDestroyBuffer(allocator, indexBuffer, indexBufferAllocation);
    
    vkDestroyImageView(device.device, depthImageView, nullptr);
    vmaDestroyImage(allocator, depthImage, depthImageAllocation);

    vkDestroyPipeline(device.device, viz_pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, viz_pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device.device, viz_descriptor_layout, nullptr);
    vkDestroyDescriptorPool(device.device, viz_descriptor_pool, nullptr);
    vkDestroyDescriptorPool(device.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(device.device, compute_descriptor_layout, nullptr);
    
    for(int i=0; i<2; i++) {
        vkDestroyImageView(device.device, storage_image_views[i], nullptr);
        vmaDestroyImage(allocator, storage_images[i], storage_image_allocations[i]);
        vkDestroyImageView(device.device, heightmap_views[i], nullptr);
        vmaDestroyImage(allocator, heightmap_images[i], heightmap_allocations[i]);
        vkDestroyImageView(device.device, temp_views[i], nullptr);
        vmaDestroyImage(allocator, temp_images[i], temp_allocations[i]);
        vkDestroyImageView(device.device, humidity_views[i], nullptr);
        vmaDestroyImage(allocator, humidity_images[i], humidity_allocations[i]);
    }

    // Sync
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if(in_flight_fences[i]) vkDestroyFence(device.device, in_flight_fences[i], nullptr);
        if(image_available_semaphores[i]) vkDestroySemaphore(device.device, image_available_semaphores[i], nullptr);
    }
    for (size_t i = 0; i < render_finished_semaphores.size(); i++) {
        if(render_finished_semaphores[i]) vkDestroySemaphore(device.device, render_finished_semaphores[i], nullptr);
    }

    vkDestroyCommandPool(device.device, command_pool, nullptr);
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device.device, framebuffer, nullptr);
    }
    vkDestroyRenderPass(device.device, render_pass, nullptr);
    for (auto imageView : swapchain_image_views) {
        vkDestroyImageView(device.device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device.device, swapchain.swapchain, nullptr);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device.device, nullptr);
    vkDestroySurfaceKHR(instance.instance, surface, nullptr);
    vkb::destroy_debug_utils_messenger(instance.instance, debug_messenger);
    vkDestroyInstance(instance.instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
}

// =================================================================================================
// Week 5: 2.5D Rendering Resources
// =================================================================================================

void LivingWorlds::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer, VmaAllocation& allocation) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }
}

void LivingWorlds::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    // Single time command buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = command_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    
    vkFreeCommandBuffers(device.device, command_pool, 1, &commandBuffer);
}

void LivingWorlds::transition_image_layout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = command_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT; // Prepare for compute write
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);

    vkFreeCommandBuffers(device.device, command_pool, 1, &commandBuffer);
}

void LivingWorlds::create_grid_mesh() {
    // Grid size matches texture size for 1:1 mapping
    // But for performance, maybe subsample? 
    // Let's try 1:1 first (1024x1024 = 1M verts, 2M tris). Modern GPUs can handle it.
    // Actually, to be safe, let's just do 512x512 first.
    int gridW = width;
    int gridH = height;
    
    // Optimization: Depending on FPS, we might want to scale down.
    // Vertices
    vertices.resize(gridW * gridH);
    for (int y = 0; y < gridH; y++) {
        for (int x = 0; x < gridW; x++) {
            // Normalized 0..1 coordinates
            float u = (float)x / (gridW - 1);
            float v = (float)y / (gridH - 1);
            // Center at 0,0
            vertices[y * gridW + x] = Vertex{ {u, v} };
        }
    }
    
    // Indices (Triangle Strip or List)
    // List is easier for standard pipelines.
    // (W-1) * (H-1) quads * 6 indices
    indices.resize((gridW - 1) * (gridH - 1) * 6);
    int idx = 0;
    for (int y = 0; y < gridH - 1; y++) {
        for (int x = 0; x < gridW - 1; x++) {
            // Quad: TL, BL, BR, TL, BR, TR
            uint32_t tl = y * gridW + x;
            uint32_t bl = (y + 1) * gridW + x;
            uint32_t br = (y + 1) * gridW + (x + 1);
            uint32_t tr = y * gridW + (x + 1);
            
            indices[idx++] = tl;
            indices[idx++] = bl;
            indices[idx++] = br;
            
            indices[idx++] = tl;
            indices[idx++] = br;
            indices[idx++] = tr;
        }
    }
    
    std::cout << "Generated Grid Mesh: " << vertices.size() << " vertices, " << indices.size() << " indices.\n";
}

void LivingWorlds::create_vertex_buffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferAllocation;
    create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingBufferAllocation);
    
    void* data;
    vmaMapMemory(allocator, stagingBufferAllocation, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vmaUnmapMemory(allocator, stagingBufferAllocation);
    
    create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, vertexBuffer, vertexBufferAllocation);
    
    copy_buffer(stagingBuffer, vertexBuffer, bufferSize);
    
    vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
}

void LivingWorlds::create_index_buffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferAllocation;
    create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingBufferAllocation);
    
    void* data;
    vmaMapMemory(allocator, stagingBufferAllocation, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vmaUnmapMemory(allocator, stagingBufferAllocation);
    
    create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, indexBuffer, indexBufferAllocation);
    
    copy_buffer(stagingBuffer, indexBuffer, bufferSize);
    
    vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
}

void LivingWorlds::create_uniform_buffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersAllocation.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        create_buffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, uniformBuffers[i], uniformBuffersAllocation[i]);
        vmaMapMemory(allocator, uniformBuffersAllocation[i], &uniformBuffersMapped[i]);
    }
}

void LivingWorlds::update_uniform_buffer(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    
    UniformBufferObject ubo = {};
    
    // Rotating Camera
    // ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(10.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.model = glm::mat4(1.0f);
    
    // Update Camera (WASD)
    // We process input here or in draw?
    // process_input calculates delta time internally or we pass it? 
    // We already have time. We need delta time.
    // Let's rely on frames_this_second calculation or just measure it properly handling wrapped time.
    static auto lastFrameTime = std::chrono::high_resolution_clock::now();
    auto currentFrameTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameTime - lastFrameTime).count();
    lastFrameTime = currentFrameTime;
    
    process_input(deltaTime);

    ubo.view = camera.getViewMatrix();
    
    // Perspective
    ubo.proj = glm::perspective(glm::radians(45.0f), swapchain.extent.width / (float) swapchain.extent.height, 0.1f, 1000.0f);
    ubo.proj[1][1] *= -1; // Vulkan Y-flip
    ubo.time = (float)glfwGetTime();
    ubo.vizMode = vizMode;
    
    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

VkFormat LivingWorlds::find_depth_format() {
    std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physical_device.physical_device, format, &props);
        
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported depth format!");
}

void LivingWorlds::create_depth_resources() {
    VkFormat depthFormat = find_depth_format();
    
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchain.extent.width;
    imageInfo.extent.height = swapchain.extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr);
    
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(device.device, &viewInfo, nullptr, &depthImageView);
}

// =================================================================================================
// Week 5: Terrain Pipeline & Descriptors
// =================================================================================================

void LivingWorlds::create_ubo_descriptors() {
    // 1. Layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &ubo_descriptor_layout));

    // 2. Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VK_CHECK(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &ubo_descriptor_pool));

    // 3. Allocate Sets
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, ubo_descriptor_layout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = ubo_descriptor_pool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    ubo_descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(device.device, &allocInfo, ubo_descriptor_sets.data()));

    // 4. Update Sets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = ubo_descriptor_sets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device.device, 1, &descriptorWrite, 0, nullptr);
    }
}

void LivingWorlds::create_texture_descriptors() {
    // 1. Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f; 
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    VK_CHECK(vkCreateSampler(device.device, &samplerInfo, nullptr, &textureSampler));

    // 2. Layout - Only Height (0) and Biome (1)
    VkDescriptorSetLayoutBinding bindings[2] = {};
    
    // Height
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Biome (R8_UINT)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &texture_descriptor_layout));

    // 3. Pool (2 bindings * 2 sets = 4)
    VkDescriptorPoolSize poolSizes[1];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2;

    VK_CHECK(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &texture_descriptor_pool));

    // 4. Allocate Sets
    std::vector<VkDescriptorSetLayout> layouts(2, texture_descriptor_layout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = texture_descriptor_pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    texture_descriptor_sets.resize(2);
    VK_CHECK(vkAllocateDescriptorSets(device.device, &allocInfo, texture_descriptor_sets.data()));

    // 5. Update Sets
    for (int i = 0; i < 2; i++) {
        VkDescriptorImageInfo heightInfo{};
        heightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        heightInfo.imageView = heightmap_views[i];
        heightInfo.sampler = textureSampler;
        
        VkDescriptorImageInfo biomeInfo{};
        biomeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        biomeInfo.imageView = biome_views[i];
        biomeInfo.sampler = textureSampler;
        
        VkWriteDescriptorSet writes[2] = {};
        
        // Height (Binding 0)
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = texture_descriptor_sets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &heightInfo;
        
        // Biome (Binding 1)
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = texture_descriptor_sets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &biomeInfo;
        
        vkUpdateDescriptorSets(device.device, 2, writes, 0, nullptr);
    }
}

void LivingWorlds::init_terrain_pipeline() {
    create_ubo_descriptors();
    create_texture_descriptors();
    
    // Shaders
    VkShaderModule vertShader, fragShader;
    if (!load_shader_module("shaders/terrain.vert.spv", &vertShader) ||
        !load_shader_module("shaders/terrain.frag.spv", &fragShader)) {
        abort();
    }
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main", nullptr }
    };
    
    // Vertex Input
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; 
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Layout: Set 0 = UBO, Set 1 = Textures
    VkDescriptorSetLayout setLayouts[] = { ubo_descriptor_layout, texture_descriptor_layout };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;

    VK_CHECK(vkCreatePipelineLayout(device.device, &pipelineLayoutInfo, nullptr, &terrain_pipeline_layout));
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = terrain_pipeline_layout;
    pipelineInfo.renderPass = render_pass;
    pipelineInfo.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrain_pipeline));

    vkDestroyShaderModule(device.device, vertShader, nullptr);
    vkDestroyShaderModule(device.device, fragShader, nullptr);
}

// =================================================================================================
// Input Handling
// =================================================================================================

void LivingWorlds::process_input(float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Visualization Modes
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) vizMode = 0; // Default
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) vizMode = 1; // Temperature
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) vizMode = 2; // Humidity

    // Simulation Speed
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) 
        simInterval = std::min(simInterval + 0.005f, 1.0f); // Slower
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
        simInterval = std::max(simInterval - 0.005f, 0.001f); // Faster

    // Reset Map (with debouncing)
    static bool resetPressed = false;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
        if (!resetPressed) {
            resetPressed = true;
            vkDeviceWaitIdle(device.device); // Wait for GPU
            
            // Generate new random seed from current time
            currentSeed = static_cast<float>(glfwGetTime() * 1000.0);
            
            dispatch_noise_init();
            dispatch_biome_init();
            // Run biome CA init once (now clears images first)
            dispatch_biome_ca_init();
            current_heightmap_index = 0;
            simAccumulator = 0.0f; // Reset simulation timer
            
            // Reset biome step counter for seeding
            static uint32_t simStep = 0;
            simStep = 0;
        }
    } else {
        resetPressed = false;
    }
    
    // Toggle seeding mode (T key)
    // High density = initial clusters, Low density = pure CA
    static bool seedingPressed = false;
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) {
        if (!seedingPressed) {
            seedingPressed = true;
            // Toggle between seeded (0.3) and pure CA (0.0)
            if (biomePushConstants.forestChance > 0.1f) {
                // Switch to PURE CA (no seeding)
                biomePushConstants.forestChance = 0.0f;
                biomePushConstants.desertChance = 0.0f;
                printf("Mode: PURE CA (no initial seeding)\n");
            } else {
                // Switch to SEEDED mode
                biomePushConstants.forestChance = 0.3f;
                biomePushConstants.desertChance = 0.3f;
                printf("Mode: SEEDED (initial clusters, then CA)\n");
            }
        }
    } else {
        seedingPressed = false;
    }

    float velocity = camera.movementSpeed * deltaTime;
    // Speedup
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        velocity *= 3.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.position += camera.front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.position -= camera.front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.position -= camera.right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.position += camera.right * velocity;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        camera.position += camera.up * velocity; // Up
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        camera.position -= camera.up * velocity; // Down
}

void LivingWorlds::mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    LivingWorlds* app = reinterpret_cast<LivingWorlds*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->handle_mouse(xpos, ypos);
    }
}

void LivingWorlds::handle_mouse(double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.yaw   += xoffset * camera.mouseSensitivity;
    camera.pitch += yoffset * camera.mouseSensitivity;

    // Constrain pitch
    if (camera.pitch > 89.0f)
        camera.pitch = 89.0f;
    if (camera.pitch < -89.0f)
        camera.pitch = -89.0f;

    camera.updateCameraVectors();
}
