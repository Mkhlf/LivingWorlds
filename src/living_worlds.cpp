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
    init_default_renderpass();
    init_framebuffers();
    init_sync_structures();
    
    // Compute setup
    init_storage_images();
    init_descriptors();
    init_compute_pipeline();
    init_noise_pipeline();
    init_noise_pipeline();
    dispatch_noise_init();
    
    init_biome_pipeline();
    dispatch_biome_init();
    
    init_biome_growth_pipeline();
    init_erosion_pipeline();
    init_viz_pipeline();
}

void LivingWorlds::init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Living Worlds", nullptr, nullptr);
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

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    VK_CHECK(vkCreateRenderPass(device.device, &render_pass_info, nullptr, &render_pass));
}

void LivingWorlds::init_framebuffers() {
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 1;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.layers = 1;

    const uint32_t swapchain_imagecount = swapchain_images.size();
    framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

    for (uint32_t i = 0; i < swapchain_imagecount; i++) {
        fb_info.pAttachments = &swapchain_image_views[i];
        VK_CHECK(vkCreateFramebuffer(device.device, &fb_info, nullptr, &framebuffers[i]));
    }
}

void LivingWorlds::init_sync_structures() {
    image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &image_available_semaphores[i]));
        VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &render_finished_semaphores[i]));
        VK_CHECK(vkCreateFence(device.device, &fenceInfo, nullptr, &in_flight_fences[i]));
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
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // TRANSFER_DST for clearing
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
    
    // Week 3 Heightmaps
    create_storage_image(heightmap_images[0], heightmap_allocations[0], heightmap_views[0], VK_FORMAT_R32_SFLOAT);
    create_storage_image(heightmap_images[1], heightmap_allocations[1], heightmap_views[1], VK_FORMAT_R32_SFLOAT);
    
    // Week 4 Biomes
    create_storage_image(temp_images[0], temp_allocations[0], temp_views[0], VK_FORMAT_R32_SFLOAT);
    create_storage_image(temp_images[1], temp_allocations[1], temp_views[1], VK_FORMAT_R32_SFLOAT);
    create_storage_image(humidity_images[0], humidity_allocations[0], humidity_views[0], VK_FORMAT_R32_SFLOAT);
    create_storage_image(humidity_images[1], humidity_allocations[1], humidity_views[1], VK_FORMAT_R32_SFLOAT);
    
    // Transition both to GENERAL layout immediately to simplify barriers
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

    for (int i=0; i<2; i++) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = storage_images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    for (int i=0; i<2; i++) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = heightmap_images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                              0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    
    // Transition Week 4 Biomes
    for (int i=0; i<2; i++) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = temp_images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        barrier.image = humidity_images[i];
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);
    vkFreeCommandBuffers(device.device, command_pool, 1, &cmd);
}

void LivingWorlds::init_descriptors() {
    VkDescriptorSetLayoutBinding bindings[8] = {}; // GOL(2) + Height(2) + Temp(2) + Hum(2)
    
    for(int i=0; i<8; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 8;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &compute_descriptor_layout));

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 16; // 2 sets * 8 bindings

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

    // Set 1: Current=1, Next=0
    // Bindings: 0:GOL1, 1:GOL0, 2:H1, 3:H0, 4:T1, 5:T0, 6:Hum1, 7:Hum0
    add_write(compute_descriptor_sets[1], 0, &gol1);
    add_write(compute_descriptor_sets[1], 1, &gol0);
    add_write(compute_descriptor_sets[1], 2, &h1);
    add_write(compute_descriptor_sets[1], 3, &h0);
    add_write(compute_descriptor_sets[1], 4, &t1);
    add_write(compute_descriptor_sets[1], 5, &t0);
    add_write(compute_descriptor_sets[1], 6, &hum1);
    add_write(compute_descriptor_sets[1], 7, &hum0);

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

