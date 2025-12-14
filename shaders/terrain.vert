#version 450

layout(location = 0) in vec2 inPos; // Grid position (u, v)

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
    int vizMode;
} ubo;

// We need to sample the heightmap in the vertex shader
// This requires the sampler to be available.
// We will use the same descriptor set layout as visualization, 
// binding 0 is heightmap.
layout(set = 1, binding = 0) uniform sampler2D heightMap;

void main() {
    outUV = inPos;
    
    // Sample height
    // Note: Vertex Texture Fetch. Ensure texture is accessible.
    float height = texture(heightMap, inPos).r;
    outHeight = height;
    
    // Scale height for visualization
    float heightScale = 0.22; 
    
    vec3 localPos = vec3(inPos.x - 0.5, height * heightScale, inPos.y - 0.5);
    
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(localPos, 1.0);
    
    // Pass world position to fragment shader for lighting/fog
    outWorldPos = (ubo.model * vec4(localPos, 1.0)).xyz;
}
