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
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
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

void LivingWorlds::create_storage_image(VkImage& image, VmaAllocation& alloc, VkImageView& view) {
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
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
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device.device, &viewInfo, nullptr, &view));
}

void LivingWorlds::init_storage_images() {
    create_storage_image(storage_images[0], storage_image_allocations[0], storage_image_views[0]);
    create_storage_image(storage_images[1], storage_image_allocations[1], storage_image_views[1]);
    
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
    VkDescriptorSetLayoutBinding bindings[2] = {};
    // Binding 0: Input (Readonly)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Output (Writeonly)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VK_CHECK(vkCreateDescriptorSetLayout(device.device, &layoutInfo, nullptr, &compute_descriptor_layout));

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 4; // 2 sets * 2 bindings

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;

    VK_CHECK(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &descriptor_pool));

    compute_descriptor_sets.resize(2);
    std::vector<VkDescriptorSetLayout> layouts(2, compute_descriptor_layout);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptor_pool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    VK_CHECK(vkAllocateDescriptorSets(device.device, &allocInfo, compute_descriptor_sets.data()));

    // Set 0: Input=Img0, Output=Img1
    VkDescriptorImageInfo inputInfo0 = {VK_NULL_HANDLE, storage_image_views[0], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo outputInfo0 = {VK_NULL_HANDLE, storage_image_views[1], VK_IMAGE_LAYOUT_GENERAL};
    
    // Set 1: Input=Img1, Output=Img0
    VkDescriptorImageInfo inputInfo1 = {VK_NULL_HANDLE, storage_image_views[1], VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo outputInfo1 = {VK_NULL_HANDLE, storage_image_views[0], VK_IMAGE_LAYOUT_GENERAL};

    VkWriteDescriptorSet descriptorWrites[4] = {};
    // Set 0
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = compute_descriptor_sets[0];
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &inputInfo0;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = compute_descriptor_sets[0];
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &outputInfo0;

    // Set 1
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = compute_descriptor_sets[1];
    descriptorWrites[2].dstBinding = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &inputInfo1;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = compute_descriptor_sets[1];
    descriptorWrites[3].dstBinding = 1;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pImageInfo = &outputInfo1;

    vkUpdateDescriptorSets(device.device, 4, descriptorWrites, 0, nullptr);
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout, 0, 1, &compute_descriptor_sets[use_set_index], 0, nullptr);
    
    vkCmdDispatch(cmd, width/16, height/16, 1);

    // 2. COPY OUTPUT TO SWAPCHAIN
    int output_image_idx = (use_set_index == 0) ? 1 : 0;
    VkImage sourceImage = storage_images[output_image_idx];

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

    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = width;
    copyRegion.extent.height = height;
    copyRegion.extent.depth = 1;

    vkCmdCopyImage(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copyRegion);

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
    
    frame_count++;
    std::cout << "Frame: " << frame_count << "\r" << std::flush;
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
    vkDestroyDescriptorPool(device.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(device.device, compute_descriptor_layout, nullptr);
    
    for(int i=0; i<2; i++) {
        vkDestroyImageView(device.device, storage_image_views[i], nullptr);
        vmaDestroyImage(allocator, storage_images[i], storage_image_allocations[i]);
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