void LivingWorlds::update_viz_descriptors(int image_index) {
     // Check if sets are allocated? We need to allocate them first. 
     // Im implementing allocation inside init_viz_pipeline, but I need to fix pool size first.
     
     VkDescriptorImageInfo inputInfo = {VK_NULL_HANDLE, heightmap_views[0], VK_IMAGE_LAYOUT_GENERAL}; // Just view 0 for now? Or PingPong?
     // We want to view the CURRENT output.
     // But descriptor set update is expensive to do every frame if we wait for idle?
     // Actually vkUpdateDescriptorSets is fast.
     // But we need to update the SWAPCHAIN image binding which changes every frame (potentially).
     // Wait, swapchain images are fixed. `image_index` refers to which swapchain image we acquired.
     // So `viz_descriptor_sets[image_index]` should be permanently bound to `swapchain_images[image_index]`.
     // The INPUT heightmap is dynamic (ping-pong). 
     // So we can update Binding 0 every frame? Or bind both heightmaps and select in shader?
     // Let's just update Binding 0 every frame.
     
     VkWriteDescriptorSet write = {};
     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
     write.dstSet = viz_descriptor_sets[image_index];
     write.dstBinding = 0;
     write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
     write.descriptorCount = 1;
     write.pImageInfo = &inputInfo; // Update this with correct index?
     // ...
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
    
    // Transition Heightmap[0] to General (should already be General from init_storage)
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline);
    
    // Push Seed
    PushConsts push;
    push.seed = SEED;
    vkCmdPushConstants(cmd, noise_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConsts), &push);
    
    // Bind Set 1 (Writes to heightmap_images[0] which is binding 3 if we use Set 1 layout)
    // Wait, descriptors are shared. 
    // Set 1 binds heightmap_images[0] to Binding 3.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, noise_pipeline_layout, 0, 1, &compute_descriptor_sets[1], 0, nullptr);
    
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

    // 1. EROSION (Ping Pong)
    // Input: Heightmap[current] -> Output: Heightmap[next]
    // We reuse compute_descriptor_sets structure:
    // set[0]: HeightIn=0, HeightOut=1
    // set[1]: HeightIn=1, HeightOut=0
    
    // If current is 0, we use set[0] (Writes to 1). Next is 1.
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, erosion_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, erosion_pipeline_layout, 0, 1, &compute_descriptor_sets[current_heightmap_index], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    // Barrier for Erosion Output -> Viz Input
    // Output is heightmap_images[(current + 1) % 2]
    int erosion_output_idx = (current_heightmap_index + 1) % 2;
    
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
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &erosionBarrier);
    
    // 2. BIOME GROWTH (Ping Pong)
    // Same as erosion but for Temp/Hum. Reuses compute_descriptor_sets.
    // Reads Temp[current] -> Writes Temp[next] (Bindings 4,5,6,7)
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_growth_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, biome_growth_pipeline_layout, 0, 1, &compute_descriptor_sets[current_heightmap_index], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    // Barrier for Biome Output -> Viz Input
    // Output is same index as Erosion output (next index)
    
    VkImageMemoryBarrier biomeBarrier[2] = {};
    for (int i=0; i<2; i++) {
        biomeBarrier[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        biomeBarrier[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        biomeBarrier[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        biomeBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        biomeBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        biomeBarrier[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        biomeBarrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        biomeBarrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        biomeBarrier[i].subresourceRange.baseMipLevel = 0;
        biomeBarrier[i].subresourceRange.levelCount = 1;
        biomeBarrier[i].subresourceRange.baseArrayLayer = 0;
        biomeBarrier[i].subresourceRange.layerCount = 1;
    }
    biomeBarrier[0].image = temp_images[erosion_output_idx];
    biomeBarrier[1].image = humidity_images[erosion_output_idx];
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, biomeBarrier);
    
    // 3. VISUALIZATION
    // Input: Heightmap[erosion_output_idx], Temp[erosion_output_idx], Hum[erosion_output_idx], Hum[erosion_output_idx]
    int display_buffer_idx = 0;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, viz_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, viz_pipeline_layout, 0, 1, &viz_descriptor_sets[erosion_output_idx], 0, nullptr);
    vkCmdDispatch(cmd, width/16, height/16, 1);
    
    // Update State for Next Frame
    current_heightmap_index = erosion_output_idx;

    // 3. COPY OUTPUT TO SWAPCHAIN
    VkImage sourceImage = storage_images[display_buffer_idx];

    VkImageMemoryBarrier copyBarrier = {};
    copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    copyBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copyBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    copyBarrier.image = sourceImage;
    copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyBarrier.subresourceRange.baseMipLevel = 0;
    copyBarrier.subresourceRange.levelCount = 1;
    copyBarrier.subresourceRange.baseArrayLayer = 0;
    copyBarrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

    VkImageMemoryBarrier swapBarrier = {};
    swapBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapBarrier.image = swapchain_images[swapchain_image_index];
    swapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapBarrier.subresourceRange.baseMipLevel = 0;
    swapBarrier.subresourceRange.levelCount = 1;
    swapBarrier.subresourceRange.baseArrayLayer = 0;
    swapBarrier.subresourceRange.layerCount = 1;
    swapBarrier.srcAccessMask = 0;
    swapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &swapBarrier);

    VkImageBlit blitRegion = {};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[1] = {(int32_t)width, (int32_t)height, 1};
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[1] = {(int32_t)width, (int32_t)height, 1};

    vkCmdBlitImage(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blitRegion, VK_FILTER_NEAREST);

    swapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    swapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &swapBarrier);
    
    copyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copyBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    copyBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_semaphores[current_frame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_semaphores[current_frame];
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit, in_flight_fences[current_frame]));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &render_finished_semaphores[current_frame];
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
