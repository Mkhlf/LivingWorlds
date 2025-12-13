#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inHeight;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
    int vizMode;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 1) uniform usampler2D biomeMap; // R8_UINT

// Biome IDs
const uint WATER  = 0u;
const uint SAND   = 1u;
const uint GRASS  = 2u;
const uint FOREST = 3u;
const uint DESERT = 4u;
const uint ROCK   = 5u;
const uint SNOW   = 6u;
const uint TUNDRA = 7u;

// Biome Colors (more distinct)
const vec3 biomeColors[8] = vec3[](
    vec3(0.1, 0.3, 0.8),  // Water - Deep Blue
    vec3(0.95, 0.9, 0.7), // Sand - Light Beige (coastal)
    vec3(0.3, 0.6, 0.3),  // Grass - Green
    vec3(0.1, 0.35, 0.1), // Forest - Dark Green
    vec3(0.85, 0.55, 0.3),// Desert - ORANGE (distinct from sand!)
    vec3(0.45, 0.42, 0.4),// Rock - Dark Gray
    vec3(0.95, 0.97, 1.0),// Snow - Bright White
    vec3(0.55, 0.6, 0.55) // Tundra - Greenish Gray
);

void main() {
    float h = texture(heightMap, inUV).r;
    uint biome = texture(biomeMap, inUV).r;
    
    // Use discrete biome colors
    vec3 color = biomeColors[min(biome, 7u)];
    
    // Lighting
    vec3 X = dFdx(inWorldPos);
    vec3 Y = dFdy(inWorldPos);
    vec3 normal = normalize(cross(X, Y));
    
    vec3 lightDir = normalize(vec3(-1.0, -2.0, -1.0));
    float diff = max(dot(normal, -lightDir), 0.0);
    
    vec3 ambient = vec3(0.3);
    vec3 finalColor = color * (ambient + diff);
    
    outColor = vec4(finalColor, 1.0);
}
