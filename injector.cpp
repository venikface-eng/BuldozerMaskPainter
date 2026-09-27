#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

struct ProcessEntry
{
    DWORD pid;
    std::string name;
};

std::vector<ProcessEntry> FindTargetProcesses()
{
    std::vector<ProcessEntry> results;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return results;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, name, sizeof(name), nullptr, nullptr);
            std::string sName(name);

            // Convert to lowercase for comparison
            std::string lower = sName;
            for (auto& c : lower) c = tolower(c);

            if (lower.find("dayzdiag") != std::string::npos ||
                lower.find("dayz") != std::string::npos ||
                lower.find("buldozer") != std::string::npos)
            {
                results.push_back({ pe.th32ProcessID, sName });
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return results;
}

bool InjectDLL(DWORD pid, const std::string& dllPath)
{
    std::string fullPath = fs::absolute(dllPath).string();
    if (!fs::exists(fullPath))
    {
        std::cout << "[ERROR] DLL file not found: " << fullPath << std::endl;
        return false;
    }

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc)
    {
        std::cout << "[ERROR] Failed to open process (PID: " << pid << "). Error: " << GetLastError() << std::endl;
        std::cout << "        Try running this injector as Administrator." << std::endl;
        return false;
    }

    size_t pathLen = fullPath.length() + 1;
    void* pRemoteBuf = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteBuf)
    {
        std::cout << "[ERROR] VirtualAllocEx failed in target process. Error: " << GetLastError() << std::endl;
        CloseHandle(hProc);
        return false;
    }

    if (!WriteProcessMemory(hProc, pRemoteBuf, fullPath.c_str(), pathLen, nullptr))
    {
        std::cout << "[ERROR] WriteProcessMemory failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProc, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    LPTHREAD_START_ROUTINE pfnLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA")
    );

    if (!pfnLoadLibrary)
    {
        std::cout << "[ERROR] Failed to get LoadLibraryA address." << std::endl;
        VirtualFreeEx(hProc, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pfnLoadLibrary, pRemoteBuf, 0, nullptr);
    if (!hThread)
    {
        std::cout << "[ERROR] CreateRemoteThread failed. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProc, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);

    VirtualFreeEx(hProc, pRemoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProc);

    std::cout << "[SUCCESS] DLL successfully injected into PID: " << pid << std::endl;
    return true;
}

int main()
{
    SetConsoleTitleA("Buldozer Mask Painter Injector");

    std::cout << "========================================================" << std::endl;
    std::cout << "     Buldozer Landscape Mask Painter - Injector         " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Detect DLL
    std::string dllPath = "BuldozerMaskPainter.dll";
    if (!fs::exists(dllPath))
    {
        if (fs::exists("bin/Release/BuldozerMaskPainter.dll"))
            dllPath = "bin/Release/BuldozerMaskPainter.dll";
        else if (fs::exists("../bin/Release/BuldozerMaskPainter.dll"))
            dllPath = "../bin/Release/BuldozerMaskPainter.dll";
    }

    std::cout << "[INFO] Using DLL: " << fs::absolute(dllPath).string() << std::endl << std::endl;

    auto targets = FindTargetProcesses();

    DWORD targetPID = 0;
    if (targets.empty())
    {
        std::cout << "[?] No DayZ / Buldozer process automatically detected." << std::endl;
        std::cout << "Enter target Process ID (PID) or process name: ";
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) return 0;

        try {
            targetPID = std::stoul(input);
        } catch (...) {
            // Find by name
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(snap, &pe)) {
                do {
                    char sBuf[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, sBuf, sizeof(sBuf), nullptr, nullptr);
                    if (input == sBuf) {
                        targetPID = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
    }
    else if (targets.size() == 1)
    {
        std::cout << "[+] Found target: " << targets[0].name << " (PID: " << targets[0].pid << ")" << std::endl;
        std::cout << "Inject now? (Y/n): ";
        std::string choice;
        std::getline(std::cin, choice);
        if (choice.empty() || choice[0] == 'y' || choice[0] == 'Y')
        {
            targetPID = targets[0].pid;
        }
        else
        {
            std::cout << "Cancelled." << std::endl;
            return 0;
        }
    }
    else
    {
        std::cout << "[+] Multiple target processes found:" << std::endl;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            std::cout << "  [" << (i + 1) << "] " << targets[i].name << " (PID: " << targets[i].pid << ")" << std::endl;
        }
        std::cout << "Select number (1-" << targets.size() << "): ";
        int sel = 1;
        std::cin >> sel;
        if (sel >= 1 && sel <= static_cast<int>(targets.size()))
            targetPID = targets[sel - 1].pid;
    }

    if (targetPID == 0)
    {
        std::cout << "[ERROR] No valid target process selected." << std::endl;
        return 1;
    }

    std::cout << "\n[INFO] Injecting into PID " << targetPID << "..." << std::endl;
    if (InjectDLL(targetPID, dllPath))
    {
        std::cout << "\n========================================================" << std::endl;
        std::cout << " Hotkeys in Buldozer:" << std::endl;
        std::cout << "   [F]       - Cycle Buldozer Tool Mode:" << std::endl;
        std::cout << "               1: Objects Mode (Native)" << std::endl;
        std::cout << "               2: Brush Tool (Native, G cycles Flatten/etc.)" << std::endl;
        std::cout << "               3: Mask Painter Mode (Active Painting, Terrain Safe)" << std::endl;
        std::cout << "   [INSERT]  - Toggle ImGui Menu" << std::endl;
        std::cout << "   [F11]     - Toggle ImGui Menu" << std::endl;
        std::cout << "   [Ctrl+M]  - Toggle Mini-Map" << std::endl;
        std::cout << "   [E]       - Toggle Eraser (restores original base mask)" << std::endl;
        std::cout << "   [Ctrl+Z]  - Undo Stroke" << std::endl;
        std::cout << "   [Ctrl+S]  - Save Merged Mask BMP" << std::endl;
        std::cout << "   [Ctrl+END]- Cleanly Unload / Detach DLL" << std::endl;
        std::cout << "========================================================" << std::endl;
    }

    std::cout << "\nPress Enter to exit injector...";
    std::cin.ignore();
    std::cin.get();
    return 0;
}
