#include "mask_painter.h"

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb/stb_image_write.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace DayZBuldozer
{
    MaskPainter::MaskPainter()
    {
        m_minimapPixels.resize(MINIMAP_TEX_DIM * MINIMAP_TEX_DIM, 0xFF333333);
    }

    MaskPainter::~MaskPainter()
    {
        InvalidateTextures();
    }

    void MaskPainter::InvalidateTextures()
    {
        if (m_pMinimapSRV)
        {
            m_pMinimapSRV->Release();
            m_pMinimapSRV = nullptr;
        }
        if (m_pMinimapTexture)
        {
            m_pMinimapTexture->Release();
            m_pMinimapTexture = nullptr;
        }
    }

    void MaskPainter::SetPalette(const std::vector<TerrainLayer>& layers)
    {
        m_palette = layers;
        if (m_palette.empty())
        {
            m_palette.push_back({ "cp_grass", 82, 90, 76 });
            m_palette.push_back({ "cp_dirt", 131, 58, 22 });
            m_palette.push_back({ "ground_gravel", 255, 211, 1 });
            m_palette.push_back({ "cp_concrete1", 32, 1, 255 });
        }
        m_minimapDirty = true;
    }

    TerrainLayer MaskPainter::GetPaletteLayer(uint8_t index) const
    {
        if (index < m_palette.size())
            return m_palette[index];
        if (!m_palette.empty())
            return m_palette[0];
        return TerrainLayer{ "unknown", 128, 128, 128 };
    }

    uint8_t MaskPainter::FindClosestLayerIndex(uint8_t r, uint8_t g, uint8_t b) const
    {
        if (m_palette.empty()) return 0;

        uint8_t bestIdx = 0;
        int minDiffSq = 99999999;

        for (size_t i = 0; i < m_palette.size(); ++i)
        {
            int dr = static_cast<int>(r) - static_cast<int>(m_palette[i].r);
            int dg = static_cast<int>(g) - static_cast<int>(m_palette[i].g);
            int db = static_cast<int>(b) - static_cast<int>(m_palette[i].b);
            int diffSq = dr * dr + dg * dg + db * db;
            if (diffSq < minDiffSq)
            {
                minDiffSq = diffSq;
                bestIdx = static_cast<uint8_t>(i);
                if (diffSq == 0) break;
            }
        }
        return bestIdx;
    }

    #pragma pack(push, 1)
    struct BMPFileHeader
    {
        uint16_t bfType;
        uint32_t bfSize;
        uint16_t bfReserved1;
        uint16_t bfReserved2;
        uint32_t bfOffBits;
    };

    struct BMPInfoHeader
    {
        uint32_t biSize;
        int32_t  biWidth;
        int32_t  biHeight;
        uint16_t biPlanes;
        uint16_t biBitCount;
        uint32_t biCompression;
        uint32_t biSizeImage;
        int32_t  biXPelsPerMeter;
        int32_t  biYPelsPerMeter;
        uint32_t biClrUsed;
        uint32_t biClrImportant;
    };

    struct BMPColorEntry
    {
        uint8_t rgbBlue;
        uint8_t rgbGreen;
        uint8_t rgbRed;
        uint8_t rgbReserved;
    };
    #pragma pack(pop)

    bool MaskPainter::LoadStandardBMP8Bit(const std::string& filePath, const std::vector<TerrainLayer>& activeLayers)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        BMPFileHeader fileHeader;
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        if (fileHeader.bfType != 0x4D42) return false;

        uint32_t biSize = 0;
        file.read(reinterpret_cast<char*>(&biSize), sizeof(uint32_t));
        if (biSize < 40) return false;

        int32_t width = 0;
        int32_t height = 0;
        uint16_t planes = 0;
        uint16_t bitCount = 0;
        uint32_t compression = 0;
        uint32_t sizeImage = 0;
        int32_t xPels = 0;
        int32_t yPels = 0;
        uint32_t clrUsed = 0;
        uint32_t clrImportant = 0;

        file.read(reinterpret_cast<char*>(&width), 4);
        file.read(reinterpret_cast<char*>(&height), 4);
        file.read(reinterpret_cast<char*>(&planes), 2);
        file.read(reinterpret_cast<char*>(&bitCount), 2);
        file.read(reinterpret_cast<char*>(&compression), 4);
        file.read(reinterpret_cast<char*>(&sizeImage), 4);
        file.read(reinterpret_cast<char*>(&xPels), 4);
        file.read(reinterpret_cast<char*>(&yPels), 4);
        file.read(reinterpret_cast<char*>(&clrUsed), 4);
        file.read(reinterpret_cast<char*>(&clrImportant), 4);

        if (bitCount != 8 || compression != 0)
        {
            file.close();
            return false;
        }

        if (width <= 0 || height == 0)
        {
            file.close();
            return false;
        }

        int realHeight = std::abs(height);
        bool isBottomUp = (height > 0);

        // Palette is located at 14 + biSize
        file.seekg(14 + biSize, std::ios::beg);

        uint32_t numColors = (clrUsed == 0 || clrUsed > 256) ? 256 : clrUsed;
        if (fileHeader.bfOffBits > (14 + biSize))
        {
            uint32_t availableColors = (fileHeader.bfOffBits - (14 + biSize)) / 4;
            if (availableColors < numColors && availableColors > 0)
            {
                numColors = availableColors;
            }
        }
        if (numColors > 256) numColors = 256;

        struct RawColorEntry { uint8_t b, g, r, reserved; };
        std::vector<RawColorEntry> rawPal(numColors);
        file.read(reinterpret_cast<char*>(rawPal.data()), numColors * sizeof(RawColorEntry));

        if (!file)
        {
            file.close();
            return false;
        }

        // Initialize palette from activeLayers (from layers.cfg)
        m_palette = activeLayers;
        if (m_palette.empty())
        {
            m_palette.push_back({ "cp_grass", 82, 90, 76 });
            m_palette.push_back({ "cp_dirt", 131, 58, 22 });
            m_palette.push_back({ "ground_gravel", 255, 211, 1 });
            m_palette.push_back({ "cp_concrete1", 32, 1, 255 });
        }

        // Build remap table: BMP raw palette index -> canonical m_palette index
        uint8_t remap[256];
        for (int i = 0; i < 256; ++i) remap[i] = 0;

        for (uint32_t c = 0; c < numColors; ++c)
        {
            uint8_t r = rawPal[c].r;
            uint8_t g = rawPal[c].g;
            uint8_t b = rawPal[c].b;

            // 1. Try exact match in m_palette
            int matchedIdx = -1;
            for (size_t i = 0; i < m_palette.size(); ++i)
            {
                if (m_palette[i].r == r && m_palette[i].g == g && m_palette[i].b == b)
                {
                    matchedIdx = static_cast<int>(i);
                    break;
                }
            }

            // 2. Try close match within RGB color distance squared <= 36 (tolerance +/- 6)
            if (matchedIdx < 0)
            {
                int bestDistSq = 999999;
                int bestIdx = -1;
                for (size_t i = 0; i < m_palette.size(); ++i)
                {
                    int dr = static_cast<int>(r) - static_cast<int>(m_palette[i].r);
                    int dg = static_cast<int>(g) - static_cast<int>(m_palette[i].g);
                    int db = static_cast<int>(b) - static_cast<int>(m_palette[i].b);
                    int dSq = dr * dr + dg * dg + db * db;
                    if (dSq < bestDistSq)
                    {
                        bestDistSq = dSq;
                        bestIdx = static_cast<int>(i);
                    }
                }
                if (bestDistSq <= 36)
                {
                    matchedIdx = bestIdx;
                }
            }

            // 3. If completely distinct color, add as new layer in m_palette if space permits
            if (matchedIdx < 0)
            {
                if (m_palette.size() < 255)
                {
                    matchedIdx = static_cast<int>(m_palette.size());
                    m_palette.push_back({ "Layer_" + std::to_string(c), r, g, b });
                }
                else
                {
                    matchedIdx = FindClosestLayerIndex(r, g, b);
                }
            }

            remap[c] = static_cast<uint8_t>(matchedIdx);
        }

        // Seek to exact pixel data offset from header
        file.clear();
        file.seekg(fileHeader.bfOffBits, std::ios::beg);

        uint32_t rowStride = ((width * 8 + 31) / 32) * 4;
        std::vector<uint8_t> rowBuffer(rowStride);

        m_width = width;
        m_height = realHeight;
        m_currentFilePath = filePath;

        size_t totalPixels = static_cast<size_t>(width) * realHeight;
        m_baseIndices.resize(totalPixels);
        m_overlayIndices.assign(totalPixels, EMPTY_OVERLAY);
        m_overlayPixelCount = 0;

        for (int row = 0; row < realHeight; ++row)
        {
            file.read(reinterpret_cast<char*>(rowBuffer.data()), rowStride);
            if (!file) break;

            int dstZ = isBottomUp ? row : (realHeight - 1 - row);
            size_t dstRowStart = static_cast<size_t>(dstZ) * width;
            uint8_t* dst = &m_baseIndices[dstRowStart];

            for (int x = 0; x < width; ++x)
            {
                dst[x] = remap[rowBuffer[x]];
            }
        }

        file.close();
        m_undoStack.clear();
        m_currentStrokeTouched.clear();
        m_minimapDirty = true;
        return true;
    }

    bool MaskPainter::LoadBaseMask(const std::string& filePath, const std::vector<TerrainLayer>& activeLayers)
    {
        if (LoadStandardBMP8Bit(filePath, activeLayers))
        {
            return true;
        }

        // Fallback: 24/32-bit BMP or PNG via STB
        int w = 0, h = 0, channels = 0;
        uint8_t* data = stbi_load(filePath.c_str(), &w, &h, &channels, 3);
        if (!data) return false;

        m_width = w;
        m_height = h;
        m_currentFilePath = filePath;
        SetPalette(activeLayers);

        size_t totalPixels = static_cast<size_t>(w) * h;
        m_baseIndices.resize(totalPixels);
        m_overlayIndices.assign(totalPixels, EMPTY_OVERLAY);
        m_overlayPixelCount = 0;

        for (int y = 0; y < h; ++y)
        {
            int z = (h - 1 - y);
            size_t dstRow = static_cast<size_t>(z) * w;
            size_t srcRow = static_cast<size_t>(y) * w * 3;

            for (int x = 0; x < w; ++x)
            {
                uint8_t r = data[srcRow + x * 3 + 0];
                uint8_t g = data[srcRow + x * 3 + 1];
                uint8_t b = data[srcRow + x * 3 + 2];
                m_baseIndices[dstRow + x] = FindClosestLayerIndex(r, g, b);
            }
        }

        stbi_image_free(data);
        m_undoStack.clear();
        m_currentStrokeTouched.clear();
        m_minimapDirty = true;
        return true;
    }

    bool MaskPainter::CreateNewMask(int width, int height, uint8_t baseLayerIndex, const std::vector<TerrainLayer>& activeLayers)
    {
        if (width <= 0 || height <= 0) return false;

        m_width = width;
        m_height = height;
        SetPalette(activeLayers);

        size_t totalPixels = static_cast<size_t>(width) * height;
        m_baseIndices.assign(totalPixels, baseLayerIndex);
        m_overlayIndices.assign(totalPixels, EMPTY_OVERLAY);
        m_overlayPixelCount = 0;

        m_undoStack.clear();
        m_currentStrokeTouched.clear();
        m_minimapDirty = true;
        return true;
    }

    void MaskPainter::ClearOverlay()
    {
        if (!IsLoaded()) return;

        m_overlayIndices.assign(m_overlayIndices.size(), EMPTY_OVERLAY);
        m_overlayPixelCount = 0;
        m_undoStack.clear();
        m_currentStrokeTouched.clear();
        m_minimapDirty = true;
    }

    bool MaskPainter::SaveMergedMaskBMP(const std::string& filePath)
    {
        if (!IsLoaded()) return false;

        std::string target = filePath.empty() ? m_currentFilePath : filePath;
        if (target.empty()) target = "mask_merged.bmp";

        std::ofstream file(target, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t rowStride = ((m_width * 8 + 31) / 32) * 4;
        uint32_t imageSize = rowStride * m_height;
        uint32_t headerAndPaletteSize = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + 256 * sizeof(BMPColorEntry);
        uint32_t totalFileSize = headerAndPaletteSize + imageSize;

        BMPFileHeader fileHeader;
        fileHeader.bfType = 0x4D42;
        fileHeader.bfSize = totalFileSize;
        fileHeader.bfReserved1 = 0;
        fileHeader.bfReserved2 = 0;
        fileHeader.bfOffBits = headerAndPaletteSize;

        BMPInfoHeader infoHeader;
        infoHeader.biSize = sizeof(BMPInfoHeader);
        infoHeader.biWidth = m_width;
        infoHeader.biHeight = m_height;
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 8;
        infoHeader.biCompression = 0;
        infoHeader.biSizeImage = imageSize;
        infoHeader.biXPelsPerMeter = 2835;
        infoHeader.biYPelsPerMeter = 2835;
        infoHeader.biClrUsed = 256;
        infoHeader.biClrImportant = 256;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        BMPColorEntry paletteEntries[256] = {};
        for (size_t i = 0; i < 256; ++i)
        {
            if (i < m_palette.size())
            {
                paletteEntries[i].rgbRed = m_palette[i].r;
                paletteEntries[i].rgbGreen = m_palette[i].g;
                paletteEntries[i].rgbBlue = m_palette[i].b;
                paletteEntries[i].rgbReserved = 0;
            }
        }
        file.write(reinterpret_cast<const char*>(paletteEntries), sizeof(paletteEntries));

        // Stream merged rows (overlay baked onto base)
        std::vector<uint8_t> rowBuffer(rowStride, 0);
        for (int z = 0; z < m_height; ++z)
        {
            size_t rowOffset = static_cast<size_t>(z) * m_width;
            for (int x = 0; x < m_width; ++x)
            {
                uint8_t over = m_overlayIndices[rowOffset + x];
                rowBuffer[x] = (over != EMPTY_OVERLAY) ? over : m_baseIndices[rowOffset + x];
            }
            file.write(reinterpret_cast<const char*>(rowBuffer.data()), rowStride);
        }

        file.close();
        m_currentFilePath = target;
        return true;
    }

    bool MaskPainter::SaveOverlayPNG(const std::string& filePath)
    {
        if (!IsLoaded()) return false;

        std::string target = filePath.empty() ? "mask_overlay.png" : filePath;

        // Allocate 32-bit RGBA buffer for STB write
        size_t totalBytes = static_cast<size_t>(m_width) * m_height * 4;
        std::vector<uint8_t> rgba(totalBytes, 0);

        for (int z = 0; z < m_height; ++z)
        {
            // PNG is stored top-to-bottom
            int y = (m_height - 1 - z);
            size_t srcRow = static_cast<size_t>(z) * m_width;
            size_t dstRow = static_cast<size_t>(y) * m_width * 4;

            for (int x = 0; x < m_width; ++x)
            {
                uint8_t over = m_overlayIndices[srcRow + x];
                if (over != EMPTY_OVERLAY)
                {
                    TerrainLayer l = GetPaletteLayer(over);
                    rgba[dstRow + x * 4 + 0] = l.r;
                    rgba[dstRow + x * 4 + 1] = l.g;
                    rgba[dstRow + x * 4 + 2] = l.b;
                    rgba[dstRow + x * 4 + 3] = 255; // 100% opaque for painted pixels
                }
                else
                {
                    // Transparent alpha 0
                    rgba[dstRow + x * 4 + 0] = 0;
                    rgba[dstRow + x * 4 + 1] = 0;
                    rgba[dstRow + x * 4 + 2] = 0;
                    rgba[dstRow + x * 4 + 3] = 0;
                }
            }
        }

        int result = stbi_write_png(target.c_str(), m_width, m_height, 4, rgba.data(), m_width * 4);
        return (result != 0);
    }

    bool MaskPainter::SaveOverlayBMP(const std::string& filePath)
    {
        if (!IsLoaded()) return false;

        std::string target = filePath.empty() ? "mask_overlay.bmp" : filePath;
        std::ofstream file(target, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t rowStride = ((m_width * 8 + 31) / 32) * 4;
        uint32_t imageSize = rowStride * m_height;
        uint32_t headerAndPaletteSize = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + 256 * sizeof(BMPColorEntry);
        uint32_t totalFileSize = headerAndPaletteSize + imageSize;

        BMPFileHeader fileHeader;
        fileHeader.bfType = 0x4D42;
        fileHeader.bfSize = totalFileSize;
        fileHeader.bfReserved1 = 0;
        fileHeader.bfReserved2 = 0;
        fileHeader.bfOffBits = headerAndPaletteSize;

        BMPInfoHeader infoHeader;
        infoHeader.biSize = sizeof(BMPInfoHeader);
        infoHeader.biWidth = m_width;
        infoHeader.biHeight = m_height;
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 8;
        infoHeader.biCompression = 0;
        infoHeader.biSizeImage = imageSize;
        infoHeader.biXPelsPerMeter = 2835;
        infoHeader.biYPelsPerMeter = 2835;
        infoHeader.biClrUsed = 256;
        infoHeader.biClrImportant = 256;

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

        BMPColorEntry paletteEntries[256] = {};
        for (size_t i = 0; i < 255; ++i)
        {
            if (i < m_palette.size())
            {
                paletteEntries[i].rgbRed = m_palette[i].r;
                paletteEntries[i].rgbGreen = m_palette[i].g;
                paletteEntries[i].rgbBlue = m_palette[i].b;
                paletteEntries[i].rgbReserved = 0;
            }
        }
        // Palette index 255: Key transparent color (Black or Magenta)
        paletteEntries[255].rgbRed = 0;
        paletteEntries[255].rgbGreen = 0;
        paletteEntries[255].rgbBlue = 0;
        paletteEntries[255].rgbReserved = 0;

        file.write(reinterpret_cast<const char*>(paletteEntries), sizeof(paletteEntries));

        std::vector<uint8_t> rowBuffer(rowStride, 255);
        for (int z = 0; z < m_height; ++z)
        {
            const uint8_t* srcRow = &m_overlayIndices[static_cast<size_t>(z) * m_width];
            std::memcpy(rowBuffer.data(), srcRow, m_width);
            file.write(reinterpret_cast<const char*>(rowBuffer.data()), rowStride);
        }

        file.close();
        return true;
    }

    void MaskPainter::WorldToPixel(float worldX, float worldZ, float mapSizeMeters, int& outPx, int& outPz) const
    {
        if (!IsLoaded() || mapSizeMeters <= 0.0f)
        {
            outPx = 0;
            outPz = 0;
            return;
        }

        float scale = static_cast<float>(m_width) / mapSizeMeters;
        int px = static_cast<int>(std::floor(worldX * scale));
        int pz = static_cast<int>(std::floor(worldZ * scale));

        outPx = std::clamp(px, 0, m_width - 1);
        outPz = std::clamp(pz, 0, m_height - 1);
    }

    void MaskPainter::PixelToWorld(int px, int pz, float mapSizeMeters, float& outWorldX, float& outWorldZ) const
    {
        if (!IsLoaded() || m_width <= 0 || m_height <= 0)
        {
            outWorldX = 0.0f;
            outWorldZ = 0.0f;
            return;
        }

        float scale = mapSizeMeters / static_cast<float>(m_width);
        outWorldX = (static_cast<float>(px) + 0.5f) * scale;
        outWorldZ = (static_cast<float>(pz) + 0.5f) * scale;
    }

    uint8_t MaskPainter::GetBaseLayerIndex(int px, int pz) const
    {
        if (px < 0 || px >= m_width || pz < 0 || pz >= m_height || m_baseIndices.empty())
            return 0;
        return m_baseIndices[static_cast<size_t>(pz) * m_width + px];
    }

    uint8_t MaskPainter::GetOverlayLayerIndex(int px, int pz) const
    {
        if (px < 0 || px >= m_width || pz < 0 || pz >= m_height || m_overlayIndices.empty())
            return EMPTY_OVERLAY;
        return m_overlayIndices[static_cast<size_t>(pz) * m_width + px];
    }

    bool MaskPainter::HasOverlay(int px, int pz) const
    {
        return GetOverlayLayerIndex(px, pz) != EMPTY_OVERLAY;
    }

    uint8_t MaskPainter::GetCompositedLayerIndex(int px, int pz) const
    {
        if (px < 0 || px >= m_width || pz < 0 || pz >= m_height) return 0;
        size_t off = static_cast<size_t>(pz) * m_width + px;
        uint8_t over = m_overlayIndices.empty() ? EMPTY_OVERLAY : m_overlayIndices[off];
        if (over != EMPTY_OVERLAY) return over;
        if (!m_baseIndices.empty()) return m_baseIndices[off];
        return 0;
    }

    bool MaskPainter::GetLayerAtWorld(float worldX, float worldZ, float mapSizeMeters, uint8_t& outIndex, TerrainLayer& outLayer) const
    {
        int px = 0, pz = 0;
        WorldToPixel(worldX, worldZ, mapSizeMeters, px, pz);
        outIndex = GetCompositedLayerIndex(px, pz);
        outLayer = GetPaletteLayer(outIndex);
        return true;
    }

    bool MaskPainter::GetPixelRGB(int px, int pz, uint8_t& outR, uint8_t& outG, uint8_t& outB) const
    {
        if (px < 0 || px >= m_width || pz < 0 || pz >= m_height)
            return false;

        uint8_t idx = GetCompositedLayerIndex(px, pz);
        TerrainLayer l = GetPaletteLayer(idx);
        outR = l.r;
        outG = l.g;
        outB = l.b;
        return true;
    }

    void MaskPainter::StartStroke()
    {
        if (!m_isPaintingStroke)
        {
            m_isPaintingStroke = true;
            m_hasLastPaint = false;
            m_currentStrokeTouched.clear();
        }
    }

    void MaskPainter::EndStroke()
    {
        if (m_isPaintingStroke)
        {
            PushUndoStroke();
            m_isPaintingStroke = false;
            m_hasLastPaint = false;
            m_currentStrokeTouched.clear();
        }
    }

    void MaskPainter::PushUndoStroke()
    {
        if (m_currentStrokeTouched.empty()) return;

        if (m_undoStack.size() >= MAX_UNDO_LEVELS)
        {
            m_undoStack.erase(m_undoStack.begin());
        }

        std::vector<OverlayUndo> stroke;
        stroke.reserve(m_currentStrokeTouched.size());
        for (const auto& pair : m_currentStrokeTouched)
        {
            stroke.push_back({ pair.first, pair.second });
        }
        m_undoStack.push_back(std::move(stroke));
    }

    bool MaskPainter::Undo()
    {
        if (m_undoStack.empty()) return false;

        const auto& stroke = m_undoStack.back();
        for (const auto& item : stroke)
        {
            if (item.offset < m_overlayIndices.size())
            {
                uint8_t cur = m_overlayIndices[item.offset];
                uint8_t old = item.oldVal;

                if (cur == EMPTY_OVERLAY && old != EMPTY_OVERLAY)
                    m_overlayPixelCount++;
                else if (cur != EMPTY_OVERLAY && old == EMPTY_OVERLAY)
                    m_overlayPixelCount--;

                m_overlayIndices[item.offset] = old;
            }
        }

        m_undoStack.pop_back();
        m_minimapDirty = true;
        return true;
    }

    void MaskPainter::PaintPixelDirect(int px, int pz, uint8_t layerIndex, bool isEraser)
    {
        if (px < 0 || px >= m_width || pz < 0 || pz >= m_height) return;

        size_t offset = static_cast<size_t>(pz) * m_width + px;
        uint8_t targetVal = isEraser ? EMPTY_OVERLAY : layerIndex;
        uint8_t oldVal = m_overlayIndices[offset];

        if (oldVal != targetVal)
        {
            if (m_currentStrokeTouched.find(static_cast<uint32_t>(offset)) == m_currentStrokeTouched.end())
            {
                m_currentStrokeTouched[static_cast<uint32_t>(offset)] = oldVal;
            }

            if (oldVal == EMPTY_OVERLAY && targetVal != EMPTY_OVERLAY)
                m_overlayPixelCount++;
            else if (oldVal != EMPTY_OVERLAY && targetVal == EMPTY_OVERLAY)
                m_overlayPixelCount--;

            m_overlayIndices[offset] = targetVal;
            m_minimapDirty = true;
        }
    }

    void MaskPainter::PaintAtWorldPos(float worldX, float worldZ, float mapSizeMeters, uint8_t layerIndex, bool isEraser)
    {
        if (!IsLoaded()) return;

        // Sub-meter stroke interpolation
        if (m_hasLastPaint)
        {
            float dx = worldX - m_lastPaintX;
            float dz = worldZ - m_lastPaintZ;
            float dist = std::sqrt(dx * dx + dz * dz);
            float step = 0.5f;

            if (dist > step)
            {
                int steps = static_cast<int>(dist / step);
                for (int s = 1; s <= steps; ++s)
                {
                    float t = static_cast<float>(s) / static_cast<float>(steps);
                    float ix = m_lastPaintX + dx * t;
                    float iz = m_lastPaintZ + dz * t;

                    int centerPx = 0, centerPz = 0;
                    WorldToPixel(ix, iz, mapSizeMeters, centerPx, centerPz);

                    int r = m_brushRadiusPx;
                    int rSq = r * r;

                    if (m_brushShape == BrushShape::Circle)
                    {
                        for (int dz2 = -r; dz2 <= r; ++dz2)
                        {
                            int pz = centerPz + dz2;
                            if (pz < 0 || pz >= m_height) continue;
                            for (int dx2 = -r; dx2 <= r; ++dx2)
                            {
                                if (dx2 * dx2 + dz2 * dz2 <= rSq)
                                {
                                    PaintPixelDirect(centerPx + dx2, pz, layerIndex, isEraser);
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int dz2 = -r; dz2 <= r; ++dz2)
                        {
                            int pz = centerPz + dz2;
                            if (pz < 0 || pz >= m_height) continue;
                            for (int dx2 = -r; dx2 <= r; ++dx2)
                            {
                                PaintPixelDirect(centerPx + dx2, pz, layerIndex, isEraser);
                            }
                        }
                    }
                }
            }
        }

        m_lastPaintX = worldX;
        m_lastPaintZ = worldZ;
        m_hasLastPaint = true;

        int centerPx = 0, centerPz = 0;
        WorldToPixel(worldX, worldZ, mapSizeMeters, centerPx, centerPz);

        int r = m_brushRadiusPx;
        int rSq = r * r;

        if (m_brushShape == BrushShape::Circle)
        {
            for (int dz2 = -r; dz2 <= r; ++dz2)
            {
                int pz = centerPz + dz2;
                if (pz < 0 || pz >= m_height) continue;
                for (int dx2 = -r; dx2 <= r; ++dx2)
                {
                    if (dx2 * dx2 + dz2 * dz2 <= rSq)
                    {
                        PaintPixelDirect(centerPx + dx2, pz, layerIndex, isEraser);
                    }
                }
            }
        }
        else
        {
            for (int dz2 = -r; dz2 <= r; ++dz2)
            {
                int pz = centerPz + dz2;
                if (pz < 0 || pz >= m_height) continue;
                for (int dx2 = -r; dx2 <= r; ++dx2)
                {
                    PaintPixelDirect(centerPx + dx2, pz, layerIndex, isEraser);
                }
            }
        }
    }

    ID3D11ShaderResourceView* MaskPainter::GetOrCreateMinimapTexture(
        ID3D11Device* device,
        float centerWorldX,
        float centerWorldZ,
        float zoom,
        float mapSizeMeters
    )
    {
        if (!device || !IsLoaded()) return nullptr;

        bool viewportMoved = (std::abs(centerWorldX - m_lastMinimapCenterX) > 0.1f ||
                              std::abs(centerWorldZ - m_lastMinimapCenterZ) > 0.1f ||
                              std::abs(zoom - m_lastMinimapZoom) > 0.001f ||
                              m_viewMode != m_lastMinimapViewMode);

        if (viewportMoved || m_minimapDirty || !m_pMinimapTexture)
        {
            m_lastMinimapCenterX = centerWorldX;
            m_lastMinimapCenterZ = centerWorldZ;
            m_lastMinimapZoom = zoom;
            m_lastMinimapViewMode = m_viewMode;

            float invZoom = 1.0f / zoom;
            float halfDim = MINIMAP_TEX_DIM * 0.5f;

            for (int py = 0; py < MINIMAP_TEX_DIM; ++py)
            {
                float wz = centerWorldZ + (halfDim - static_cast<float>(py)) * invZoom;
                int pz = static_cast<int>(std::floor(wz));
                bool validZ = (pz >= 0 && pz < m_height);

                for (int px = 0; px < MINIMAP_TEX_DIM; ++px)
                {
                    float wx = centerWorldX + (static_cast<float>(px) - halfDim) * invZoom;
                    int pxx = static_cast<int>(std::floor(wx));

                    uint32_t col = 0xFF222222;

                    if (validZ && pxx >= 0 && pxx < m_width)
                    {
                        size_t off = static_cast<size_t>(pz) * m_width + pxx;
                        uint8_t over = m_overlayIndices[off];
                        uint8_t base = m_baseIndices[off];

                        if (m_viewMode == MaskViewMode::Composited)
                        {
                            uint8_t idx = (over != EMPTY_OVERLAY) ? over : base;
                            TerrainLayer l = GetPaletteLayer(idx);
                            col = 0xFF000000 | (static_cast<uint32_t>(l.b) << 16) | (static_cast<uint32_t>(l.g) << 8) | static_cast<uint32_t>(l.r);
                        }
                        else if (m_viewMode == MaskViewMode::OverlayOnly)
                        {
                            if (over != EMPTY_OVERLAY)
                            {
                                TerrainLayer l = GetPaletteLayer(over);
                                col = 0xFF000000 | (static_cast<uint32_t>(l.b) << 16) | (static_cast<uint32_t>(l.g) << 8) | static_cast<uint32_t>(l.r);
                            }
                            else
                            {
                                // Checkerboard transparency preview
                                bool check = ((px / 8) + (py / 8)) % 2 == 0;
                                col = check ? 0xFF353535 : 0xFF252525;
                            }
                        }
                        else // BaseOnly
                        {
                            TerrainLayer l = GetPaletteLayer(base);
                            col = 0xFF000000 | (static_cast<uint32_t>(l.b) << 16) | (static_cast<uint32_t>(l.g) << 8) | static_cast<uint32_t>(l.r);
                        }
                    }
                    m_minimapPixels[py * MINIMAP_TEX_DIM + px] = col;
                }
            }

            if (m_pMinimapTexture)
            {
                ID3D11DeviceContext* context = nullptr;
                device->GetImmediateContext(&context);
                if (context)
                {
                    context->UpdateSubresource(m_pMinimapTexture, 0, nullptr, m_minimapPixels.data(), MINIMAP_TEX_DIM * sizeof(uint32_t), 0);
                    context->Release();
                }
            }
            else
            {
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = MINIMAP_TEX_DIM;
                desc.Height = MINIMAP_TEX_DIM;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA subData = {};
                subData.pSysMem = m_minimapPixels.data();
                subData.SysMemPitch = MINIMAP_TEX_DIM * sizeof(uint32_t);

                HRESULT hr = device->CreateTexture2D(&desc, &subData, &m_pMinimapTexture);
                if (SUCCEEDED(hr))
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = desc.Format;
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    device->CreateShaderResourceView(m_pMinimapTexture, &srvDesc, &m_pMinimapSRV);
                }
            }

            m_minimapDirty = false;
        }

        return m_pMinimapSRV;
    }
}
