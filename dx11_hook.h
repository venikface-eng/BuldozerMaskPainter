#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>

namespace DayZBuldozer
{
    class DX11Hook
    {
    public:
        static bool Initialize();
        static void Shutdown();

        // State
        static bool s_isHooked;
        static bool s_showMenu;
        static bool s_showMinimap;
        static bool s_maskPaintMode;
        static bool s_isAltHeld;
        static bool s_isEraserActive;
        static HWND s_gameHwnd;

        // Configuration
        static float s_cameraFov; // Field of View in radians
        static float s_manualMapSize; // Overridden map size if needed
        static bool s_useAutoMapSize;
        static bool s_detectPauseMenu;
        static bool s_detectWindowFocus;

        // Buldozer Tool Mode Controls
        static void SetBuldozerMode(uint8_t mode);
        static void CycleBuldozerMode();
    };
}
