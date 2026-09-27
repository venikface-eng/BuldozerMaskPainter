#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace DayZBuldozer
{
    struct TerrainLayer
    {
        std::string name;
        uint8_t r{ 0 };
        uint8_t g{ 0 };
        uint8_t b{ 0 };

        uint32_t GetColorU32() const
        {
            // RGBA format for DirectX / ImGui: 0xAABBGGRR
            return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
        }

        void GetColorFloat(float outColor[4]) const
        {
            outColor[0] = static_cast<float>(r) / 255.0f;
            outColor[1] = static_cast<float>(g) / 255.0f;
            outColor[2] = static_cast<float>(b) / 255.0f;
            outColor[3] = 1.0f;
        }

        std::string GetDisplayName() const
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (RGB: %d, %d, %d)", name.c_str(), r, g, b);
            return std::string(buf);
        }
    };

    class LayersParser
    {
    public:
        LayersParser();

        // Load and parse layers.cfg from a file path
        bool LoadFromFile(const std::string& filePath);

        // Parse content directly from string
        bool ParseContent(const std::string& content);

        // Get list of parsed layers
        const std::vector<TerrainLayer>& GetLayers() const { return m_layers; }

        // Find layer by name
        const TerrainLayer* FindByName(const std::string& name) const;

        // Find layer by RGB color
        const TerrainLayer* FindByColor(uint8_t r, uint8_t g, uint8_t b) const;

        // Find closest layer by RGB color within max distance squared
        const TerrainLayer* FindClosestLayer(uint8_t r, uint8_t g, uint8_t b, int maxDistSq = 50) const;

        // Add a layer manually
        void AddLayer(const std::string& name, uint8_t r, uint8_t g, uint8_t b);

        // Add default Bohemia / DayZ surface layers as fallback
        void LoadDefaults();

        // Get file path of currently loaded layers.cfg
        const std::string& GetLoadedFilePath() const { return m_loadedFilePath; }

        // Number of layers
        size_t GetCount() const { return m_layers.size(); }

    private:
        std::string CleanComments(const std::string& source);
        std::vector<TerrainLayer> m_layers;
        std::string m_loadedFilePath;
    };

    inline LayersParser g_LayersParser;
}
