#pragma once

#include <windows.h>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace DayZBuldozer
{
    // -----------------------------------------------------------------------------------------
    // DayZ Buldozer (DayZDiag_x64.exe) Reverse Engineering Findings
    // Analyzed via IDA Pro MCP for DayZDiag_x64.exe (DayZ 1.30)
    // -----------------------------------------------------------------------------------------

    // RVA Offsets (Base = 0x140000000)
    namespace Offsets
    {
        // World / Landscape static pointer
        // sub_140C26810: mov rcx, cs:qword_141B24F70
        constexpr uintptr_t WORLD_STATIC_PTR_RVA     = 0x1B24F70;

        // CGame::GetWorld pointer: sub_140C1B110 returns qword_141B0FBA8
        constexpr uintptr_t CGAME_WORLD_STATIC_RVA   = 0x1B0FBA8;

        // Engine / Camera root static pointer
        // sub_140C17E70: mov rax, cs:qword_141B132F0
        constexpr uintptr_t ENGINE_STATIC_PTR_RVA    = 0x1B132F0;

        // Offset from Engine* to CameraTransform*
        // sub_140C17E70: mov rcx, [rax + 118h] (35 * sizeof(uintptr_t))
        constexpr uintptr_t CAMERA_TRANSFORM_OFFSET  = 0x118;

        // Buldozer EditCursor instance pointer (sub_1406E9C10: qword_141B14420)
        constexpr uintptr_t BULDOZER_EDITCURSOR_RVA            = 0x1B14420;

        // EditCursor struct member offsets
        constexpr uintptr_t EDITCURSOR_MODE_OFFSET             = 0x584; // 1412 (1 = Objects, 2 = Brush, 3 = Mask Painter)
        constexpr uintptr_t EDITCURSOR_SUBMODE_OFFSET          = 0x601; // 1537 (0 = Normal, 1 = Flatten, 2 = Smooth, 3 = Height set)

        // Buldozer Selection Type (UABuldSelectionType) toggle logic RVA
        constexpr uintptr_t BULDOZER_SELECTION_TYPE_RVA        = 0x791EE4;
        constexpr uintptr_t BULDOZER_SELECTION_TYPE_RESUME_RVA = 0x791F1E;
        constexpr size_t    BULDOZER_SELECTION_TYPE_PATCH_LEN  = 26;

        // Buldozer Brush init function (sub_14078F630)
        constexpr uintptr_t BULDOZER_BRUSH_INIT_RVA            = 0x78F630;

        // Buldozer Status banner function (sub_14078AEB0)
        constexpr uintptr_t BULDOZER_STATUS_BANNER_RVA         = 0x78AEB0;

        // Buldozer Object hover/arrow update function (sub_14078F920)
        constexpr uintptr_t BULDOZER_OBJECT_HOVER_RVA          = 0x78F920;

        // Buldozer Notification sink (qword_141B27188) & function (sub_140C8C030)
        constexpr uintptr_t BULDOZER_NOTIFY_SINK_RVA           = 0x1B27188;
        constexpr uintptr_t BULDOZER_NOTIFY_FUNC_RVA           = 0xC8C030;

        // UIManager static pointer (sub_140B7A260: qword_144F1CF00)
        constexpr uintptr_t UIMANAGER_STATIC_RVA               = 0x4F1CF00;
        constexpr uintptr_t UIMANAGER_MENU_OFFSET              = 144; // 0x90 (sub_140B7A0C0: GetMenu)
        constexpr uintptr_t UIMANAGER_DIALOG_OFFSET            = 152; // 0x98 (sub_140B7A490: IsDialogVisible)

        // SurfaceY function: float __fastcall sub_14072BEB0(Landscape* world, float x, float z)
        constexpr uintptr_t SURFACEY_FUNC_RVA        = 0x72BEB0;

        // SurfaceY with Water / Roads: float __fastcall sub_140728940(...)
        constexpr uintptr_t SURFACEY_WATER_RVA       = 0x728940;

        // SurfaceY script wrapper: double sub_140C26810() -> jmp qword ptr [rax+0F0h]
        constexpr uintptr_t SURFACEY_WRAPPER_RVA     = 0xC26810;

        // Native DayZ Shape API (sub_140462D10, sub_140461C40, sub_14019C1B0)
        constexpr uintptr_t SHAPE_CREATETRIS_RVA     = 0x462D10;
        constexpr uintptr_t SHAPE_CREATELINES_RVA    = 0x461C40;
        constexpr uintptr_t SHAPE_DESTROY_RVA        = 0x19C1B0;

        // TerrainGrid struct offset within World / Landscape instance
        // sub_14072BEB0: v3 = *(_QWORD *)(world + 30192)
        constexpr uintptr_t TERRAIN_OFFSET_IN_WORLD  = 30192; // 0x75F0

        // Fields inside TerrainGrid
        constexpr uintptr_t TERRAIN_DIM_OFFSET       = 644;   // 0x284 (int32, e.g. 2048)
        constexpr uintptr_t TERRAIN_CELLSIZE_OFFSET  = 664;   // 0x298 (float, e.g. 7.8125m)
        constexpr uintptr_t TERRAIN_INVCELL_OFFSET   = 696;   // 0x2B8 (float, 1.0 / cell_size)
        constexpr uintptr_t TERRAIN_SIZEMETERS_OFFSET= 672;   // 0x2A0 (float, total map size in meters)
    }

    // DayZ Shape Flags bitmask (matching Enforce Script ShapeFlags)
    namespace ShapeFlags
    {
        constexpr int NOZBUFFER   = 1 << 0;  // 1: Do not compare z-buffer
        constexpr int NOZWRITE    = 1 << 1;  // 2: Do not write to z-buffer
        constexpr int WIREFRAME   = 1 << 2;  // 4: Wireframe lines only
        constexpr int TRANSP      = 1 << 3;  // 8: Translucent blending
        constexpr int DOUBLESIDE  = 1 << 4;  // 16: Render both sides (no backface culling)
        constexpr int ONCE        = 1 << 5;  // 32: Render once and auto-destroy
        constexpr int NOOUTLINE   = 1 << 6;  // 64: Solid faces, no wireframe outline
        constexpr int BACKFACE    = 1 << 7;  // 128: Render backfaces only
        constexpr int NOCULL      = 1 << 8;  // 256: Do not cull by view frustum
        constexpr int VISIBLE     = 1 << 9;  // 512: Visible (required for rendering)
        constexpr int ADDITIVE    = 1 << 10; // 1024: Additive blending
    }

    // Pattern Signatures (AOB) for dynamic scanning
    namespace Signatures
    {
        // sub_140C26810 (SurfaceY script wrapper):
        // 48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 48 FF A0 F0 00 00 00
        inline const char* SIG_SURFACEY_WRAPPER = "48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 FF A0 F0 00 00 00";

        // sub_140C17E70 (GetCurrentCameraPosition):
        // 48 8B 05 ?? ?? ?? ?? 48 8B 88 18 01 00 00 8B 41 2C 89 02 8B 41 30 89 42 04 8B 41 34 89 42 08
        inline const char* SIG_GET_CAM_POS      = "48 8B 05 ? ? ? ? 48 8B 88 18 01 00 00 8B 41 2C 89 02 8B 41 30 89 42 04 8B 41 34 89 42 08";

        // sub_140C17E40 (GetCurrentCameraDirection):
        // 48 8B 05 ?? ?? ?? ?? 48 8B 88 18 01 00 00 8B 41 20 89 02 8B 41 24 89 42 04 8B 41 28 89 42 08
        inline const char* SIG_GET_CAM_DIR      = "48 8B 05 ? ? ? ? 48 8B 88 18 01 00 00 8B 41 20 89 02 8B 41 24 89 42 04 8B 41 28 89 42 08";

        // sub_14072BEB0 (SurfaceY calculation implementation):
        // 4C 8B DC 48 83 EC 68 48 8B 89 F0 75 00 00
        inline const char* SIG_SURFACEY_IMPL    = "4C 8B DC 48 83 EC 68 48 8B 89 F0 75 00 00";
    }

    // 3D Vector with essential math operations
    struct Vector3
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };

        Vector3() = default;
        Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        inline Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        inline Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        inline Vector3 operator*(float s) const          { return { x * s, y * s, z * s }; }
        inline Vector3 operator/(float s) const          { float inv = 1.0f / s; return { x * inv, y * inv, z * inv }; }

        inline Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        inline Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        inline Vector3& operator*=(float s)          { x *= s; y *= s; z *= s; return *this; }

        inline float Dot(const Vector3& o) const     { return x * o.x + y * o.y + z * o.z; }
        inline float LengthSq() const                { return x * x + y * y + z * z; }
        inline float Length() const                  { return std::sqrt(LengthSq()); }

        inline Vector3 Normalized() const
        {
            float len = Length();
            if (len > 1e-6f)
                return *this * (1.0f / len);
            return { 0.0f, 0.0f, 0.0f };
        }

        inline Vector3 Cross(const Vector3& o) const
        {
            return {
                y * o.z - z * o.y,
                z * o.x - x * o.z,
                x * o.y - y * o.x
            };
        }
    };

    // Camera Transform layout in memory
    // Exact struct layout from sub_1405F9A00 / sub_1405F9A30 / sub_1400C5C40
    #pragma pack(push, 1)
    struct CameraTransform
    {
        void*   vftable;    // 0x00: vtable pointer
        Vector3 right;      // 0x08: Right vector (X=0x08, Y=0x0C, Z=0x10)
        Vector3 up;         // 0x14: Up vector    (X=0x14, Y=0x18, Z=0x1C)
        Vector3 forward;    // 0x20: Dir vector   (X=0x20, Y=0x24, Z=0x28)
        Vector3 position;   // 0x2C: Pos vector   (X=0x2C, Y=0x30, Z=0x34)
        uint8_t pad_38[0x70 - 0x38]; // 0x38 - 0x6F
        float   projX;      // 0x70: Projection scale X (focal X)
        uint8_t pad_74[0x80 - 0x74]; // 0x74 - 0x7F
        float   projY;      // 0x80: Projection scale Y (focal Y)
    };
    #pragma pack(pop)

    // Function typedef for native SurfaceY
    typedef float (__fastcall* tSurfaceY)(uintptr_t world, float x, float z, float* outNormX, float* outNormZ, void* outSurfType);

    // Function typedefs for native DayZ Shape API
    typedef void* (__fastcall* tShapeCreateTris)(int colorARGB, int flags, const Vector3* vertices, int count);
    typedef void* (__fastcall* tShapeCreateLines)(int colorARGB, int flags, const Vector3* vertices, int count);
    typedef void  (__fastcall* tShapeDestroy)(void* shape);

    // Fast Pattern Scanner
    inline uintptr_t FindPattern(HMODULE module, const char* signature)
    {
        if (!module) module = GetModuleHandleA(nullptr);
        if (!module) return 0;

        auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
        auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uint8_t*>(module) + dosHeader->e_lfanew);

        uint8_t* scanStart = reinterpret_cast<uint8_t*>(module);
        size_t scanSize = ntHeaders->OptionalHeader.SizeOfImage;

        std::vector<int> patternBytes;
        const char* current = signature;
        while (*current)
        {
            if (*current == ' ')
            {
                current++;
                continue;
            }
            if (*current == '?')
            {
                patternBytes.push_back(-1);
                current++;
                if (*current == '?') current++;
            }
            else
            {
                patternBytes.push_back(strtoul(current, const_cast<char**>(&current), 16));
            }
        }

        const size_t patternLen = patternBytes.size();
        for (size_t i = 0; i < scanSize - patternLen; ++i)
        {
            bool found = true;
            for (size_t j = 0; j < patternLen; ++j)
            {
                if (patternBytes[j] != -1 && patternBytes[j] != scanStart[i + j])
                {
                    found = false;
                    break;
                }
            }
            if (found)
                return reinterpret_cast<uintptr_t>(&scanStart[i]);
        }
        return 0;
    }

    // Resolve RIP-relative 32-bit offset
    inline uintptr_t ResolveRelative(uintptr_t instructionAddress, int offsetToDisplacement, int instructionLength)
    {
        if (!instructionAddress) return 0;
        int32_t disp = *reinterpret_cast<int32_t*>(instructionAddress + offsetToDisplacement);
        return instructionAddress + instructionLength + disp;
    }

    // Engine Interface Manager
    class EngineInterface
    {
    public:
        uintptr_t moduleBase{ 0 };
        uintptr_t pWorldStaticPtr{ 0 };
        uintptr_t pEngineStaticPtr{ 0 };
        uintptr_t pUIManagerStaticPtr{ 0 };
        tSurfaceY fnSurfaceY{ nullptr };
        tShapeCreateTris fnShapeCreateTris{ nullptr };
        tShapeCreateLines fnShapeCreateLines{ nullptr };
        tShapeDestroy fnShapeDestroy{ nullptr };

        bool Initialize()
        {
            moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            if (!moduleBase) return false;

            // 1. Direct RVA resolution
            pWorldStaticPtr = moduleBase + Offsets::WORLD_STATIC_PTR_RVA;
            pEngineStaticPtr = moduleBase + Offsets::ENGINE_STATIC_PTR_RVA;
            pUIManagerStaticPtr = moduleBase + Offsets::UIMANAGER_STATIC_RVA;
            fnSurfaceY = reinterpret_cast<tSurfaceY>(moduleBase + Offsets::SURFACEY_FUNC_RVA);
            fnShapeCreateTris = reinterpret_cast<tShapeCreateTris>(moduleBase + Offsets::SHAPE_CREATETRIS_RVA);
            fnShapeCreateLines = reinterpret_cast<tShapeCreateLines>(moduleBase + Offsets::SHAPE_CREATELINES_RVA);
            fnShapeDestroy = reinterpret_cast<tShapeDestroy>(moduleBase + Offsets::SHAPE_DESTROY_RVA);

            // 2. Fallback pattern scan verification for World Static Pointer
            uintptr_t wrapperAddr = FindPattern(reinterpret_cast<HMODULE>(moduleBase), Signatures::SIG_SURFACEY_WRAPPER);
            if (wrapperAddr)
            {
                uintptr_t scannedWorldPtr = ResolveRelative(wrapperAddr, 3, 7);
                if (scannedWorldPtr)
                    pWorldStaticPtr = scannedWorldPtr;
            }

            // Fallback scan for Engine / Camera pointer
            uintptr_t camPosAddr = FindPattern(reinterpret_cast<HMODULE>(moduleBase), Signatures::SIG_GET_CAM_POS);
            if (camPosAddr)
            {
                uintptr_t scannedEnginePtr = ResolveRelative(camPosAddr, 3, 7);
                if (scannedEnginePtr)
                    pEngineStaticPtr = scannedEnginePtr;
            }

            return true;
        }

        // Returns current World/Landscape pointer
        inline uintptr_t GetWorld() const
        {
            if (!pWorldStaticPtr) return 0;
            return *reinterpret_cast<uintptr_t*>(pWorldStaticPtr);
        }

        // Returns current Engine pointer
        inline uintptr_t GetEngine() const
        {
            if (!pEngineStaticPtr) return 0;
            return *reinterpret_cast<uintptr_t*>(pEngineStaticPtr);
        }

        // Returns CameraTransform structure pointer
        inline CameraTransform* GetCameraTransform() const
        {
            uintptr_t engine = GetEngine();
            if (!engine) return nullptr;

            uintptr_t camTransPtr = *reinterpret_cast<uintptr_t*>(engine + Offsets::CAMERA_TRANSFORM_OFFSET);
            return reinterpret_cast<CameraTransform*>(camTransPtr);
        }

        // Returns Buldozer EditCursor pointer
        inline uintptr_t GetEditCursor() const
        {
            if (!moduleBase) return 0;
            return *reinterpret_cast<uintptr_t*>(moduleBase + Offsets::BULDOZER_EDITCURSOR_RVA);
        }

        // Returns current Buldozer Tool Mode (1 = Objects, 2 = Brush tool, 3 = Mask Painter)
        inline uint8_t GetBuldozerMode() const
        {
            uintptr_t cursor = GetEditCursor();
            if (!cursor) return 1;
            return *reinterpret_cast<uint8_t*>(cursor + Offsets::EDITCURSOR_MODE_OFFSET);
        }

        // Returns current Buldozer Brush Sub-Mode (0 = Normal, 1 = Flatten, 2 = Smooth, 3 = Height set)
        inline uint8_t GetBrushSubMode() const
        {
            uintptr_t cursor = GetEditCursor();
            if (!cursor) return 0;
            return *reinterpret_cast<uint8_t*>(cursor + Offsets::EDITCURSOR_SUBMODE_OFFSET);
        }

        // Returns active DayZ UIScriptedMenu pointer (matching native sub_1406146C0 / GetMenu)
        inline uintptr_t GetActiveMenu() const
        {
            if (!pUIManagerStaticPtr) return 0;
            __try
            {
                uintptr_t pUIManager = *reinterpret_cast<uintptr_t*>(pUIManagerStaticPtr);
                if (!pUIManager || pUIManager < 0x10000) return 0;

                // Native GetMenu() logic (sub_1406146C0):
                // mov rax, [rcx + 90h] ; 144
                // test rax, rax
                // jz loc_ret
                // mov rax, [rax + 10h] ; 16
                uintptr_t pMenuWrapper = *reinterpret_cast<uintptr_t*>(pUIManager + Offsets::UIMANAGER_MENU_OFFSET);
                if (!pMenuWrapper || pMenuWrapper < 0x10000) return 0;

                return *reinterpret_cast<uintptr_t*>(pMenuWrapper + 16);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        // Returns true if DayZ pause/options/UI menu is currently open
        inline bool IsGameMenuOpen() const
        {
            return (GetActiveMenu() != 0);
        }

        // Returns TerrainGrid pointer within World
        inline uintptr_t GetTerrainGrid() const
        {
            uintptr_t world = GetWorld();
            if (!world) return 0;

            return *reinterpret_cast<uintptr_t*>(world + Offsets::TERRAIN_OFFSET_IN_WORLD);
        }

        // Native DayZ GetWorldSize implementation (matching sub_140AC4F80: dim * cellSize)
        inline float GetTerrainSizeMeters() const
        {
            uintptr_t world = GetWorld();
            if (!world && moduleBase)
            {
                uintptr_t pCGameWorld = *reinterpret_cast<uintptr_t*>(moduleBase + Offsets::CGAME_WORLD_STATIC_RVA);
                if (pCGameWorld) world = pCGameWorld;
            }

            if (world)
            {
                uintptr_t grid = *reinterpret_cast<uintptr_t*>(world + Offsets::TERRAIN_OFFSET_IN_WORLD);
                if (grid)
                {
                    int32_t dim = *reinterpret_cast<int32_t*>(grid + Offsets::TERRAIN_DIM_OFFSET);
                    float cellSize = *reinterpret_cast<float*>(grid + Offsets::TERRAIN_CELLSIZE_OFFSET);
                    if (dim > 0 && cellSize > 0.0f)
                    {
                        float calculatedSize = static_cast<float>(dim) * cellSize;
                        if (calculatedSize >= 256.0f && calculatedSize <= 131072.0f)
                            return calculatedSize;
                    }
                }
            }

            return 20480.0f; // Default fallback to user's 20480x20480 map
        }

        // Sample exact surface height Y at world coordinates (X, Z)
        inline float GetSurfaceHeight(float x, float z) const
        {
            uintptr_t world = GetWorld();
            if (world && fnSurfaceY)
            {
                return fnSurfaceY(world, x, z, nullptr, nullptr, nullptr);
            }

            // Fallback: world vtable call at [rax + 0xF0]
            if (world)
            {
                uintptr_t vtable = *reinterpret_cast<uintptr_t*>(world);
                if (vtable)
                {
                    typedef double (__fastcall* tVirtSurfaceY)(uintptr_t w, float x_coord, float z_coord);
                    auto virtFn = *reinterpret_cast<tVirtSurfaceY*>(vtable + 0xF0);
                    if (virtFn)
                        return static_cast<float>(virtFn(world, x, z));
                }
            }

            return 0.0f;
        }

        // Native DayZ Shape API wrappers
        inline void* CreateTris(int colorARGB, int flags, const Vector3* vertices, int count) const
        {
            if (fnShapeCreateTris && vertices && count > 0)
                return fnShapeCreateTris(colorARGB, flags, vertices, count);
            return nullptr;
        }

        inline void* CreateLines(int colorARGB, int flags, const Vector3* vertices, int count) const
        {
            if (fnShapeCreateLines && vertices && count > 0)
                return fnShapeCreateLines(colorARGB, flags, vertices, count);
            return nullptr;
        }

        inline void DestroyShape(void* shape) const
        {
            if (fnShapeDestroy && shape)
                fnShapeDestroy(shape);
        }

        // Trace ray from camera into terrain
        // Returns true if hit, outHitPos receives 3D collision coordinates
        bool RaycastTerrain(const Vector3& rayOrigin, const Vector3& rayDir, float maxDistance, Vector3& outHitPos) const
        {
            uintptr_t world = GetWorld();
            if (!world) return false;

            // If ray points straight up or horizontal above terrain with positive slope, check bound
            if (rayDir.LengthSq() < 1e-4f) return false;

            const float stepSize = 2.0f; // 2 meter coarse step
            float prevT = 0.0f;
            Vector3 prevP = rayOrigin;
            float prevDiff = prevP.y - GetSurfaceHeight(prevP.x, prevP.z);

            for (float t = stepSize; t <= maxDistance; t += stepSize)
            {
                Vector3 currentP = rayOrigin + rayDir * t;
                float terrainY = GetSurfaceHeight(currentP.x, currentP.z);
                float currentDiff = currentP.y - terrainY;

                // Crossed the terrain surface
                if (currentDiff <= 0.0f)
                {
                    // Binary search refinement for sub-millimeter precision
                    float lowT = prevT;
                    float highT = t;
                    for (int iter = 0; iter < 16; ++iter)
                    {
                        float midT = (lowT + highT) * 0.5f;
                        Vector3 midP = rayOrigin + rayDir * midT;
                        float midH = GetSurfaceHeight(midP.x, midP.z);
                        if (midP.y <= midH)
                            highT = midT;
                        else
                            lowT = midT;
                    }
                    float finalT = (lowT + highT) * 0.5f;
                    outHitPos = rayOrigin + rayDir * finalT;
                    outHitPos.y = GetSurfaceHeight(outHitPos.x, outHitPos.z);
                    return true;
                }

                prevT = t;
                prevP = currentP;
                prevDiff = currentDiff;
            }

            return false;
        }

        // Native-matched World to Screen projection (matching sub_140C30C10 & sub_1405FC000)
        inline bool WorldToScreen(const Vector3& worldPos, float screenW, float screenH, float& outScreenX, float& outScreenY) const
        {
            CameraTransform* cam = GetCameraTransform();
            if (!cam || screenW <= 0.0f || screenH <= 0.0f) return false;

            Vector3 diff = worldPos - cam->position;

            // Transform to camera local space
            float xCam = diff.Dot(cam->right);
            float yCam = diff.Dot(cam->up);
            float zCam = diff.Dot(cam->forward);

            if (zCam < 0.1f) return false; // Behind camera near plane

            float px = cam->projX;
            float py = cam->projY;
            if (px <= 0.001f || py <= 0.001f || std::isnan(px) || std::isnan(py))
            {
                float aspect = screenW / screenH;
                py = 1.0f / std::tan(0.741765f * 0.5f);
                px = py / aspect;
            }

            // sub_1405FC000 exact formula:
            float ndcX = (xCam * px) / zCam;
            float ndcY = (yCam * py) / zCam;

            outScreenX = (ndcX + 1.0f) * 0.5f * screenW;
            outScreenY = (1.0f - ndcY) * 0.5f * screenH;

            return (outScreenX >= -1000.0f && outScreenX <= screenW + 1000.0f &&
                    outScreenY >= -1000.0f && outScreenY <= screenH + 1000.0f);
        }

        // Native-matched Screen to World Ray (matching sub_1405FC640)
        inline Vector3 ScreenToWorldRay(float mouseX, float mouseY, float screenW, float screenH) const
        {
            CameraTransform* cam = GetCameraTransform();
            if (!cam || screenW <= 0.0f || screenH <= 0.0f) return { 0.0f, -1.0f, 0.0f };

            float px = cam->projX;
            float py = cam->projY;
            if (px <= 0.001f || py <= 0.001f || std::isnan(px) || std::isnan(py))
            {
                float aspect = screenW / screenH;
                py = 1.0f / std::tan(0.741765f * 0.5f);
                px = py / aspect;
            }

            // sub_1405FC640 formula:
            float ndcX = (mouseX / screenW - 0.5f) * 2.0f;
            float ndcY = (0.5f - mouseY / screenH) * 2.0f;

            Vector3 ray = cam->forward + (cam->right * (ndcX / px)) + (cam->up * (ndcY / py));
            return ray.Normalized();
        }
    };

    inline EngineInterface g_Engine;
}
