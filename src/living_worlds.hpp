#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct PushConsts {
    float seed;
};

struct BiomePushConstants {
    float forestChance = 0.3f;  // Seeding density (0.3 = 3% of cells)
    float desertChance = 0.3f;  // EQUAL to forest for balance
    int forestThreshold = 3;    // Neighbors needed to spread
    int desertThreshold = 3;    // SAME as forest for symmetry
    float time = 0.0f;          // Simulation step counter
};

static constexpr float SEED = 42.0f; // Default Seed

struct Vertex {
    glm::vec2 pos;
    // We don't need color or texCoord explicitly for this grid, 
    // we can calculate UV from pos (0..1 range).
    // But let's keep it simple: just Pos (X, Z).
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(1);
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        return attributeDescriptions;
    }
};

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    float time;
    int vizMode; // 0=Default, 1=Temp, 2=Hum
};

class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;
    float movementSpeed;
    float mouseSensitivity;

    Camera() 
        : position(0.0f, 0.5f, 0.0f), 
          front(0.0f, 0.0f, -1.0f), 
          up(0.0f, 1.0f, 0.0f), 
          worldUp(0.0f, 1.0f, 0.0f),
          yaw(-90.0f), 
          pitch(0.0f), 
          movementSpeed(2.0f), 
          mouseSensitivity(0.1f) 
    {
        updateCameraVectors();
    }

    glm::mat4 getViewMatrix() {
        return glm::lookAt(position, position + front, up);
    }

    void updateCameraVectors() {
        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
        right = glm::normalize(glm::cross(front, worldUp));
        up    = glm::normalize(glm::cross(right, front));
    }
};
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
    void transition_image_layout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    // Window
    GLFWwindow* window{nullptr};
    const uint32_t width = 2048;  // Increased to 2048 (Small Game Size)
    const uint32_t height = 2048;
    size_t current_frame = 0;
    
    // FPS Counting
    double last_timestamp = 0.0;
    int frames_this_second = 0;
    
    // Visualization State
    int vizMode = 0; // 0=Default, 1=Temp, 2=Hum

    // Simulation Control
    float simAccumulator = 0.0f;
    float simInterval = 0.1f; // Default ~10 updates/sec
    float currentSeed = 42.0f; // Seed for terrain generation
    double lastFrameTime = 0.0;

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
    static const int MAX_FRAMES_IN_FLIGHT = 3;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;

    // Compute Resources
    VkDescriptorSetLayout compute_descriptor_layout{VK_NULL_HANDLE};
    VkPipelineLayout compute_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline compute_pipeline{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    
    // Input State
    bool firstMouse = true;
    double lastX = 0.0;
    double lastY = 0.0;
    
    void process_input(float deltaTime);
    static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
    void handle_mouse(double xpos, double ypos);
    
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
    
    // Discrete Biome Resources (Week 5.5) (R8_UINT)
    VkImage biome_images[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation biome_allocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView biome_views[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    
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
    
    // Discrete Biome CA (Week 5.5)
    VkPipelineLayout biome_ca_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline biome_ca_pipeline{VK_NULL_HANDLE};
    BiomePushConstants biomePushConstants;
    void init_biome_ca_pipeline();
    void dispatch_biome_ca_init();
    
    // 2.5D Rendering Resources
    Camera camera;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformBuffersAllocation;
    std::vector<void*> uniformBuffersMapped;
    
    VkImage depthImage;
    VmaAllocation depthImageAllocation;
    VkImageView depthImageView;
    
    void create_grid_mesh();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_uniform_buffers();
    void create_depth_resources();
    void update_uniform_buffer(uint32_t currentImage);
    
    // Helpers for buffers
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer, VmaAllocation& allocation);
    void copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    
    // Helper for Depth Format
    VkFormat find_depth_format();

    // Visualization
    VkPipelineLayout viz_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline viz_pipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout viz_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool viz_descriptor_pool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> viz_descriptor_sets;
    void init_viz_pipeline();
    void update_viz_descriptors();
    
    // 2.5D Terrain Pipeline
    VkPipelineLayout terrain_pipeline_layout{VK_NULL_HANDLE};
    VkPipeline terrain_pipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout ubo_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool ubo_descriptor_pool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> ubo_descriptor_sets;
    
    VkDescriptorSetLayout texture_descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool texture_descriptor_pool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> texture_descriptor_sets;
    VkSampler textureSampler{VK_NULL_HANDLE};
    
    void init_terrain_pipeline();
    void create_ubo_descriptors();
    void create_texture_descriptors();
    
    // Helpers
    bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
    void create_storage_image(VkImage& image, VmaAllocation& alloc, VkImageView& view, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
};
