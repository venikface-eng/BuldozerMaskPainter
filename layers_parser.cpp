#include "layers_parser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>

namespace DayZBuldozer
{
    LayersParser::LayersParser()
    {
        LoadDefaults();
    }

    std::string LayersParser::CleanComments(const std::string& source)
    {
        std::string result;
        result.reserve(source.size());

        size_t i = 0;
        const size_t len = source.size();

        while (i < len)
        {
            // Single-line comment: //
            if (i + 1 < len && source[i] == '/' && source[i + 1] == '/')
            {
                i += 2;
                while (i < len && source[i] != '\n' && source[i] != '\r')
                {
                    i++;
                }
            }
            // Multi-line comment: /* ... */
            else if (i + 1 < len && source[i] == '/' && source[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < len && !(source[i] == '*' && source[i + 1] == '/'))
                {
                    i++;
                }
                if (i + 1 < len) i += 2;
            }
            else
            {
                result.push_back(source[i]);
                i++;
            }
        }

        return result;
    }

    bool LayersParser::LoadFromFile(const std::string& filePath)
    {
        std::vector<std::string> pathsToTry;
        if (!filePath.empty()) pathsToTry.push_back(filePath);
        pathsToTry.push_back("BuldozerMaskPainter\\info\\layers.cfg");
        pathsToTry.push_back("info\\layers.cfg");
        pathsToTry.push_back("layers.cfg");
        pathsToTry.push_back("C:\\Program Files (x86)\\Steam\\steamapps\\common\\DayZ\\BuldozerMaskPainter\\info\\layers.cfg");
        pathsToTry.push_back("P:\\dz\\surfaces\\layers.cfg");

        for (const auto& candidate : pathsToTry)
        {
            std::ifstream file(candidate);
            if (file.is_open())
            {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();

                if (ParseContent(buffer.str()))
                {
                    m_loadedFilePath = candidate;
                    return true;
                }
            }
        }

        return false;
    }

    bool LayersParser::ParseContent(const std::string& content)
    {
        std::string clean = CleanComments(content);

        // Parse individual layer definitions:
        // Examples:
        // cp_grass[] = {{82,90,76}};
        // sk_snow[] = {{210, 210, 210}};
        // concrete1[] = { 255, 0, 255 };
        std::regex entryRegex(
            R"(([a-zA-Z0-9_\-]+)\s*\[\s*\]\s*=\s*\{\s*\{?\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}?\s*\}\s*;)",
            std::regex::icase
        );

        std::vector<TerrainLayer> parsedLayers;
        auto words_begin = std::sregex_iterator(clean.begin(), clean.end(), entryRegex);
        auto words_end = std::sregex_iterator();

        for (std::sregex_iterator it = words_begin; it != words_end; ++it)
        {
            std::smatch match = *it;
            std::string name = match[1].str();
            int r = std::stoi(match[2].str());
            int g = std::stoi(match[3].str());
            int b = std::stoi(match[4].str());

            TerrainLayer layer;
            layer.name = name;
            layer.r = static_cast<uint8_t>(std::clamp(r, 0, 255));
            layer.g = static_cast<uint8_t>(std::clamp(g, 0, 255));
            layer.b = static_cast<uint8_t>(std::clamp(b, 0, 255));

            parsedLayers.push_back(layer);
        }

        if (!parsedLayers.empty())
        {
            m_layers = std::move(parsedLayers);
            return true;
        }

        return false;
    }

    const TerrainLayer* LayersParser::FindByName(const std::string& name) const
    {
        for (const auto& layer : m_layers)
        {
            if (_stricmp(layer.name.c_str(), name.c_str()) == 0)
                return &layer;
        }
        return nullptr;
    }

    const TerrainLayer* LayersParser::FindByColor(uint8_t r, uint8_t g, uint8_t b) const
    {
        for (const auto& layer : m_layers)
        {
            if (layer.r == r && layer.g == g && layer.b == b)
                return &layer;
        }
        return nullptr;
    }

    const TerrainLayer* LayersParser::FindClosestLayer(uint8_t r, uint8_t g, uint8_t b, int maxDistSq) const
    {
        const TerrainLayer* exact = FindByColor(r, g, b);
        if (exact) return exact;

        const TerrainLayer* best = nullptr;
        int minDiffSq = maxDistSq + 1;
        for (const auto& layer : m_layers)
        {
            int dr = static_cast<int>(r) - static_cast<int>(layer.r);
            int dg = static_cast<int>(g) - static_cast<int>(layer.g);
            int db = static_cast<int>(b) - static_cast<int>(layer.b);
            int diffSq = dr * dr + dg * dg + db * db;
            if (diffSq < minDiffSq)
            {
                minDiffSq = diffSq;
                best = &layer;
            }
        }
        return best;
    }

    void LayersParser::AddLayer(const std::string& name, uint8_t r, uint8_t g, uint8_t b)
    {
        for (auto& layer : m_layers)
        {
            if (_stricmp(layer.name.c_str(), name.c_str()) == 0)
            {
                layer.r = r;
                layer.g = g;
                layer.b = b;
                return;
            }
        }

        TerrainLayer layer;
        layer.name = name;
        layer.r = r;
        layer.g = g;
        layer.b = b;
        m_layers.push_back(layer);
    }

    void LayersParser::LoadDefaults()
    {
        m_layers.clear();
        // Standard DayZ / Chernarus / Livonia terrain surface layers
        AddLayer("grass_green",       0,   255, 0);
        AddLayer("forest_conifer",    0,   128, 0);
        AddLayer("forest_deciduous",  34,  177, 76);
        AddLayer("soil",              185, 122, 87);
        AddLayer("dirt",              136, 0,   21);
        AddLayer("gravel",            128, 128, 128);
        AddLayer("asphalt",           64,  64,  64);
        AddLayer("concrete1",         255, 0,   255);
        AddLayer("rock",              195, 195, 195);
        AddLayer("sand",              255, 242, 0);
    }
}
