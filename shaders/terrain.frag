#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inHeight;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 invView;  // Pre-computed inverse view
    float time;
    int vizMode;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 1) uniform usampler2D biomeMap;

// Biome IDs
const uint WATER  = 0u;
const uint SAND   = 1u;
const uint GRASS  = 2u;
const uint FOREST = 3u;
const uint DESERT  = 4u;
const uint ROCK    = 5u;
const uint SNOW    = 6u;
const uint TUNDRA  = 7u;
const uint WETLAND = 8u;

// Biome Base Colors (light variants - high elevation)
const vec3 biomeColorsLight[9] = vec3[](
    vec3(0.2, 0.45, 0.9),  // Water - Bright Blue
    vec3(1.0, 0.95, 0.8),  // Sand - Light Cream
    vec3(0.45, 0.75, 0.35),// Grass - Bright Green
    vec3(0.2, 0.5, 0.15),  // Forest - Medium Green
    vec3(0.95, 0.7, 0.45), // Desert - Light Orange
    vec3(0.6, 0.58, 0.55), // Rock - Light Gray
    vec3(1.0, 1.0, 1.0),   // Snow - Pure White
    vec3(0.7, 0.6, 0.45),  // Tundra - Light Brown/Tan (alpine meadow)
    vec3(0.25, 0.55, 0.45) // Wetland - Teal Green
);

// Biome Base Colors (dark variants - low elevation)
const vec3 biomeColorsDark[9] = vec3[](
    vec3(0.05, 0.15, 0.5), // Water - Deep Blue
    vec3(0.8, 0.7, 0.5),   // Sand - Dark Tan
    vec3(0.15, 0.4, 0.15), // Grass - Dark Green
    vec3(0.05, 0.2, 0.05), // Forest - Very Dark Green
    vec3(0.6, 0.35, 0.2),  // Desert - Dark Brown
    vec3(0.25, 0.23, 0.22),// Rock - Dark Gray
    vec3(0.85, 0.88, 0.95),// Snow - Slight Blue Tint
    vec3(0.5, 0.4, 0.3),   // Tundra - Dark Brown/Tan
    vec3(0.1, 0.35, 0.3)   // Wetland - Dark Teal Green
);

// Hash function for procedural noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Multi-octave noise
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) {
        v += a * noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void main() {
    float h = texture(heightMap, inUV).r;
    uint biome = texture(biomeMap, inUV).r;
    biome = min(biome, 8u);
    
    // === PROCEDURAL NOISE VARIATION ===
    float noiseVal = fbm(inUV * 50.0);
    float fineNoise = noise(inUV * 200.0);
    
    // === HEIGHT-BASED COLOR GRADIENT ===
    // Normalize height to 0-1 range for color blending
    float normalizedH = clamp((h - 0.2) / 0.6, 0.0, 1.0);
    vec3 baseColor = mix(biomeColorsDark[biome], biomeColorsLight[biome], normalizedH);
    
    // Add procedural noise (±8% variation)
    vec3 noiseOffset = vec3(noiseVal * 0.08 - 0.04);
    noiseOffset += vec3(fineNoise * 0.04 - 0.02);
    baseColor += noiseOffset;
    
    // === CALCULATE NORMALS ===
    vec3 X = dFdx(inWorldPos);
    vec3 Y = dFdy(inWorldPos);
    vec3 normal = normalize(cross(X, Y));
    
    // === SLOPE-BASED DARKENING ===
    float slope = 1.0 - abs(normal.y); // 0 = flat, 1 = vertical
    baseColor *= (1.0 - slope * 0.25); // Darker on steep slopes
    
    // === LIGHTING ===
    vec3 lightDir = normalize(vec3(-0.7, -1.0, -0.5));
    float diff = max(dot(normal, -lightDir), 0.0);
    
    // Ambient with vertical gradient (sky light)
    float skyAmbient = 0.25 + 0.1 * normal.y;
    vec3 ambient = vec3(skyAmbient);
    
    vec3 finalColor = baseColor * (ambient + diff * 0.8);
    
    // === SPECULAR FOR WATER AND SNOW ===
    if (biome == WATER || biome == SNOW) {
        mat4 invView = inverse(ubo.view);
        vec3 cameraPos = vec3(invView[3][0], invView[3][1], invView[3][2]);
        vec3 viewDir = normalize(cameraPos - inWorldPos);
        vec3 halfDir = normalize(-lightDir + viewDir);
        
        float shininess = (biome == WATER) ? 64.0 : 16.0;
        float specStrength = (biome == WATER) ? 0.6 : 0.3;
        float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
        finalColor += vec3(spec * specStrength);
        
        // Water has slight animation
        if (biome == WATER) {
            float wave = sin(inUV.x * 100.0 + ubo.time * 2.0) * 0.02;
            finalColor += vec3(wave);
        }
    }
    
    // === BIOME EDGE DETECTION ===
    vec2 texelSize = vec2(1.0 / 3072.0);
    uint biomeN = texture(biomeMap, inUV + vec2(0, texelSize.y)).r;
    uint biomeS = texture(biomeMap, inUV - vec2(0, texelSize.y)).r;
    uint biomeE = texture(biomeMap, inUV + vec2(texelSize.x, 0)).r;
    uint biomeW = texture(biomeMap, inUV - vec2(texelSize.x, 0)).r;
    
    bool isEdge = (biome != biomeN || biome != biomeS || biome != biomeE || biome != biomeW);
    if (isEdge) {
        finalColor *= 0.92; // Subtle darkening at biome boundaries
    }
    
    // === SKY GRADIENT + ATMOSPHERIC DEPTH ===
    // Use pre-computed invView from UBO (optimized - no per-fragment inverse)
    vec3 cameraPos = vec3(ubo.invView[3][0], ubo.invView[3][1], ubo.invView[3][2]);
    float dist = length(inWorldPos - cameraPos);
    
    // View direction for sky gradient
    vec3 viewDir = normalize(inWorldPos - cameraPos);
    float skyFactor = clamp(-viewDir.y * 2.0 + 0.3, 0.0, 1.0); // Higher = more sky
    
    // Sky colors (zenith to horizon) - darker, more saturated
    vec3 skyZenith = vec3(0.25, 0.45, 0.75);  // Deep blue overhead
    vec3 skyHorizon = vec3(0.35, 0.50, 0.70); // Medium blue at horizon (matches clear color)
    vec3 groundHaze = vec3(0.40, 0.55, 0.70); // Ground haze
    
    // Blend sky gradient
    vec3 skyColor = mix(groundHaze, mix(skyHorizon, skyZenith, skyFactor), skyFactor);
    
    // Distance-based fog blending (parallax effect - distant = more sky)
    float fogStart = 0.8;
    float fogEnd = 2.5;
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 0.7);
    
    // Apply atmospheric perspective (distant objects fade to sky)
    finalColor = mix(finalColor, skyColor, fogFactor);
    
    outColor = vec4(finalColor, 1.0);
}

