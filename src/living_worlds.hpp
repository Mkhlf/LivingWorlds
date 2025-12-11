#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h> // Include GLFW

#include <vector>
#include <iostream>

class LivingWorlds {
public:
    void run();

private:
    void init();
    void main_loop();
    void cleanup();

    void init_window();
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_default_renderpass();
    void init_framebuffers();
    void init_sync_structures();

    void draw();

    // Window
    GLFWwindow* window{nullptr};
    const uint32_t width = 1024;
    const uint32_t height = 1024;

    // Vulkan Core
    vkb::Instance instance;
    VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    vkb::PhysicalDevice physical_device;
    vkb::Device device; // Logical device
    
    VkQueue graphics_queue{VK_NULL_HANDLE};
    uint32_t graphics_queue_family;

    // Swapchain
    vkb::Swapchain swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat swapchain_image_format;

    // Render structures
    VkRenderPass render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers;

    // Commands
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkCommandBuffer main_command_buffer{VK_NULL_HANDLE};

    // Sync
    VkSemaphore present_semaphore{VK_NULL_HANDLE};
    VkSemaphore render_semaphore{VK_NULL_HANDLE};
    VkFence render_fence{VK_NULL_HANDLE};

    // VMA
    VmaAllocator allocator{VK_NULL_HANDLE};
};
