#include <windows.h>
#include "dx11_hook.h"
#include <iostream>

DWORD WINAPI MainThread(LPVOID lpParam)
{
    HMODULE hModule = reinterpret_cast<HMODULE>(lpParam);

    // Allow the host process and DirectX subsystems to finish booting
    Sleep(1200);

    // Install DirectX 11 hooks and ImGui
    if (!DayZBuldozer::DX11Hook::Initialize())
    {
        OutputDebugStringA("[BuldozerMaskPainter] Failed to initialize DirectX 11 hooks.\n");
        return FALSE;
    }

    OutputDebugStringA("[BuldozerMaskPainter] Hooks successfully established!\n");

    // Background thread loop: monitor for manual uninject hotkey (END key)
    while (true)
    {
        Sleep(100);

        if ((GetAsyncKeyState(VK_END) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000))
        {
            // Ctrl + END triggers clean uninject
            break;
        }
    }

    // Cleanup hooks and uninject cleanly
    DayZBuldozer::DX11Hook::Shutdown();
    Sleep(200);

    FreeLibraryAndExitThread(hModule, 0);
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr)
        {
            DayZBuldozer::DX11Hook::Shutdown();
        }
        break;
    }
    return TRUE;
}
