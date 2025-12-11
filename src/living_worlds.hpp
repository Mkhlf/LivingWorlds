#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>

enum class Pattern {
    Glider,
    GosperGliderGun,
    Random,
    RPentomino
};

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
    
    // Compute specific
    void init_descriptors();
    void init_compute_pipeline();
    void init_storage_images();

    void draw();
    
    // Helper to clear/initialize grid
    void initialize_grid_pattern(Pattern pattern);

    // Window
    GLFWwindow* window{nullptr};
    const uint32_t width = 1024;
    const uint32_t height = 1024;
    size_t current_frame = 0;
    
    // FPS Counting
    double last_timestamp = 0.0;
    int frames_this_second = 0;

    // Vulkan Core
    vkb::Instance instance;
    VkDebugUtilsMessengerEXT debug_messenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    vkb::PhysicalDevice physical_device;
    vkb::Device device;
    VkQueue graphics_queue{VK_NULL_HANDLE};
    uint32_t graphics_queue_family;

    // VMA
    VmaAllocator allocator{VK_NULL_HANDLE};

    // Swapchain
    vkb::Swapchain swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkFormat swapchain_image_format;

    // Render structures use for clear pass
    VkRenderPass render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers;

    // Commands
    VkCommandPool command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers;

    // Sync
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;

    // Compute Resources
    VkDescriptorSetLayout compute_descriptor_layout{VK_NULL_HANDLE};
    VkPipelineLayout compute_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline compute_pipeline{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    
    // Ping-Pong Buffers
    std::vector<VkDescriptorSet> compute_descriptor_sets;
    VkImage storage_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation storage_image_allocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView storage_image_views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    
    size_t current_sim_output_index = 0;

    // Heightmap Resources (Week 3) (R32_SFLOAT)
    VkImage heightmap_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation heightmap_allocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView heightmap_views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    
    // Biome Resources (Week 4) (R32_SFLOAT)
    VkImage temp_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation temp_allocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView temp_views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    
    VkImage humidity_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation humidity_allocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView humidity_views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    
    size_t current_heightmap_index = 0;

    // Heightmap/Biome Initialization
    VkPipelineLayout noise_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline noise_pipeline{VK_NULL_HANDLE};
    void init_noise_pipeline();
    void dispatch_noise_init();
    
    VkPipelineLayout biome_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline biome_pipeline{VK_NULL_HANDLE};
    void init_biome_pipeline();
    void dispatch_biome_init();
    
    // Biome Growth
    VkPipelineLayout biome_growth_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline biome_growth_pipeline{VK_NULL_HANDLE};
    void init_biome_growth_pipeline();
    
    // Erosion
    VkPipelineLayout erosion_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline erosion_pipeline{VK_NULL_HANDLE};
    void init_erosion_pipeline();

    // Visualization
    VkPipelineLayout viz_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline viz_pipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout viz_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool viz_descriptor_pool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> viz_descriptor_sets;
    void init_viz_pipeline();
    void update_viz_descriptors(int image_index); // Update specific set for swapchain image

    // Helpers
    bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
    void create_storage_image(VkImage& image, VmaAllocation& alloc, VkImageView& view, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
};
