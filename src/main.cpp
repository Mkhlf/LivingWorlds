#include "living_worlds.hpp"
#include <cstring>
#include <cstdlib>

// Helper functions for CLI parsing
bool hasArg(int argc, char* argv[], const char* arg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], arg) == 0) return true;
    }
    return false;
}

int getArgInt(int argc, char* argv[], const char* arg, int defaultVal) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], arg) == 0) {
            return atoi(argv[i + 1]);
        }
    }
    return defaultVal;
}

float getArgFloat(int argc, char* argv[], const char* arg, float defaultVal) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], arg) == 0) {
            return static_cast<float>(atof(argv[i + 1]));
        }
    }
    return defaultVal;
}

void printUsage() {
    std::cout << "Usage: LivingWorlds [options]\n"
              << "Options:\n"
              << "  --benchmark       Enable benchmark mode (auto-exit, CSV logging)\n"
              << "  --grid SIZE       Set grid size (default: 3072)\n"
              << "  --duration SECS   Benchmark duration in seconds (default: 30)\n"
              << "  --speed MULT      Simulation speed multiplier (default: 1.0)\n"
              << "  --no-erosion      Disable erosion simulation\n"
              << "  --no-biome        Disable biome CA simulation\n"
              << "  --help            Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (hasArg(argc, argv, "--help")) {
        printUsage();
        return 0;
    }
    
    ProfileConfig config;
    config.benchmarkMode = hasArg(argc, argv, "--benchmark");
    config.gridSize = getArgInt(argc, argv, "--grid", 3072);
    config.duration = getArgInt(argc, argv, "--duration", 30);
    config.simSpeed = getArgFloat(argc, argv, "--speed", 1.0f);
    config.enableErosion = !hasArg(argc, argv, "--no-erosion");
    config.enableBiomeCA = !hasArg(argc, argv, "--no-biome");
    
    if (config.benchmarkMode) {
        std::cout << "=== BENCHMARK MODE ===\n"
                  << "Grid: " << config.gridSize << "x" << config.gridSize << "\n"
                  << "Duration: " << config.duration << "s\n"
                  << "Speed: " << config.simSpeed << "x\n"
                  << "Erosion: " << (config.enableErosion ? "ON" : "OFF") << "\n"
                  << "BiomeCA: " << (config.enableBiomeCA ? "ON" : "OFF") << "\n"
                  << "======================\n";
    }
    
    LivingWorlds app(config);
    app.run();
    return 0;
}
