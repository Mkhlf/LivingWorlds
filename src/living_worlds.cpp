#include "living_worlds.hpp"

#include <iostream>

#define VK_CHECK(x)                                                 \
    do {                                                            \
        VkResult err = x;                                           \
        if (err) {                                                  \
            std::cerr << "Detected Vulkan error: " << err << "\n";  \
            abort();                                                \
        }                                                           \
    } while (0)

void LivingWorlds::run() {
    init();
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
}

void LivingWorlds::init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Living Worlds", nullptr, nullptr);
}

void LivingWorlds::init_vulkan() {
    // 1. Instance
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("Living Worlds")
                        .request_validation_layers(true) // Enable validation layers
                        .require_api_version(1, 3, 0) // Vulkan 1.3
                        .use_default_debug_messenger()
                        .build();

    if (!inst_ret) {
        std::cerr << "Failed to create Vulkan instance: " << inst_ret.error().message() << "\n";
        abort();
    }
    instance = inst_ret.value();
    debug_messenger = instance.debug_messenger;

    // 2. Surface
    glfwCreateWindowSurface(instance.instance, window, nullptr, &surface);

    // 3. Physical Device
    vkb::PhysicalDeviceSelector selector{instance};
    auto phys_ret = selector.set_surface(surface)
                        .set_minimum_version(1, 2) // Require at least 1.2 features if needed, but we asked 1.3
                        .select();
    
    if (!phys_ret) {
        std::cerr << "Failed to select physical device: " << phys_ret.error().message() << "\n";
        abort();
    }
    physical_device = phys_ret.value();

    // 4. Logical Device
    vkb::DeviceBuilder device_builder{physical_device};
    auto dev_ret = device_builder.build();
    
    if (!dev_ret) {
        std::cerr << "Failed to create logical device: " << dev_ret.error().message() << "\n";
        abort();
    }
    device = dev_ret.value();

    // Get queues
    graphics_queue = device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = device.get_queue_index(vkb::QueueType::graphics).value();

    // VMA Allocator
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
        // fallback to common format if above not found? vkb handles selection well
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // VSync
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
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.queueFamilyIndex = graphics_queue_family;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device.device, &commandPoolInfo, nullptr, &command_pool));

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandPool = command_pool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(device.device, &cmdAllocInfo, &main_command_buffer));
}

void LivingWorlds::init_default_renderpass() {
    // Simple renderpass with color attachment, clear to red
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
    fb_info.pNext = nullptr;
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
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &present_semaphore));
    VK_CHECK(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &render_semaphore));
    VK_CHECK(vkCreateFence(device.device, &fenceInfo, nullptr, &render_fence));
}

void LivingWorlds::draw() {
    // Wait for fence
    VK_CHECK(vkWaitForFences(device.device, 1, &render_fence, true, 1000000000));
    VK_CHECK(vkResetFences(device.device, 1, &render_fence));

    // Acquire
    uint32_t swapchain_image_index;
    VK_CHECK(vkAcquireNextImageKHR(device.device, swapchain.swapchain, 1000000000, present_semaphore, nullptr, &swapchain_image_index));

    // Record Commands
    VK_CHECK(vkResetCommandBuffer(main_command_buffer, 0));
    
    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(main_command_buffer, &cmdBeginInfo));

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = render_pass;
    rpInfo.framebuffer = framebuffers[swapchain_image_index];
    rpInfo.renderArea.offset.x = 0;
    rpInfo.renderArea.offset.y = 0;
    rpInfo.renderArea.extent.width = width;
    rpInfo.renderArea.extent.height = height;

    VkClearValue clearValue;
    // Red color
    clearValue.color = { { 1.0f, 0.0f, 0.0f, 1.0f } };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(main_command_buffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    // Do nothing (clear only)
    vkCmdEndRenderPass(main_command_buffer);

    VK_CHECK(vkEndCommandBuffer(main_command_buffer));

    // Submit
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask = &waitStage;
    
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &present_semaphore;
    
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_semaphore;
    
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &main_command_buffer;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit, render_fence));

    // Present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &render_semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.swapchain;
    presentInfo.pImageIndices = &swapchain_image_index;

    VK_CHECK(vkQueuePresentKHR(graphics_queue, &presentInfo));
}

void LivingWorlds::main_loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        draw();
    }
    // Wait idle before cleanup
    vkDeviceWaitIdle(device.device);
}

void LivingWorlds::cleanup() {
    // Delete sync objects
    vkDestroyFence(device.device, render_fence, nullptr);
    vkDestroySemaphore(device.device, render_semaphore, nullptr);
    vkDestroySemaphore(device.device, present_semaphore, nullptr);

    // Delete commands
    vkDestroyCommandPool(device.device, command_pool, nullptr);

    // Delete framebuffers
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device.device, framebuffer, nullptr);
    }

    // Delete renderpass
    vkDestroyRenderPass(device.device, render_pass, nullptr);

    // Delete swapchain
    for (auto imageView : swapchain_image_views) {
        vkDestroyImageView(device.device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device.device, swapchain.swapchain, nullptr);

    // VMA
    vmaDestroyAllocator(allocator);

    // Device
    vkDestroyDevice(device.device, nullptr);
    
    // Surface
    vkDestroySurfaceKHR(instance.instance, surface, nullptr);
    vkb::destroy_debug_utils_messenger(instance.instance, debug_messenger);
    vkDestroyInstance(instance.instance, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();
}
