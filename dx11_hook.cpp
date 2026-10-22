#include "dx11_hook.h"
#include "offsets.h"
#include "layers_parser.h"
#include "mask_painter.h"

#include "vendor/minhook/include/MinHook.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/backends/imgui_impl_win32.h"
#include "vendor/imgui/backends/imgui_impl_dx11.h"

#include <vector>
#include <cmath>
#include <string>
#include <iostream>
#include <algorithm>

// Forward declare Win32 ImGui WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace DayZBuldozer
{
    // Static member definitions
    bool DX11Hook::s_isHooked = false;
    bool DX11Hook::s_showMenu = true;
    bool DX11Hook::s_showMinimap = true;
    bool DX11Hook::s_maskPaintMode = false;
    bool DX11Hook::s_isAltHeld = false;
    bool DX11Hook::s_isEraserActive = false;
    HWND DX11Hook::s_gameHwnd = nullptr;
    float DX11Hook::s_cameraFov = 0.741765f;
    float DX11Hook::s_manualMapSize = 20480.0f;
    bool DX11Hook::s_useAutoMapSize = true;
    bool DX11Hook::s_detectPauseMenu = true;
    bool DX11Hook::s_detectWindowFocus = true;

    // Direct3D 11 function prototypes
    typedef HRESULT(__fastcall* tPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    typedef HRESULT(__fastcall* tResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

    static tPresent oPresent = nullptr;
    static tResizeBuffers oResizeBuffers = nullptr;
    static WNDPROC oWndProc = nullptr;

    static ID3D11Device* g_pd3dDevice = nullptr;
    static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

    static int g_selectedLayerIndex = 0;
    static char g_layersCfgPath[512] = "BuldozerMaskPainter\\info\\layers.cfg";
    static char g_maskFilePath[512] = "mask_merged.bmp";
    static char g_overlayPngPath[512] = "mask_overlay.png";
    static int g_newMaskWidth = 20480;
    static int g_newMaskHeight = 20480;
    static bool g_layersAutoLoaded = false;

    // Mini-map Viewport state
    static float g_minimapZoom = 6.0f;
    static float g_minimapCenterX = 10240.0f;
    static float g_minimapCenterZ = 10240.0f;
    static bool g_minimapFollowBrush = true;
    static bool g_minimapFollowCamera = false;
    static bool g_minimapShowGrid = true;

    static void CreateRenderTarget(IDXGISwapChain* pSwapChain)
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer))))
        {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    static void CleanupRenderTarget()
    {
        if (g_mainRenderTargetView)
        {
            g_mainRenderTargetView->Release();
            g_mainRenderTargetView = nullptr;
        }
    }

    static inline bool ProjectToScreen(const Vector3& worldPos, float screenW, float screenH, ImVec2& outScreen)
    {
        return g_Engine.WorldToScreen(worldPos, screenW, screenH, outScreen.x, outScreen.y);
    }

    // -----------------------------------------------------------------------------------------
    // Buldozer Tool Mode Integration & Hook State
    // Mode 1: Objects Mode (Buldozer Native)
    // Mode 2: Brush Tool (Buldozer Native - Flatten/Smooth/Normal/Height set)
    // Mode 3: Mask Painter Mode (Isolated Tool Mode: No terrain deformation, no object dragging)
    // -----------------------------------------------------------------------------------------
    static void* g_pSelectionTypeStub = nullptr;
    static uint8_t g_origSelectionTypeBytes[Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN] = { 0 };

    typedef __int64 (__fastcall* tBuldozerStatusBanner)(__int64 a1, __int64 a2, char a3, float *a4, float a5);
    typedef __int64 (__fastcall* tSub_140B87E60)(__int64 a1, __int64 a2);

    static tBuldozerStatusBanner oBuldozerStatusBanner = nullptr;
    static tSub_140B87E60 oSub_140B87E60 = nullptr;

    extern "C" void OnUABuldSelectionType(uintptr_t editCursor)
    {
        if (!editCursor) return;

        uint8_t currentMode = *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET);
        if (currentMode == 1)
        {
            // 1 (Objects Mode) -> 2 (Native Brush Tool)
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 2;
            typedef void(__fastcall* tBrushInit)(uintptr_t);
            auto fnBrushInit = reinterpret_cast<tBrushInit>(g_Engine.moduleBase + Offsets::BULDOZER_BRUSH_INIT_RVA);
            if (fnBrushInit) fnBrushInit(editCursor);

            DX11Hook::s_maskPaintMode = false;
        }
        else if (currentMode == 2)
        {
            // 2 (Native Brush Tool) -> 3 (Mask Painter Mode)
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 3;
            DX11Hook::s_maskPaintMode = true;
        }
        else
        {
            // 3 (Mask Painter Mode) or any other -> 1 (Objects Mode)
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 1;
            DX11Hook::s_maskPaintMode = false;
        }
    }

    void DX11Hook::SetBuldozerMode(uint8_t targetMode)
    {
        uintptr_t editCursor = g_Engine.GetEditCursor();
        if (!editCursor) return;

        if (targetMode == 2)
        {
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 2;
            typedef void(__fastcall* tBrushInit)(uintptr_t);
            auto fnBrushInit = reinterpret_cast<tBrushInit>(g_Engine.moduleBase + Offsets::BULDOZER_BRUSH_INIT_RVA);
            if (fnBrushInit) fnBrushInit(editCursor);
            DX11Hook::s_maskPaintMode = false;
        }
        else if (targetMode == 3)
        {
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 3;
            DX11Hook::s_maskPaintMode = true;
        }
        else
        {
            *reinterpret_cast<uint8_t*>(editCursor + Offsets::EDITCURSOR_MODE_OFFSET) = 1;
            DX11Hook::s_maskPaintMode = false;
        }
    }

    void DX11Hook::CycleBuldozerMode()
    {
        uintptr_t editCursor = g_Engine.GetEditCursor();
        if (editCursor)
        {
            OnUABuldSelectionType(editCursor);
        }
    }

    static bool InstallSelectionTypeHook()
    {
        if (!g_Engine.moduleBase) return false;

        uintptr_t patchAddr = g_Engine.moduleBase + Offsets::BULDOZER_SELECTION_TYPE_RVA;
        uintptr_t resumeAddr = g_Engine.moduleBase + Offsets::BULDOZER_SELECTION_TYPE_RESUME_RVA;

        // Allocate executable memory for the stub
        g_pSelectionTypeStub = VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_pSelectionTypeStub) return false;

        uint8_t* stub = reinterpret_cast<uint8_t*>(g_pSelectionTypeStub);
        int idx = 0;

        // sub rsp, 28h (4 bytes - 16-byte align stack + shadow space)
        stub[idx++] = 0x48; stub[idx++] = 0x83; stub[idx++] = 0xEC; stub[idx++] = 0x28;

        // mov rcx, rsi (3 bytes - rsi holds editCursor in sub_140B882A0)
        stub[idx++] = 0x48; stub[idx++] = 0x8B; stub[idx++] = 0xCE;

        // mov rax, &OnUABuldSelectionType (10 bytes)
        stub[idx++] = 0x48; stub[idx++] = 0xB8;
        uint64_t fnAddr = reinterpret_cast<uint64_t>(&OnUABuldSelectionType);
        memcpy(&stub[idx], &fnAddr, 8);
        idx += 8;

        // call rax (2 bytes)
        stub[idx++] = 0xFF; stub[idx++] = 0xD0;

        // add rsp, 28h (4 bytes)
        stub[idx++] = 0x48; stub[idx++] = 0x83; stub[idx++] = 0xC4; stub[idx++] = 0x28;

        // mov rax, resumeAddr (10 bytes)
        stub[idx++] = 0x48; stub[idx++] = 0xB8;
        uint64_t resAddr64 = static_cast<uint64_t>(resumeAddr);
        memcpy(&stub[idx], &resAddr64, 8);
        idx += 8;

        // jmp rax (2 bytes)
        stub[idx++] = 0xFF; stub[idx++] = 0xE0;

        FlushInstructionCache(GetCurrentProcess(), g_pSelectionTypeStub, idx);

        // Patch the bytes at BULDOZER_SELECTION_TYPE_RVA
        DWORD oldProtect;
        if (VirtualProtect(reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            memcpy(g_origSelectionTypeBytes, reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN);

            uint8_t patch[Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN];
            // jmp qword ptr [rip + 0] (6 bytes)
            patch[0] = 0xFF;
            patch[1] = 0x25;
            patch[2] = 0x00;
            patch[3] = 0x00;
            patch[4] = 0x00;
            patch[5] = 0x00;

            // Target pointer (8 bytes)
            uint64_t stubAddr64 = reinterpret_cast<uint64_t>(g_pSelectionTypeStub);
            memcpy(&patch[6], &stubAddr64, 8);

            // NOP remaining bytes (0x90)
            if (Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN > 14)
            {
                memset(&patch[14], 0x90, Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN - 14);
            }

            memcpy(reinterpret_cast<void*>(patchAddr), patch, Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN);
            VirtualProtect(reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN);
            return true;
        }

        return false;
    }

    static void UninstallSelectionTypeHook()
    {
        if (g_pSelectionTypeStub && g_Engine.moduleBase)
        {
            uintptr_t patchAddr = g_Engine.moduleBase + Offsets::BULDOZER_SELECTION_TYPE_RVA;
            DWORD oldProtect;
            if (VirtualProtect(reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                memcpy(reinterpret_cast<void*>(patchAddr), g_origSelectionTypeBytes, Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN);
                VirtualProtect(reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN, oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patchAddr), Offsets::BULDOZER_SELECTION_TYPE_PATCH_LEN);
            }

            VirtualFree(g_pSelectionTypeStub, 0, MEM_RELEASE);
            g_pSelectionTypeStub = nullptr;
        }
    }

    static __int64 __fastcall HookedBuldozerStatusBanner(__int64 a1, __int64 a2, char a3, float *a4, float a5)
    {
        if (a1 && *reinterpret_cast<uint8_t*>(a1 + Offsets::EDITCURSOR_MODE_OFFSET) == 3)
        {
            unsigned int duration = static_cast<unsigned int>(a5 * 3000.0f);
            uintptr_t notifyTarget = g_Engine.moduleBase + Offsets::BULDOZER_NOTIFY_SINK_RVA;
            typedef __int64 (*tBuldNotify)(void*, int, unsigned int, const char*, ...);
            auto fnNotify = reinterpret_cast<tBuldNotify>(g_Engine.moduleBase + Offsets::BULDOZER_NOTIFY_FUNC_RVA);

            const char* layerName = "None";
            const auto& layers = g_LayersParser.GetLayers();
            if (g_selectedLayerIndex >= 0 && g_selectedLayerIndex < static_cast<int>(layers.size()))
            {
                layerName = layers[g_selectedLayerIndex].name.c_str();
            }
            TerrainLayer curLayer = g_MaskPainter.GetPaletteLayer(static_cast<uint8_t>(g_selectedLayerIndex));

            if (fnNotify)
            {
                return fnNotify(
                    reinterpret_cast<char*>(notifyTarget) + 4,
                    1,
                    duration,
                    "Landscape Mask Painter - [E] Eraser: %s | [Ctrl+Z] Undo | [Ctrl+S] Save | Layer: %s (R:%d G:%d B:%d) Radius: %d m",
                    DX11Hook::s_isEraserActive ? "ON" : "OFF",
                    layerName,
                    curLayer.r, curLayer.g, curLayer.b,
                    g_MaskPainter.m_brushRadiusPx
                );
            }
        }

        if (oBuldozerStatusBanner)
            return oBuldozerStatusBanner(a1, a2, a3, a4, a5);
        return 0;
    }

    static __int64 __fastcall HookedSub_140B87E60(__int64 a1, __int64 a2)
    {
        if (a1 && *reinterpret_cast<uint8_t*>(a1 + Offsets::EDITCURSOR_MODE_OFFSET) == 3)
        {
            // In Mask Painter mode: do not raycast or highlight objects in Buldozer
            return 0;
        }
        if (oSub_140B87E60)
            return oSub_140B87E60(a1, a2);
        return 0;
    }

    // Window Procedure Hook
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        // Toggle menu with INSERT or F11
        if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && (wParam == VK_INSERT || wParam == VK_F11))
        {
            DX11Hook::s_showMenu = !DX11Hook::s_showMenu;
            return 0;
        }

        bool isGameMenuOpen = DX11Hook::s_detectPauseMenu && g_Engine.IsGameMenuOpen();

        // Hotkeys (only active when game pause menu is not open)
        if (!isGameMenuOpen && (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN))
        {
            if (wParam == 'M' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                DX11Hook::s_showMinimap = !DX11Hook::s_showMinimap;
                return 0;
            }
            if (wParam == VK_MENU)
            {
                DX11Hook::s_isAltHeld = true;
            }
            else if (wParam == 'E' && !(GetKeyState(VK_CONTROL) & 0x8000))
            {
                DX11Hook::s_isEraserActive = !DX11Hook::s_isEraserActive;
            }
            else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                g_MaskPainter.Undo();
            }
            else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                g_MaskPainter.SaveMergedMaskBMP(g_maskFilePath);
            }
        }
        else if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
        {
            if (wParam == VK_MENU)
            {
                DX11Hook::s_isAltHeld = false;
            }
        }

        // Standard Dear ImGui message routing
        if (DX11Hook::s_showMenu || DX11Hook::s_showMinimap)
        {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
                return true;

            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST))
                return true;

            if (io.WantCaptureKeyboard && ((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) || uMsg == WM_CHAR))
                return true;
        }

        // Mask Painting Mode: Isolate left clicks so Buldozer window proc doesn't receive them while painting
        // NOTE: If game menu is open, let clicks pass through to DayZ menu buttons!
        if (DX11Hook::s_maskPaintMode && !isGameMenuOpen)
        {
            if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP)
            {
                return 0;
            }
        }

        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    }

    // Hooked IDXGISwapChain::Present
    static HRESULT __fastcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        if (!g_pd3dDevice)
        {
            if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_pd3dDevice))))
                return oPresent(pSwapChain, SyncInterval, Flags);

            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);

            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            DX11Hook::s_gameHwnd = desc.OutputWindow;

            CreateRenderTarget(pSwapChain);

            oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(DX11Hook::s_gameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            // Custom Theme with Amber & Dark Slate
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 6.0f;
            style.FrameRounding = 4.0f;
            style.PopupRounding = 4.0f;
            style.GrabRounding = 4.0f;
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.85f, 0.45f, 0.10f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.85f, 0.45f, 0.10f, 0.7f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.95f, 0.55f, 0.20f, 0.8f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.70f, 0.35f, 0.10f, 0.8f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.45f, 0.15f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.55f, 0.20f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.60f, 0.15f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.45f, 0.10f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.60f, 0.20f, 1.0f);

            ImGui_ImplWin32_Init(DX11Hook::s_gameHwnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            g_Engine.Initialize();

            static bool s_buldozerHooksInstalled = false;
            if (!s_buldozerHooksInstalled && g_Engine.moduleBase)
            {
                InstallSelectionTypeHook();

                MH_CreateHook(
                    reinterpret_cast<void*>(g_Engine.moduleBase + Offsets::BULDOZER_STATUS_BANNER_RVA),
                    reinterpret_cast<void*>(&HookedBuldozerStatusBanner),
                    reinterpret_cast<void**>(&oBuldozerStatusBanner)
                );
                MH_EnableHook(reinterpret_cast<void*>(g_Engine.moduleBase + Offsets::BULDOZER_STATUS_BANNER_RVA));

                MH_CreateHook(
                    reinterpret_cast<void*>(g_Engine.moduleBase + Offsets::BULDOZER_OBJECT_HOVER_RVA),
                    reinterpret_cast<void*>(&HookedSub_140B87E60),
                    reinterpret_cast<void**>(&oSub_140B87E60)
                );
                MH_EnableHook(reinterpret_cast<void*>(g_Engine.moduleBase + Offsets::BULDOZER_OBJECT_HOVER_RVA));

                s_buldozerHooksInstalled = true;
            }

            if (!g_layersAutoLoaded)
            {
                if (g_LayersParser.LoadFromFile(g_layersCfgPath) ||
                    g_LayersParser.LoadFromFile("BuldozerMaskPainter\\info\\layers.cfg") ||
                    g_LayersParser.LoadFromFile("info\\layers.cfg") ||
                    g_LayersParser.LoadFromFile("layers.cfg"))
                {
                    strncpy_s(g_layersCfgPath, g_LayersParser.GetLoadedFilePath().c_str(), sizeof(g_layersCfgPath) - 1);
                    g_MaskPainter.SetPalette(g_LayersParser.GetLayers());
                }
                g_layersAutoLoaded = true;
            }
        }

        // Keep DX11Hook::s_maskPaintMode perfectly synchronized with Buldozer's tool mode
        uint8_t currentBuldozerMode = g_Engine.GetBuldozerMode();
        DX11Hook::s_maskPaintMode = (currentBuldozerMode == 3);

        // Begin ImGui Frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Screen dimensions
        RECT clientRect;
        GetClientRect(DX11Hook::s_gameHwnd, &clientRect);
        float screenW = static_cast<float>(clientRect.right - clientRect.left);
        float screenH = static_cast<float>(clientRect.bottom - clientRect.top);

        // Map Size (auto-detected via native GetWorldSize: dim * cellSize)
        float mapSizeMeters = DX11Hook::s_useAutoMapSize ? g_Engine.GetTerrainSizeMeters() : DX11Hook::s_manualMapSize;
        if (mapSizeMeters < 100.0f) mapSizeMeters = DX11Hook::s_manualMapSize;

        // Window focus & mouse position checks
        HWND foregroundHwnd = GetForegroundWindow();
        bool isWindowFocused = !DX11Hook::s_detectWindowFocus || (foregroundHwnd == DX11Hook::s_gameHwnd);
        bool isGameMenuOpen = DX11Hook::s_detectPauseMenu && g_Engine.IsGameMenuOpen();

        POINT mousePt;
        GetCursorPos(&mousePt);
        ScreenToClient(DX11Hook::s_gameHwnd, &mousePt);

        bool isMouseInClient = (mousePt.x >= 0 && mousePt.y >= 0 && 
                                mousePt.x < static_cast<int>(screenW) && 
                                mousePt.y < static_cast<int>(screenH));

        // Can interact with terrain: must be foreground window, mouse inside window, and no game pause menu
        bool canInteractTerrain = isWindowFocused && isMouseInClient && !isGameMenuOpen;

        // Camera & Terrain Raycasting
        CameraTransform* cam = g_Engine.GetCameraTransform();
        bool hasHitTerrain = false;
        Vector3 hitPoint(0, 0, 0);

        DX11Hook::s_isAltHeld = isWindowFocused && ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);

        if (canInteractTerrain && cam && screenW > 10.0f && screenH > 10.0f)
        {
            float mouseX = static_cast<float>(mousePt.x);
            float mouseY = static_cast<float>(mousePt.y);

            Vector3 rayDir = g_Engine.ScreenToWorldRay(mouseX, mouseY, screenW, screenH);
            hasHitTerrain = g_Engine.RaycastTerrain(cam->position, rayDir, 8000.0f, hitPoint);

            // ---------------------------------------------------------------------------------
            // 3D TERRAIN SURFACE VISUALIZATION (ONLY ACTIVE IN MASK PAINTING MODE [F])
            // ---------------------------------------------------------------------------------
            if (DX11Hook::s_maskPaintMode && hasHitTerrain && g_MaskPainter.IsLoaded())
            {
                ImDrawList* drawList = ImGui::GetBackgroundDrawList();

                TerrainLayer activeLayer = g_MaskPainter.GetPaletteLayer(static_cast<uint8_t>(g_selectedLayerIndex));
                uint8_t curR = DX11Hook::s_isEraserActive ? 255 : activeLayer.r;
                uint8_t curG = DX11Hook::s_isEraserActive ? 50 : activeLayer.g;
                uint8_t curB = DX11Hook::s_isEraserActive ? 50 : activeLayer.b;

                int snapX = static_cast<int>(std::floor(hitPoint.x));
                int snapZ = static_cast<int>(std::floor(hitPoint.z));

                float metersPerPx = mapSizeMeters / static_cast<float>(g_MaskPainter.GetWidth());
                int radiusM = static_cast<int>(g_MaskPainter.m_brushRadiusPx * metersPerPx + 0.5f);
                if (radiusM < 1) radiusM = 1;
                float radiusMeters = static_cast<float>(radiusM);

                // 1. Draw 1:1 Meter Pixel Quads directly on the landscape surface
                if (g_MaskPainter.m_showPixelQuadsOnTerrain)
                {
                    int previewRad = std::min(g_MaskPainter.m_surfacePreviewRadiusMeters, 45);
                    int rBrushSq = radiusM * radiusM;

                    for (int dz = -previewRad; dz <= previewRad; ++dz)
                    {
                        int cellZ = snapZ + dz;
                        if (cellZ < 0 || cellZ >= static_cast<int>(mapSizeMeters)) continue;

                        for (int dx = -previewRad; dx <= previewRad; ++dx)
                        {
                            int cellX = snapX + dx;
                            if (cellX < 0 || cellX >= static_cast<int>(mapSizeMeters)) continue;

                            bool isInsideBrush = false;
                            if (g_MaskPainter.m_brushShape == BrushShape::Circle)
                                isInsideBrush = (dx * dx + dz * dz <= rBrushSq);
                            else
                                isInsideBrush = (std::abs(dx) <= radiusM && std::abs(dz) <= radiusM);

                            float x0 = static_cast<float>(cellX);
                            float x1 = x0 + 1.0f;
                            float z0 = static_cast<float>(cellZ);
                            float z1 = z0 + 1.0f;

                            Vector3 v00(x0, g_Engine.GetSurfaceHeight(x0, z0) + 0.035f, z0);
                            Vector3 v10(x1, g_Engine.GetSurfaceHeight(x1, z0) + 0.035f, z0);
                            Vector3 v11(x1, g_Engine.GetSurfaceHeight(x1, z1) + 0.035f, z1);
                            Vector3 v01(x0, g_Engine.GetSurfaceHeight(x0, z1) + 0.035f, z1);

                            ImVec2 sp00, sp10, sp11, sp01;
                            if (ProjectToScreen(v00, screenW, screenH, sp00) &&
                                ProjectToScreen(v10, screenW, screenH, sp10) &&
                                ProjectToScreen(v11, screenW, screenH, sp11) &&
                                ProjectToScreen(v01, screenW, screenH, sp01))
                            {
                                if (isInsideBrush)
                                {
                                    // Live brush hover stamp
                                    ImU32 fillCol = IM_COL32(curR, curG, curB, static_cast<int>(g_MaskPainter.m_surfaceAlpha * 255.0f));
                                    ImU32 lineCol = IM_COL32(curR, curG, curB, 220);

                                    drawList->AddQuadFilled(sp00, sp10, sp11, sp01, fillCol);
                                    if (g_MaskPainter.m_showPixelGridLines)
                                    {
                                        drawList->AddQuad(sp00, sp10, sp11, sp01, lineCol, 1.0f);
                                    }
                                }
                                else
                                {
                                    // Composited terrain mask cells (shows overlay or original base mask!)
                                    uint8_t pr, pg, pb;
                                    if (g_MaskPainter.GetPixelRGB(cellX, cellZ, pr, pg, pb))
                                    {
                                        bool isOverlay = g_MaskPainter.HasOverlay(cellX, cellZ);
                                        ImU32 fillCol = IM_COL32(pr, pg, pb, static_cast<int>(g_MaskPainter.m_surfaceAlpha * 255.0f));

                                        drawList->AddQuadFilled(sp00, sp10, sp11, sp01, fillCol);
                                        if (g_MaskPainter.m_showPixelGridLines)
                                        {
                                            ImU32 gridCol = isOverlay ? IM_COL32(255, 255, 255, 160) : IM_COL32(0, 0, 0, 70);
                                            drawList->AddQuad(sp00, sp10, sp11, sp01, gridCol, isOverlay ? 1.5f : 0.8f);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 2. Brush Outline Ring on terrain
                ImU32 brushOutlineColor = DX11Hook::s_isEraserActive ? IM_COL32(255, 50, 50, 240) : IM_COL32(curR, curG, curB, 240);

                if (g_MaskPainter.m_brushShape == BrushShape::Circle)
                {
                    constexpr int SEGMENTS = 36;
                    std::vector<ImVec2> screenPoints;
                    screenPoints.reserve(SEGMENTS + 1);

                    for (int i = 0; i <= SEGMENTS; ++i)
                    {
                        float angle = (static_cast<float>(i) / static_cast<float>(SEGMENTS)) * 6.2831853f;
                        float wx = hitPoint.x + radiusMeters * std::cos(angle);
                        float wz = hitPoint.z + radiusMeters * std::sin(angle);
                        float wy = g_Engine.GetSurfaceHeight(wx, wz) + 0.05f;

                        ImVec2 sp;
                        if (ProjectToScreen(Vector3(wx, wy, wz), screenW, screenH, sp))
                        {
                            screenPoints.push_back(sp);
                        }
                    }

                    if (screenPoints.size() >= 3)
                    {
                        drawList->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), brushOutlineColor, false, 2.5f);
                    }
                }
                else // Square
                {
                    float halfM = radiusMeters;
                    Vector3 corners[5] = {
                        { hitPoint.x - halfM, 0.0f, hitPoint.z - halfM },
                        { hitPoint.x + halfM, 0.0f, hitPoint.z - halfM },
                        { hitPoint.x + halfM, 0.0f, hitPoint.z + halfM },
                        { hitPoint.x - halfM, 0.0f, hitPoint.z + halfM },
                        { hitPoint.x - halfM, 0.0f, hitPoint.z - halfM }
                    };

                    std::vector<ImVec2> screenPoints;
                    for (int i = 0; i < 5; ++i)
                    {
                        corners[i].y = g_Engine.GetSurfaceHeight(corners[i].x, corners[i].z) + 0.05f;
                        ImVec2 sp;
                        if (ProjectToScreen(corners[i], screenW, screenH, sp))
                        {
                            screenPoints.push_back(sp);
                        }
                    }
                    if (screenPoints.size() >= 4)
                    {
                        drawList->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), brushOutlineColor, false, 2.5f);
                    }
                }

                // Center indicator dot
                ImVec2 centerSp;
                if (ProjectToScreen(hitPoint, screenW, screenH, centerSp))
                {
                    drawList->AddCircleFilled(centerSp, 3.5f, brushOutlineColor);
                }

                // 2.5. Draw 512x512 Tile Boundary on 3D Landscape
                if (g_MaskPainter.m_showTile3DBorder && g_MaskPainter.IsLoaded())
                {
                    int tileSize = g_MaskPainter.m_tileSize;
                    if (tileSize <= 0) tileSize = 512;
                    int curTileX = static_cast<int>(std::floor(hitPoint.x)) / tileSize;
                    int curTileZ = static_cast<int>(std::floor(hitPoint.z)) / tileSize;

                    TileMaterialInfo curTileInfo = g_MaskPainter.GetTileInfo(curTileX, curTileZ, tileSize, g_MaskPainter.m_maxAllowedMaterials);

                    ImU32 tileEdgeCol = curTileInfo.hasWarning ? IM_COL32(255, 40, 40, 240) : IM_COL32(0, 220, 255, 200);
                    float tileEdgeThickness = curTileInfo.hasWarning ? 3.0f : 1.8f;

                    float tileMinX = static_cast<float>(curTileX * tileSize);
                    float tileMaxX = tileMinX + static_cast<float>(tileSize);
                    float tileMinZ = static_cast<float>(curTileZ * tileSize);
                    float tileMaxZ = tileMinZ + static_cast<float>(tileSize);

                    auto DrawTerrainEdge = [&](float x1, float z1, float x2, float z2) {
                        float dist = std::hypot(x2 - x1, z2 - z1);
                        int steps = std::max(4, static_cast<int>(dist / 16.0f));
                        ImVec2 prevSp;
                        bool hasPrev = false;
                        for (int s = 0; s <= steps; ++s)
                        {
                            float t = static_cast<float>(s) / static_cast<float>(steps);
                            float px = x1 + (x2 - x1) * t;
                            float pz = z1 + (z2 - z1) * t;
                            float py = g_Engine.GetSurfaceHeight(px, pz) + 0.1f;
                            ImVec2 sp;
                            if (ProjectToScreen(Vector3(px, py, pz), screenW, screenH, sp))
                            {
                                if (hasPrev)
                                {
                                    drawList->AddLine(prevSp, sp, tileEdgeCol, tileEdgeThickness);
                                }
                                prevSp = sp;
                                hasPrev = true;
                            }
                            else
                            {
                                hasPrev = false;
                            }
                        }
                    };

                    DrawTerrainEdge(tileMinX, tileMinZ, tileMaxX, tileMinZ);
                    DrawTerrainEdge(tileMaxX, tileMinZ, tileMaxX, tileMaxZ);
                    DrawTerrainEdge(tileMaxX, tileMaxZ, tileMinX, tileMaxZ);
                    DrawTerrainEdge(tileMinX, tileMaxZ, tileMinX, tileMinZ);
                }

                // 3. Painting Trigger (Paints onto Overlay Layer in Mask Painting Mode)
                bool isMouseDown = isWindowFocused && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
                ImGuiIO& io = ImGui::GetIO();

                if (DX11Hook::s_maskPaintMode && canInteractTerrain && !io.WantCaptureMouse && isMouseDown)
                {
                    g_MaskPainter.StartStroke();
                    g_MaskPainter.PaintAtWorldPos(
                        hitPoint.x,
                        hitPoint.z,
                        mapSizeMeters,
                        static_cast<uint8_t>(g_selectedLayerIndex),
                        DX11Hook::s_isEraserActive
                    );
                }
                else
                {
                    g_MaskPainter.EndStroke();
                }
            }
            else
            {
                g_MaskPainter.EndStroke();
            }
        }
        else
        {
            g_MaskPainter.EndStroke();
        }

        // On-Screen HUD Badge for Mask Painting Mode
        if (DX11Hook::s_maskPaintMode)
        {
            ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
            char hudText[192];
            if (isGameMenuOpen)
            {
                snprintf(hudText, sizeof(hudText), "● MASK PAINTER [PAUSED - GAME MENU]  |  Press ESC to Resume");
            }
            else if (!isWindowFocused)
            {
                snprintf(hudText, sizeof(hudText), "● MASK PAINTER [INACTIVE - WINDOW UNFOCUSED]");
            }
            else
            {
                snprintf(hudText, sizeof(hudText), "● MASK PAINTER MODE [ACTIVE]  |  [F] Switch Buldozer Mode  |  [E] Eraser: %s  |  [Ctrl+Z] Undo", DX11Hook::s_isEraserActive ? "ON" : "OFF");
            }

            ImVec2 txtSize = ImGui::CalcTextSize(hudText);
            float pad = 8.0f;
            float boxX = (screenW - txtSize.x) * 0.5f - pad;
            float boxY = 16.0f;

            ImU32 bgCol = isGameMenuOpen ? IM_COL32(40, 30, 10, 210) : (!isWindowFocused ? IM_COL32(30, 30, 30, 210) : IM_COL32(15, 30, 20, 210));
            ImU32 borderCol = isGameMenuOpen ? IM_COL32(230, 160, 30, 240) : (!isWindowFocused ? IM_COL32(120, 120, 120, 200) : IM_COL32(40, 220, 100, 240));
            ImU32 textCol = isGameMenuOpen ? IM_COL32(255, 200, 80, 255) : (!isWindowFocused ? IM_COL32(180, 180, 180, 255) : IM_COL32(80, 255, 140, 255));

            bgDraw->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + txtSize.x + pad * 2.0f, boxY + txtSize.y + pad * 2.0f), bgCol, 6.0f);
            bgDraw->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + txtSize.x + pad * 2.0f, boxY + txtSize.y + pad * 2.0f), borderCol, 6.0f, 0, 1.5f);
            bgDraw->AddText(ImVec2(boxX + pad, boxY + pad), textCol, hudText);
        }

        // -----------------------------------------------------------------------------------------
        // WINDOW 1: MAIN CONTROL PANEL
        // -----------------------------------------------------------------------------------------
        if (DX11Hook::s_showMenu)
        {
            ImGui::SetNextWindowSize(ImVec2(500, 720), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Buldozer Landscape Mask Painter [v1.3]", &DX11Hook::s_showMenu))
            {
                uintptr_t world = g_Engine.GetWorld();
                ImGui::TextColored(world ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                                   "● Buldozer Status: %s", world ? "CONNECTED" : "WAITING FOR BULDOZER");

                if (isGameMenuOpen)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "● Game Menu / Pause: OPEN (Painting suspended)");
                }
                else if (!isWindowFocused)
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "● Window Focus: BACKGROUND (Painting suspended)");
                }

                uintptr_t activeMenu = g_Engine.GetActiveMenu();
                HWND fgHwnd = GetForegroundWindow();
                bool fgFocused = (fgHwnd == DX11Hook::s_gameHwnd);
                ImGui::Text("Window Focused: %s | Active Menu: 0x%p", fgFocused ? "YES" : "NO", reinterpret_cast<void*>(activeMenu));

                ImGui::Checkbox("Pause on DayZ Menu", &DX11Hook::s_detectPauseMenu);
                ImGui::SameLine();
                ImGui::Checkbox("Ignore When Unfocused", &DX11Hook::s_detectWindowFocus);

                if (cam)
                {
                    ImGui::Text("Camera: (%.1f, %.1f, %.1f) | Focal: (%.2f, %.2f)",
                                cam->position.x, cam->position.y, cam->position.z, cam->projX, cam->projY);
                    if (hasHitTerrain)
                    {
                        int hx = static_cast<int>(std::floor(hitPoint.x));
                        int hz = static_cast<int>(std::floor(hitPoint.z));
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f),
                                           "Terrain Hit: (%.1f, %.1f, %.1f) [1x1m Cell: %d, %d]", hitPoint.x, hitPoint.y, hitPoint.z, hx, hz);
                    }
                }

                ImGui::Separator();

                // Buldozer Tool Mode Selector & Status
                uint8_t curBuldMode = g_Engine.GetBuldozerMode();
                uint8_t brushSubMode = g_Engine.GetBrushSubMode();
                const char* brushSubNames[] = { "Normal", "Flatten", "Smooth", "Height set" };
                const char* curBrushSub = (brushSubMode < 4) ? brushSubNames[brushSubMode] : "Unknown";

                ImGui::Text("Buldozer Tool Mode [Cycle with key 'F']:");
                if (curBuldMode == 1)
                {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "● Current Mode: 1 - Objects Mode (Native Buldozer)");
                    ImGui::TextDisabled("   LMB selects & drags objects. Mask Painter is dormant (3D preview hidden).");
                }
                else if (curBuldMode == 2)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "● Current Mode: 2 - Brush Tool: %s (Native Buldozer)", curBrushSub);
                    ImGui::TextDisabled("   LMB modifies terrain height. Press [G] to cycle brush sub-mode. 3D preview hidden.");
                }
                else if (curBuldMode == 3)
                {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "● Current Mode: 3 - Mask Painter Mode [ACTIVE]");
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "   LMB paints terrain mask! Landscape height & objects are completely protected.");
                }

                if (ImGui::Button(curBuldMode == 1 ? "✔ 1. Objects Mode" : "1. Objects Mode", ImVec2(140, 28)))
                {
                    DX11Hook::SetBuldozerMode(1);
                }
                ImGui::SameLine();
                if (ImGui::Button(curBuldMode == 2 ? "✔ 2. Brush Tool" : "2. Brush Tool", ImVec2(140, 28)))
                {
                    DX11Hook::SetBuldozerMode(2);
                }
                ImGui::SameLine();
                if (ImGui::Button(curBuldMode == 3 ? "✔ 3. Mask Painter" : "3. Mask Painter", ImVec2(140, 28)))
                {
                    DX11Hook::SetBuldozerMode(3);
                }

                ImGui::Separator();

                // ---------------------------------------------------------------------------------
                // SECTION 1: LAYERS.CFG
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("1. Surface Layers (layers.cfg)", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::InputText("Path", g_layersCfgPath, sizeof(g_layersCfgPath));
                    ImGui::SameLine();
                    if (ImGui::Button("Load"))
                    {
                        if (g_LayersParser.LoadFromFile(g_layersCfgPath))
                        {
                            g_MaskPainter.SetPalette(g_LayersParser.GetLayers());
                        }
                    }

                    const auto& layers = g_MaskPainter.IsLoaded() ? g_MaskPainter.GetPalette() : g_LayersParser.GetLayers();
                    if (!layers.empty())
                    {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Loaded %d layers", static_cast<int>(layers.size()));

                        if (g_selectedLayerIndex >= static_cast<int>(layers.size()))
                            g_selectedLayerIndex = 0;

                        std::string previewText = layers[g_selectedLayerIndex].GetDisplayName();
                        if (ImGui::BeginCombo("Active Layer", previewText.c_str()))
                        {
                            for (int i = 0; i < static_cast<int>(layers.size()); ++i)
                            {
                                const bool isSelected = (g_selectedLayerIndex == i);
                                const auto& layer = layers[i];

                                ImVec4 col(layer.r / 255.f, layer.g / 255.f, layer.b / 255.f, 1.0f);
                                ImGui::PushID(i);
                                ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                                ImGui::SameLine();
                                if (ImGui::Selectable(layer.name.c_str(), isSelected))
                                {
                                    g_selectedLayerIndex = i;
                                }
                                ImGui::PopID();

                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        const auto& curLayer = layers[g_selectedLayerIndex];
                        ImVec4 curCol(curLayer.r / 255.f, curLayer.g / 255.f, curLayer.b / 255.f, 1.0f);
                        ImGui::ColorButton("Selected Color", curCol, 0, ImVec2(32, 24));
                        ImGui::SameLine();
                        ImGui::Text("%s -> Index %d | RGB: (%d, %d, %d)", curLayer.name.c_str(), g_selectedLayerIndex, curLayer.r, curLayer.g, curLayer.b);
                    }
                }

                // ---------------------------------------------------------------------------------
                // SECTION 2: MULTI-LAYER CANVAS & EXPORT
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("2. Multi-Layer Canvas & Export", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::InputText("Mask File (.bmp)", g_maskFilePath, sizeof(g_maskFilePath));

                    if (ImGui::Button("Load Base Mask"))
                    {
                        g_MaskPainter.LoadBaseMask(g_maskFilePath, g_LayersParser.GetLayers());
                    }

                    if (g_MaskPainter.IsLoaded())
                    {
                        float memMB = static_cast<float>(g_MaskPainter.GetMemoryBytes()) / (1024.0f * 1024.0f);
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                           "Active Canvas: %dx%d px (1 px = 1m)", g_MaskPainter.GetWidth(), g_MaskPainter.GetHeight());
                        ImGui::Text("Base Mask + Overlay Layer | RAM: %.1f MB", memMB);
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                                           "Painted Overlay Cells: %zu pixels", g_MaskPainter.GetPaintedOverlayPixelCount());

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "Export Options:");

                        // 1. Export Merged Mask
                        if (ImGui::Button("💾 Save Merged Mask (8-bit BMP)"))
                        {
                            g_MaskPainter.SaveMergedMaskBMP(g_maskFilePath);
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Single ready mask for DayZ");

                        // 2. Export Overlay Layer
                        ImGui::InputText("Overlay PNG", g_overlayPngPath, sizeof(g_overlayPngPath));
                        if (ImGui::Button("💾 Export Overlay (PNG with Alpha)"))
                        {
                            g_MaskPainter.SaveOverlayPNG(g_overlayPngPath);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("💾 Export Overlay (8-bit BMP)"))
                        {
                            g_MaskPainter.SaveOverlayBMP("mask_overlay.bmp");
                        }
                        ImGui::TextDisabled("Transparent overlay for Photoshop layers");

                        ImGui::Separator();
                        if (ImGui::Button("Undo Stroke (Ctrl+Z)") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)))
                        {
                            g_MaskPainter.Undo();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("🧹 Clear Overlay (Restore Original Mask)"))
                        {
                            g_MaskPainter.ClearOverlay();
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("No base mask loaded. Create a new canvas below:");
                        ImGui::InputInt("Width [px]", &g_newMaskWidth);
                        ImGui::InputInt("Height [px]", &g_newMaskHeight);
                        if (ImGui::Button("Create New Canvas"))
                        {
                            g_MaskPainter.CreateNewMask(g_newMaskWidth, g_newMaskHeight, 0, g_LayersParser.GetLayers());
                        }
                    }
                }

                // ---------------------------------------------------------------------------------
                // SECTION 3: 512x512 TILE INSPECTOR & ENGINE LIMITS
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("3. 512x512 Tile Inspector & Engine Limits", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::InputInt("Tile Size [meters / px]", &g_MaskPainter.m_tileSize);
                    if (g_MaskPainter.m_tileSize < 64) g_MaskPainter.m_tileSize = 64;
                    if (g_MaskPainter.m_tileSize > 4096) g_MaskPainter.m_tileSize = 4096;

                    ImGui::SliderInt("Max Materials Per Tile", &g_MaskPainter.m_maxAllowedMaterials, 1, 8);
                    ImGui::TextDisabled("DayZ terrain engine limit is 4 materials per tile! Exceeding causes pale white artifacts.");

                    ImGui::Checkbox("Show 512m Tile Grid on Mini-Map", &g_MaskPainter.m_showTileGrid);
                    ImGui::SameLine();
                    ImGui::Checkbox("Show Tile Badges", &g_MaskPainter.m_showTileCounts);
                    ImGui::Checkbox("Show 512m Tile Border on 3D Terrain", &g_MaskPainter.m_showTile3DBorder);

                    if (g_MaskPainter.IsLoaded())
                    {
                        // Current Tile under cursor info
                        TileMaterialInfo curT = g_MaskPainter.GetTileInfoAtWorld(hitPoint.x, hitPoint.z, mapSizeMeters, g_MaskPainter.m_tileSize, g_MaskPainter.m_maxAllowedMaterials);

                        std::string curLayersList = "";
                        for (size_t li = 0; li < curT.layerIndices.size(); ++li)
                        {
                            TerrainLayer tl = g_MaskPainter.GetPaletteLayer(curT.layerIndices[li]);
                            if (li > 0) curLayersList += ", ";
                            curLayersList += tl.name;
                        }

                        ImGui::Separator();
                        ImGui::Text("Current Tile at Cursor: [%d, %d]", curT.tileX, curT.tileZ);
                        if (curT.hasWarning)
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                "⚠️ Materials in Tile: %d / %d (EXCEEDS ENGINE LIMIT!)", curT.materialCount, g_MaskPainter.m_maxAllowedMaterials);
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "   Surfaces: [%s]", curLayersList.c_str());
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                "✅ Materials in Tile: %d / %d (OK)", curT.materialCount, g_MaskPainter.m_maxAllowedMaterials);
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "   Surfaces: [%s]", curLayersList.c_str());
                        }

                        // Full map scan tool
                        static TileScanSummary s_scanSummary;
                        static bool s_hasScanned = false;

                        ImGui::Separator();
                        if (ImGui::Button("🔍 Scan All Map Tiles for > 4 Material Violations"))
                        {
                            s_scanSummary = g_MaskPainter.ScanAllTiles(g_MaskPainter.m_tileSize, g_MaskPainter.m_maxAllowedMaterials);
                            s_hasScanned = true;
                        }

                        if (s_hasScanned)
                        {
                            if (s_scanSummary.violatingCount == 0)
                            {
                                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                                    "🎉 All %d tiles are clean! No tiles exceed %d materials.", s_scanSummary.totalTiles, g_MaskPainter.m_maxAllowedMaterials);
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                                    "⚠️ Found %d / %d tiles exceeding %d materials limit:",
                                    s_scanSummary.violatingCount, s_scanSummary.totalTiles, g_MaskPainter.m_maxAllowedMaterials);

                                ImGui::BeginChild("ViolatingTilesList", ImVec2(0, 150), true);
                                for (size_t vi = 0; vi < s_scanSummary.violatingTiles.size(); ++vi)
                                {
                                    const auto& vt = s_scanSummary.violatingTiles[vi];
                                    ImGui::PushID(static_cast<int>(vi));
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                        "Tile [%d, %d]: %d mats", vt.tileX, vt.tileZ, vt.materialCount);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("View on Map"))
                                    {
                                        g_minimapFollowBrush = false;
                                        g_minimapFollowCamera = false;
                                        g_minimapCenterX = static_cast<float>(vt.tileX * g_MaskPainter.m_tileSize + g_MaskPainter.m_tileSize / 2);
                                        g_minimapCenterZ = static_cast<float>(vt.tileZ * g_MaskPainter.m_tileSize + g_MaskPainter.m_tileSize / 2);
                                        DX11Hook::s_showMinimap = true;
                                    }
                                    std::string vtLayers = "";
                                    for (size_t li = 0; li < vt.layerIndices.size(); ++li)
                                    {
                                        TerrainLayer tl = g_MaskPainter.GetPaletteLayer(vt.layerIndices[li]);
                                        if (li > 0) vtLayers += ", ";
                                        vtLayers += tl.name;
                                    }
                                    ImGui::TextDisabled("  [%s]", vtLayers.c_str());
                                    ImGui::PopID();
                                }
                                ImGui::EndChild();
                            }
                        }
                    }
                }

                // ---------------------------------------------------------------------------------
                // SECTION 4: 3D TERRAIN SURFACE VISUALIZATION
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("4. 3D Terrain Surface Visualization", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Checkbox("Render 1:1 Meter Pixel Quads on 3D Surface", &g_MaskPainter.m_showPixelQuadsOnTerrain);
                    ImGui::Checkbox("Show 1x1m Pixel Grid Lines", &g_MaskPainter.m_showPixelGridLines);
                    ImGui::SliderInt("Surface Preview Radius [m]", &g_MaskPainter.m_surfacePreviewRadiusMeters, 10, 50);
                    ImGui::SliderFloat("Surface Overlay Opacity", &g_MaskPainter.m_surfaceAlpha, 0.1f, 1.0f, "%.2f");
                }

                // ---------------------------------------------------------------------------------
                // SECTION 5: BRUSH SETTINGS & HOTKEYS
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("5. Brush & Controls", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::SliderInt("Brush Radius [m / px]", &g_MaskPainter.m_brushRadiusPx, 1, 128);

                    if (g_MaskPainter.IsLoaded())
                    {
                        float mPerPx = mapSizeMeters / static_cast<float>(g_MaskPainter.GetWidth());
                        float rMeters = static_cast<float>(g_MaskPainter.m_brushRadiusPx) * mPerPx;
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                           "Coverage: Diameter = %.1f meters (%d x %d pixels)", rMeters * 2.0f, g_MaskPainter.m_brushRadiusPx * 2, g_MaskPainter.m_brushRadiusPx * 2);
                    }

                    int shapeInt = static_cast<int>(g_MaskPainter.m_brushShape);
                    ImGui::RadioButton("Circle", &shapeInt, 0); ImGui::SameLine();
                    ImGui::RadioButton("Square", &shapeInt, 1);
                    g_MaskPainter.m_brushShape = static_cast<BrushShape>(shapeInt);

                    ImGui::Separator();
                    ImGui::Checkbox("Eraser Mode (Key: E)", &DX11Hook::s_isEraserActive);
                    ImGui::TextDisabled("Eraser clears painted overlay and restores original base mask underneath!");

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Controls Guide:");
                    ImGui::BulletText("[F] : Toggle Mask Painting Mode (Isolated mode: no object moving, no terrain flattening)");
                    ImGui::BulletText("[Left Click / ALT + Click] : Paint on Landscape or Mini-Map (in Mask Mode)");
                    ImGui::BulletText("[E] : Toggle Eraser (restores original base mask underneath)");
                    ImGui::BulletText("[Ctrl + Z] : Undo last stroke");
                    ImGui::BulletText("[Ctrl + S] : Save merged 8-bit mask");
                    ImGui::BulletText("[Ctrl + M] : Toggle Mini-Map Window");
                    ImGui::BulletText("[INSERT / F11] : Hide/Show Control Panel");
                }

                // ---------------------------------------------------------------------------------
                // SECTION 5: WORLD & MAP SETTINGS
                // ---------------------------------------------------------------------------------
                if (ImGui::CollapsingHeader("6. World & Map Settings"))
                {
                    ImGui::Checkbox("Auto-Detect Terrain Size from Buldozer", &DX11Hook::s_useAutoMapSize);
                    if (!DX11Hook::s_useAutoMapSize)
                    {
                        ImGui::InputFloat("Manual Map Size (m)", &DX11Hook::s_manualMapSize, 1024.0f, 2048.0f, "%.1f");
                    }

                    float autoDetectedSize = g_Engine.GetTerrainSizeMeters();
                    ImGui::Text("Buldozer World Size (g_Game.GetWorldSize): %.1f meters", autoDetectedSize);
                    ImGui::Text("Active Scale: 1 px = %.2f m",
                                g_MaskPainter.IsLoaded() ? (mapSizeMeters / g_MaskPainter.GetWidth()) : 1.0f);
                }
            }
            ImGui::End();
        }

        // -----------------------------------------------------------------------------------------
        // WINDOW 2: INTERACTIVE MINI-MAP & 1:1 PIXEL CANVAS INSPECTOR
        // -----------------------------------------------------------------------------------------
        if (DX11Hook::s_showMinimap)
        {
            ImGui::SetNextWindowSize(ImVec2(540, 600), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("🗺️ Terrain Mask Mini-Map & 1:1 Inspector", &DX11Hook::s_showMinimap))
            {
                // Top Toolbar
                ImGui::SliderFloat("Zoom", &g_minimapZoom, 0.05f, 32.0f, "%.2fx");
                ImGui::SameLine();
                if (ImGui::Button(" - ")) g_minimapZoom = std::max(0.05f, g_minimapZoom * 0.75f);
                ImGui::SameLine();
                if (ImGui::Button(" + ")) g_minimapZoom = std::min(32.0f, g_minimapZoom * 1.33f);

                // View Mode
                int vMode = static_cast<int>(g_MaskPainter.m_viewMode);
                ImGui::RadioButton("Composited", &vMode, 0); ImGui::SameLine();
                ImGui::RadioButton("Overlay Only", &vMode, 1); ImGui::SameLine();
                ImGui::RadioButton("Base Only", &vMode, 2);
                g_MaskPainter.m_viewMode = static_cast<MaskViewMode>(vMode);

                ImGui::Checkbox("Follow Brush", &g_minimapFollowBrush);
                ImGui::SameLine();
                ImGui::Checkbox("Follow Cam", &g_minimapFollowCamera);
                ImGui::SameLine();
                ImGui::Checkbox("1x1m Grid", &g_minimapShowGrid);
                ImGui::SameLine();
                ImGui::Checkbox("512m Tiles", &g_MaskPainter.m_showTileGrid);
                ImGui::SameLine();
                if (ImGui::Button("Center"))
                {
                    g_minimapCenterX = mapSizeMeters * 0.5f;
                    g_minimapCenterZ = mapSizeMeters * 0.5f;
                }

                if (g_minimapFollowBrush && hasHitTerrain)
                {
                    g_minimapCenterX = hitPoint.x;
                    g_minimapCenterZ = hitPoint.z;
                }
                else if (g_minimapFollowCamera && cam)
                {
                    g_minimapCenterX = cam->position.x;
                    g_minimapCenterZ = cam->position.z;
                }

                // Viewport Canvas Area
                ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                ImVec2 canvasAvail = ImGui::GetContentRegionAvail();
                float canvasSize = std::min(canvasAvail.x, canvasAvail.y - 45.0f);
                if (canvasSize < 100.0f) canvasSize = 100.0f;
                ImVec2 viewportSize(canvasSize, canvasSize);

                ImGui::InvisibleButton("##minimapCanvas", viewportSize);
                bool isCanvasHovered = ImGui::IsItemHovered();
                bool isCanvasActive = ImGui::IsItemActive();

                ImDrawList* drawList = ImGui::GetWindowDrawList();

                ImGuiIO& io = ImGui::GetIO();
                if (isCanvasHovered || isCanvasActive)
                {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                    {
                        g_minimapFollowBrush = false;
                        g_minimapFollowCamera = false;
                        g_minimapCenterX -= io.MouseDelta.x / g_minimapZoom;
                        g_minimapCenterZ += io.MouseDelta.y / g_minimapZoom;
                    }

                    if (io.MouseWheel != 0.0f)
                    {
                        float zoomFactor = (io.MouseWheel > 0.0f) ? 1.25f : 0.80f;
                        g_minimapZoom = std::clamp(g_minimapZoom * zoomFactor, 0.05f, 32.0f);
                    }
                }

                // Render Canvas Texture
                ID3D11ShaderResourceView* pTexView = g_MaskPainter.GetOrCreateMinimapTexture(
                    g_pd3dDevice,
                    g_minimapCenterX,
                    g_minimapCenterZ,
                    g_minimapZoom,
                    mapSizeMeters
                );

                if (pTexView)
                {
                    drawList->AddImage(
                        reinterpret_cast<ImTextureID>(pTexView),
                        canvasPos,
                        ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize)
                    );
                }
                else
                {
                    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), IM_COL32(30, 30, 30, 255));
                }

                drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), IM_COL32(100, 100, 100, 255), 0.0f, 0, 1.5f);

                float halfDim = canvasSize * 0.5f;
                auto WorldToCanvas = [&](float wx, float wz) -> ImVec2 {
                    float cx = canvasPos.x + halfDim + (wx - g_minimapCenterX) * g_minimapZoom;
                    float cy = canvasPos.y + halfDim - (wz - g_minimapCenterZ) * g_minimapZoom;
                    return ImVec2(cx, cy);
                };

                auto CanvasToWorld = [&](const ImVec2& cp) -> Vector3 {
                    float wx = g_minimapCenterX + (cp.x - (canvasPos.x + halfDim)) / g_minimapZoom;
                    float wz = g_minimapCenterZ - (cp.y - (canvasPos.y + halfDim)) / g_minimapZoom;
                    return Vector3(wx, 0.0f, wz);
                };

                // 1x1 Meter Pixel Grid
                if (g_minimapShowGrid && g_minimapZoom >= 3.5f)
                {
                    float minVisX = g_minimapCenterX - halfDim / g_minimapZoom;
                    float maxVisX = g_minimapCenterX + halfDim / g_minimapZoom;
                    float minVisZ = g_minimapCenterZ - halfDim / g_minimapZoom;
                    float maxVisZ = g_minimapCenterZ + halfDim / g_minimapZoom;

                    int startGridX = static_cast<int>(std::floor(minVisX));
                    int endGridX = static_cast<int>(std::ceil(maxVisX));
                    int startGridZ = static_cast<int>(std::floor(minVisZ));
                    int endGridZ = static_cast<int>(std::ceil(maxVisZ));

                    ImU32 gridColor = (g_minimapZoom >= 8.0f) ? IM_COL32(255, 255, 255, 45) : IM_COL32(255, 255, 255, 25);
                    ImU32 majorGridColor = IM_COL32(255, 255, 255, 80);

                    for (int gx = startGridX; gx <= endGridX; ++gx)
                    {
                        ImVec2 p0 = WorldToCanvas(static_cast<float>(gx), minVisZ);
                        ImVec2 p1 = WorldToCanvas(static_cast<float>(gx), maxVisZ);
                        if (p0.x >= canvasPos.x && p0.x <= canvasPos.x + canvasSize)
                        {
                            bool isMajor = (gx % 10 == 0);
                            drawList->AddLine(
                                ImVec2(p0.x, std::max(canvasPos.y, p1.y)),
                                ImVec2(p0.x, std::min(canvasPos.y + canvasSize, p0.y)),
                                isMajor ? majorGridColor : gridColor,
                                isMajor ? 1.5f : 1.0f
                            );
                        }
                    }

                    for (int gz = startGridZ; gz <= endGridZ; ++gz)
                    {
                        ImVec2 p0 = WorldToCanvas(minVisX, static_cast<float>(gz));
                        ImVec2 p1 = WorldToCanvas(maxVisX, static_cast<float>(gz));
                        if (p0.y >= canvasPos.y && p0.y <= canvasPos.y + canvasSize)
                        {
                            bool isMajor = (gz % 10 == 0);
                            drawList->AddLine(
                                ImVec2(std::max(canvasPos.x, p0.x), p0.y),
                                ImVec2(std::min(canvasPos.x + canvasSize, p1.x), p0.y),
                                isMajor ? majorGridColor : gridColor,
                                isMajor ? 1.5f : 1.0f
                            );
                        }
                    }
                }

                // 512x512 Tile Grid & Material Counters on Mini-Map
                if (g_MaskPainter.m_showTileGrid && g_MaskPainter.IsLoaded())
                {
                    int tileSize = g_MaskPainter.m_tileSize;
                    if (tileSize <= 0) tileSize = 512;
                    int maxAllowed = g_MaskPainter.m_maxAllowedMaterials;

                    float minVisX = g_minimapCenterX - halfDim / g_minimapZoom;
                    float maxVisX = g_minimapCenterX + halfDim / g_minimapZoom;
                    float minVisZ = g_minimapCenterZ - halfDim / g_minimapZoom;
                    float maxVisZ = g_minimapCenterZ + halfDim / g_minimapZoom;

                    int maxTilesX = (g_MaskPainter.GetWidth() + tileSize - 1) / tileSize;
                    int maxTilesZ = (g_MaskPainter.GetHeight() + tileSize - 1) / tileSize;

                    int startTileX = std::max(0, static_cast<int>(std::floor(minVisX / static_cast<float>(tileSize))));
                    int endTileX = std::min(maxTilesX - 1, static_cast<int>(std::floor(maxVisX / static_cast<float>(tileSize))));
                    int startTileZ = std::max(0, static_cast<int>(std::floor(minVisZ / static_cast<float>(tileSize))));
                    int endTileZ = std::min(maxTilesZ - 1, static_cast<int>(std::floor(maxVisZ / static_cast<float>(tileSize))));

                    for (int tz = startTileZ; tz <= endTileZ; ++tz)
                    {
                        for (int tx = startTileX; tx <= endTileX; ++tx)
                        {
                            float wx0 = static_cast<float>(tx * tileSize);
                            float wx1 = wx0 + static_cast<float>(tileSize);
                            float wz0 = static_cast<float>(tz * tileSize);
                            float wz1 = wz0 + static_cast<float>(tileSize);

                            ImVec2 pTL = WorldToCanvas(wx0, wz1);
                            ImVec2 pBR = WorldToCanvas(wx1, wz0);

                            float boxX0 = std::max(canvasPos.x, pTL.x);
                            float boxY0 = std::max(canvasPos.y, pTL.y);
                            float boxX1 = std::min(canvasPos.x + canvasSize, pBR.x);
                            float boxY1 = std::min(canvasPos.y + canvasSize, pBR.y);

                            if (boxX1 <= boxX0 || boxY1 <= boxY0) continue;

                            TileMaterialInfo tInfo = g_MaskPainter.GetTileInfo(tx, tz, tileSize, maxAllowed);

                            if (tInfo.hasWarning)
                            {
                                drawList->AddRectFilled(ImVec2(boxX0, boxY0), ImVec2(boxX1, boxY1), IM_COL32(255, 30, 30, 45));
                            }

                            ImU32 borderCol = tInfo.hasWarning ? IM_COL32(255, 50, 50, 240) : IM_COL32(0, 220, 255, 180);
                            float borderThick = tInfo.hasWarning ? 2.5f : 1.5f;

                            if (pTL.x >= canvasPos.x && pTL.x <= canvasPos.x + canvasSize)
                                drawList->AddLine(ImVec2(pTL.x, boxY0), ImVec2(pTL.x, boxY1), borderCol, borderThick);
                            if (pBR.x >= canvasPos.x && pBR.x <= canvasPos.x + canvasSize)
                                drawList->AddLine(ImVec2(pBR.x, boxY0), ImVec2(pBR.x, boxY1), borderCol, borderThick);
                            if (pTL.y >= canvasPos.y && pTL.y <= canvasPos.y + canvasSize)
                                drawList->AddLine(ImVec2(boxX0, pTL.y), ImVec2(boxX1, pTL.y), borderCol, borderThick);
                            if (pBR.y >= canvasPos.y && pBR.y <= canvasPos.y + canvasSize)
                                drawList->AddLine(ImVec2(boxX0, pBR.y), ImVec2(boxX1, pBR.y), borderCol, borderThick);

                            if (g_MaskPainter.m_showTileCounts)
                            {
                                char bText[96];
                                if (tInfo.hasWarning)
                                    snprintf(bText, sizeof(bText), "⚠️ [%d, %d]: %d mats (>%d!)", tx, tz, tInfo.materialCount, maxAllowed);
                                else
                                    snprintf(bText, sizeof(bText), "[%d, %d]: %d/%d mats", tx, tz, tInfo.materialCount, maxAllowed);

                                ImVec2 bTextSz = ImGui::CalcTextSize(bText);
                                float badgeX = std::clamp(pTL.x + 6.0f, canvasPos.x + 4.0f, canvasPos.x + canvasSize - bTextSz.x - 12.0f);
                                float badgeY = std::clamp(pTL.y + 6.0f, canvasPos.y + 4.0f, canvasPos.y + canvasSize - bTextSz.y - 8.0f);

                                ImVec2 bMin(badgeX, badgeY);
                                ImVec2 bMax(badgeX + bTextSz.x + 6.0f, badgeY + bTextSz.y + 4.0f);

                                drawList->AddRectFilled(bMin, bMax, tInfo.hasWarning ? IM_COL32(180, 20, 20, 220) : IM_COL32(15, 25, 35, 200), 3.0f);
                                drawList->AddRect(bMin, bMax, borderCol, 3.0f, 0, 1.0f);
                                drawList->AddText(ImVec2(bMin.x + 3.0f, bMin.y + 2.0f), IM_COL32(255, 255, 255, 255), bText);
                            }
                        }
                    }
                }

                // Brush footprint on mini-map
                if (hasHitTerrain)
                {
                    ImVec2 brushCenter = WorldToCanvas(hitPoint.x, hitPoint.z);
                    float brushRadiusCanvas = static_cast<float>(g_MaskPainter.m_brushRadiusPx) * g_minimapZoom;

                    TerrainLayer activeL = g_MaskPainter.GetPaletteLayer(static_cast<uint8_t>(g_selectedLayerIndex));
                    ImU32 brushCol = DX11Hook::s_isEraserActive ? IM_COL32(255, 50, 50, 160) : IM_COL32(activeL.r, activeL.g, activeL.b, 160);
                    ImU32 outlineCol = DX11Hook::s_isEraserActive ? IM_COL32(255, 50, 50, 255) : IM_COL32(activeL.r, activeL.g, activeL.b, 255);

                    if (g_MaskPainter.m_brushShape == BrushShape::Circle)
                    {
                        drawList->AddCircleFilled(brushCenter, brushRadiusCanvas, brushCol);
                        drawList->AddCircle(brushCenter, brushRadiusCanvas, outlineCol, 0, 1.5f);
                    }
                    else
                    {
                        ImVec2 bMin(brushCenter.x - brushRadiusCanvas, brushCenter.y - brushRadiusCanvas);
                        ImVec2 bMax(brushCenter.x + brushRadiusCanvas, brushCenter.y + brushRadiusCanvas);
                        drawList->AddRectFilled(bMin, bMax, brushCol);
                        drawList->AddRect(bMin, bMax, outlineCol, 0.0f, 0, 1.5f);
                    }
                }

                // Camera indicator on mini-map
                if (cam)
                {
                    ImVec2 camCanvasPos = WorldToCanvas(cam->position.x, cam->position.z);
                    if (camCanvasPos.x >= canvasPos.x && camCanvasPos.x <= canvasPos.x + canvasSize &&
                        camCanvasPos.y >= canvasPos.y && camCanvasPos.y <= canvasPos.y + canvasSize)
                    {
                        drawList->AddCircleFilled(camCanvasPos, 4.5f, IM_COL32(0, 255, 255, 255));
                        Vector3 fwd = cam->forward.Normalized();
                        ImVec2 arrowEnd(camCanvasPos.x + fwd.x * 16.0f, camCanvasPos.y - fwd.z * 16.0f);
                        drawList->AddLine(camCanvasPos, arrowEnd, IM_COL32(0, 255, 255, 255), 2.0f);
                    }
                }

                // Mouse Hover Readout & Direct 2D painting on mini-map
                Vector3 mouseWorld = CanvasToWorld(io.MousePos);
                int hoverCellX = static_cast<int>(std::floor(mouseWorld.x));
                int hoverCellZ = static_cast<int>(std::floor(mouseWorld.z));

                uint8_t hoverLayerIdx = 0;
                TerrainLayer hoverLayer;
                g_MaskPainter.GetLayerAtWorld(mouseWorld.x, mouseWorld.z, mapSizeMeters, hoverLayerIdx, hoverLayer);
                bool hasOver = g_MaskPainter.HasOverlay(hoverCellX, hoverCellZ);

                if (isCanvasHovered && DX11Hook::s_isAltHeld && io.MouseDown[0] && isWindowFocused && !isGameMenuOpen)
                {
                    g_MaskPainter.StartStroke();
                    g_MaskPainter.PaintAtWorldPos(
                        mouseWorld.x,
                        mouseWorld.z,
                        mapSizeMeters,
                        static_cast<uint8_t>(g_selectedLayerIndex),
                        DX11Hook::s_isEraserActive
                    );
                }
                else if (isCanvasHovered && !io.MouseDown[0])
                {
                    g_MaskPainter.EndStroke();
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f),
                                   "Cursor: X: %.1fm, Z: %.1fm [Cell: %d, %d] | Layer: %s (%s) RGB: (%d, %d, %d)",
                                   mouseWorld.x, mouseWorld.z, hoverCellX, hoverCellZ,
                                   hoverLayer.name.c_str(), hasOver ? "OVERLAY" : "BASE",
                                   hoverLayer.r, hoverLayer.g, hoverLayer.b);

                if (g_MaskPainter.IsLoaded())
                {
                    int curTileSize = g_MaskPainter.m_tileSize;
                    if (curTileSize <= 0) curTileSize = 512;
                    TileMaterialInfo cursorTileInfo = g_MaskPainter.GetTileInfoAtWorld(mouseWorld.x, mouseWorld.z, mapSizeMeters, curTileSize, g_MaskPainter.m_maxAllowedMaterials);

                    std::string tileLayersList = "";
                    for (size_t li = 0; li < cursorTileInfo.layerIndices.size(); ++li)
                    {
                        TerrainLayer tl = g_MaskPainter.GetPaletteLayer(cursorTileInfo.layerIndices[li]);
                        if (li > 0) tileLayersList += ", ";
                        tileLayersList += tl.name;
                    }

                    if (cursorTileInfo.hasWarning)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                            "⚠️ TILE [%d, %d] (%dm): %d MATERIALS (LIMIT > %d!) -> Will cause pale white blending artifacts in DayZ!",
                            cursorTileInfo.tileX, cursorTileInfo.tileZ, curTileSize, cursorTileInfo.materialCount, g_MaskPainter.m_maxAllowedMaterials);
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "   Surfaces in tile: [%s]", tileLayersList.c_str());
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                            "📦 Tile [%d, %d] (%dm): %d / %d materials (OK) | Surfaces: [%s]",
                            cursorTileInfo.tileX, cursorTileInfo.tileZ, curTileSize, cursorTileInfo.materialCount, g_MaskPainter.m_maxAllowedMaterials,
                            tileLayersList.c_str());
                    }
                }
            }
            ImGui::End();
        }

        // Render ImGui draw data
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    // Hooked IDXGISwapChain::ResizeBuffers
    static HRESULT __fastcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
    {
        CleanupRenderTarget();
        HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        CreateRenderTarget(pSwapChain);
        return hr;
    }

    static bool GetSwapChainVTable(void** outPresent, void** outResizeBuffers)
    {
        WNDCLASSEXA wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProcA, 0L, 0L, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "DX11DummyWindow", nullptr };
        RegisterClassExA(&wc);
        HWND hWnd = CreateWindowA("DX11DummyWindow", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hWnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* pDummySwapChain = nullptr;
        ID3D11Device* pDummyDevice = nullptr;
        ID3D11DeviceContext* pDummyContext = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &scd,
            &pDummySwapChain,
            &pDummyDevice,
            &featureLevel,
            &pDummyContext
        );

        if (FAILED(hr))
        {
            DestroyWindow(hWnd);
            UnregisterClassA("DX11DummyWindow", wc.hInstance);
            return false;
        }

        void** pVTable = *reinterpret_cast<void***>(pDummySwapChain);
        *outPresent = pVTable[8];
        *outResizeBuffers = pVTable[13];

        pDummySwapChain->Release();
        pDummyContext->Release();
        pDummyDevice->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("DX11DummyWindow", wc.hInstance);

        return true;
    }

    bool DX11Hook::Initialize()
    {
        if (s_isHooked) return true;

        if (MH_Initialize() != MH_OK)
            return false;

        void* fnPresent = nullptr;
        void* fnResizeBuffers = nullptr;

        if (!GetSwapChainVTable(&fnPresent, &fnResizeBuffers))
            return false;

        if (MH_CreateHook(fnPresent, reinterpret_cast<void*>(&HookedPresent), reinterpret_cast<void**>(&oPresent)) != MH_OK)
            return false;

        if (MH_CreateHook(fnResizeBuffers, reinterpret_cast<void*>(&HookedResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK)
            return false;

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
            return false;

        s_isHooked = true;
        return true;
    }

    void DX11Hook::Shutdown()
    {
        if (!s_isHooked) return;

        // Restore Buldozer to Objects mode (1) if currently in Mask Painter (3)
        SetBuldozerMode(1);

        UninstallSelectionTypeHook();

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        if (s_gameHwnd && oWndProc)
        {
            SetWindowLongPtrA(s_gameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupRenderTarget();
        g_MaskPainter.InvalidateTextures();

        if (g_pd3dDeviceContext)
        {
            g_pd3dDeviceContext->Release();
            g_pd3dDeviceContext = nullptr;
        }

        if (g_pd3dDevice)
        {
            g_pd3dDevice->Release();
            g_pd3dDevice = nullptr;
        }

        s_isHooked = false;
    }
}
