#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <d3d11.h>
#include "offsets.h"
#include "layers_parser.h"

namespace DayZBuldozer
{
    enum class BrushShape
    {
        Circle = 0,
        Square = 1
    };

    enum class MaskViewMode
    {
        Composited = 0, // Base Mask + Painted Overlay
        OverlayOnly = 1, // Only the newly painted overlay
        BaseOnly = 2    // Original pristine base mask
    };

    struct OverlayUndo
    {
        uint32_t offset;
        uint8_t  oldVal;
    };

    class MaskPainter
    {
    public:
        // Marker for transparent / unpainted pixel in the overlay layer
        static constexpr uint8_t EMPTY_OVERLAY = 0xFF;

        MaskPainter();
        ~MaskPainter();

        // Canvas management
        bool LoadBaseMask(const std::string& filePath, const std::vector<TerrainLayer>& activeLayers);
        bool CreateNewMask(int width, int height, uint8_t baseLayerIndex, const std::vector<TerrainLayer>& activeLayers);
        void ClearOverlay();

        // Export operations
        bool SaveMergedMaskBMP(const std::string& filePath = "");
        bool SaveOverlayPNG(const std::string& filePath = "");
        bool SaveOverlayBMP(const std::string& filePath = "");

        // Palette management
        void SetPalette(const std::vector<TerrainLayer>& layers);
        const std::vector<TerrainLayer>& GetPalette() const { return m_palette; }
        TerrainLayer GetPaletteLayer(uint8_t index) const;
        uint8_t FindClosestLayerIndex(uint8_t r, uint8_t g, uint8_t b) const;

        // Painting operations (Paints onto Overlay Layer!)
        void StartStroke();
        void PaintAtWorldPos(float worldX, float worldZ, float mapSizeMeters, uint8_t layerIndex, bool isEraser);
        void PaintPixelDirect(int px, int pz, uint8_t layerIndex, bool isEraser);
        void EndStroke();

        // Undo system
        void PushUndoStroke();
        bool Undo();
        bool CanUndo() const { return !m_undoStack.empty(); }

        // Coordinate conversions (1 pixel = 1 meter)
        void WorldToPixel(float worldX, float worldZ, float mapSizeMeters, int& outPx, int& outPz) const;
        void PixelToWorld(int px, int pz, float mapSizeMeters, float& outWorldX, float& outWorldZ) const;

        // Layer access
        uint8_t GetBaseLayerIndex(int px, int pz) const;
        uint8_t GetOverlayLayerIndex(int px, int pz) const;
        bool HasOverlay(int px, int pz) const;
        uint8_t GetCompositedLayerIndex(int px, int pz) const;
        bool GetLayerAtWorld(float worldX, float worldZ, float mapSizeMeters, uint8_t& outIndex, TerrainLayer& outLayer) const;
        bool GetPixelRGB(int px, int pz, uint8_t& outR, uint8_t& outG, uint8_t& outB) const;

        // Minimap & Canvas dynamic texture generation (512x512)
        ID3D11ShaderResourceView* GetOrCreateMinimapTexture(
            ID3D11Device* device,
            float centerWorldX,
            float centerWorldZ,
            float zoom,
            float mapSizeMeters
        );
        void InvalidateTextures();

        // State & Metrics
        bool IsLoaded() const { return !m_baseIndices.empty() && m_width > 0 && m_height > 0; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        const std::string& GetFilePath() const { return m_currentFilePath; }
        size_t GetMemoryBytes() const { return m_baseIndices.size() + m_overlayIndices.size(); }
        size_t GetPaintedOverlayPixelCount() const { return m_overlayPixelCount; }

        // Settings
        int m_brushRadiusPx{ 16 };
        BrushShape m_brushShape{ BrushShape::Circle };
        MaskViewMode m_viewMode{ MaskViewMode::Composited };
        bool m_isPaintingStroke{ false };

        // 3D Surface View Settings
        bool m_showPixelQuadsOnTerrain{ true };
        bool m_showPixelGridLines{ true };
        int  m_surfacePreviewRadiusMeters{ 35 };
        float m_surfaceAlpha{ 0.85f };

    private:
        bool LoadStandardBMP8Bit(const std::string& filePath, const std::vector<TerrainLayer>& activeLayers);

        // Multi-Layer Storage:
        // 1. Base Mask: loaded from original file (1 byte per pixel)
        std::vector<uint8_t> m_baseIndices;
        // 2. Overlay Layer: user painted strokes (0xFF = transparent/empty, 0..254 = painted layer)
        std::vector<uint8_t> m_overlayIndices;

        int m_width{ 0 };
        int m_height{ 0 };
        std::string m_currentFilePath;
        size_t m_overlayPixelCount{ 0 };

        // 256-color palette
        std::vector<TerrainLayer> m_palette;

        // Sparse Undo system for the overlay
        std::vector<std::vector<OverlayUndo>> m_undoStack;
        std::unordered_map<uint32_t, uint8_t> m_currentStrokeTouched;
        static constexpr size_t MAX_UNDO_LEVELS = 32;

        // Stroke interpolation
        float m_lastPaintX{ 0.0f };
        float m_lastPaintZ{ 0.0f };
        bool  m_hasLastPaint{ false };

        // Dynamic 512x512 Minimap Viewport Texture
        static constexpr int MINIMAP_TEX_DIM = 512;
        std::vector<uint32_t> m_minimapPixels;
        ID3D11Texture2D* m_pMinimapTexture{ nullptr };
        ID3D11ShaderResourceView* m_pMinimapSRV{ nullptr };
        float m_lastMinimapCenterX{ -9999.0f };
        float m_lastMinimapCenterZ{ -9999.0f };
        float m_lastMinimapZoom{ -1.0f };
        MaskViewMode m_lastMinimapViewMode{ MaskViewMode::Composited };
        bool  m_minimapDirty{ true };
    };

    inline MaskPainter g_MaskPainter;
}
