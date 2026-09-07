#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <xinput.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#define OPENVR_BUILD_STATIC
#include "openvr.h"
#include "MinHook.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(REFIID riid, void **ppFactory);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID riid, void **ppFactory);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void **ppFactory);
typedef HRESULT (WINAPI *PFN_DXGIDeclareAdapterRemovalSupport)();
typedef HRESULT (WINAPI *PFN_DXGIGetDebugInterface1)(UINT Flags, REFIID riid, void **pDebug);

// NGX Prototypes
typedef int (WINAPI *PFN_NVSDK_NGX_D3D12_CreateFeature)(void* pCmdList, int FeatureId, void* pParameters, void** ppHandle);
typedef int (WINAPI *PFN_NVSDK_NGX_D3D12_EvaluateFeature)(void* pCmdList, void* pHandle, void* pParameters, void* pCallback);
typedef int (WINAPI *PFN_NVSDK_NGX_D3D12_ReleaseFeature)(void* pHandle);

static PFN_NVSDK_NGX_D3D12_CreateFeature g_pfnNGXCreateFeature = NULL;
static PFN_NVSDK_NGX_D3D12_EvaluateFeature g_pfnNGXEvaluateFeature = NULL;
static PFN_NVSDK_NGX_D3D12_ReleaseFeature g_pfnNGXReleaseFeature = NULL;
static unsigned long long g_evalFrameCounter = 0;
extern "C" int WINAPI Proxy_NVSDK_NGX_D3D12_EvaluateFeature(void* pCmdList, void* pHandle, void* pParameters, void* pCallback);

static HMODULE g_hRealVR = NULL;
static HMODULE g_hReShade = NULL;
static HMODULE g_hSysDxgi = NULL;

static PFN_CreateDXGIFactory g_pfnCreateDXGIFactory = NULL;
static PFN_CreateDXGIFactory1 g_pfnCreateDXGIFactory1 = NULL;
static PFN_CreateDXGIFactory2 g_pfnCreateDXGIFactory2 = NULL;
static PFN_DXGIDeclareAdapterRemovalSupport g_pfnDXGIDeclareAdapterRemovalSupport = NULL;
static PFN_DXGIGetDebugInterface1 g_pfnDXGIGetDebugInterface1 = NULL;

static void LogMsg(const char *msg)
{
    static char logPath[MAX_PATH] = "";
    if (logPath[0] == '\0')
    {
        if (GetModuleFileNameA(NULL, logPath, MAX_PATH))
        {
            char* lastSlash = strrchr(logPath, '\\');
            if (lastSlash)
            {
                *(lastSlash + 1) = '\0';
                strcat_s(logPath, MAX_PATH, "vr_dlss5_proxy.log");
            }
            else
            {
                strcpy_s(logPath, MAX_PATH, "vr_dlss5_proxy.log");
            }
        }
    }
    FILE *f = NULL;
    fopen_s(&f, logPath, "a");
    if (f)
    {
        fprintf(f, "%s\n", msg);
        fflush(f);
        fclose(f);
    }
}

static bool g_hudVisible = false;
static DWORD g_inputWatcherThreadId = 0;
static DWORD WINAPI InputWatcherThread(LPVOID lpParam);

// ----------------------------------------------------------------------------
// XInput & Gamepad Interception: D-Pad Masking during HUD Navigation
// Blocks D-pad forwarding to game engine while preserving movement & camera sticks
// ----------------------------------------------------------------------------
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef MMRESULT (WINAPI *PFN_joyGetPosEx)(UINT uJoyID, LPJOYINFOEX pji);

static PFN_XInputGetState g_origXInput1_4_GetState = NULL;
static PFN_XInputGetState g_origXInput1_4_Ex = NULL;
static PFN_XInputGetState g_origXInput1_3_GetState = NULL;
static PFN_XInputGetState g_origXInput9_1_0_GetState = NULL;
static PFN_XInputGetState g_origRealVR_GetState = NULL;
static PFN_joyGetPosEx g_origJoyGetPosEx = NULL;

static inline DWORD FilterXInputState(DWORD dwUserIndex, XINPUT_STATE* pState, DWORD result)
{
    // If called from our own InputWatcherThread, NEVER mask so that HUD can be navigated freely
    if (GetCurrentThreadId() == g_inputWatcherThreadId) {
        return result;
    }

    // When the VR HUD is visible, mask out ONLY the D-pad bits (0x000F)
    // Up (0x0001), Down (0x0002), Left (0x0004), Right (0x0008)
    // Movement sticks, camera stick, face buttons, bumpers, triggers remain 100% active in-game
    if (result == ERROR_SUCCESS && pState && g_hudVisible) {
        pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_DPAD_UP | 
                                      XINPUT_GAMEPAD_DPAD_DOWN | 
                                      XINPUT_GAMEPAD_DPAD_LEFT | 
                                      XINPUT_GAMEPAD_DPAD_RIGHT);
    }
    return result;
}

static DWORD WINAPI Hooked_XInput1_4_GetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD res = g_origXInput1_4_GetState ? g_origXInput1_4_GetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    return FilterXInputState(dwUserIndex, pState, res);
}

static DWORD WINAPI Hooked_XInput1_4_Ex(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD res = g_origXInput1_4_Ex ? g_origXInput1_4_Ex(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    return FilterXInputState(dwUserIndex, pState, res);
}

static DWORD WINAPI Hooked_XInput1_3_GetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD res = g_origXInput1_3_GetState ? g_origXInput1_3_GetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    return FilterXInputState(dwUserIndex, pState, res);
}

static DWORD WINAPI Hooked_XInput9_1_0_GetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD res = g_origXInput9_1_0_GetState ? g_origXInput9_1_0_GetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    return FilterXInputState(dwUserIndex, pState, res);
}

static DWORD WINAPI Hooked_RealVR_GetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD res = g_origRealVR_GetState ? g_origRealVR_GetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
    return FilterXInputState(dwUserIndex, pState, res);
}

static MMRESULT WINAPI Hooked_joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji)
{
    MMRESULT res = g_origJoyGetPosEx ? g_origJoyGetPosEx(uJoyID, pji) : JOYERR_PARMS;
    if (GetCurrentThreadId() == g_inputWatcherThreadId) return res;
    if (res == JOYERR_NOERROR && pji && g_hudVisible) {
        pji->dwPOV = JOY_POVCENTERED; // 0xFFFF = neutral POV hat (D-pad centered)
    }
    return res;
}

static void InstallXInputHooks()
{
    static bool s_minHookInited = false;
    if (!s_minHookInited) {
        MH_STATUS st = MH_Initialize();
        if (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED) {
            s_minHookInited = true;
            LogMsg("[Proxy-Input] MinHook engine initialized successfully");
        } else {
            char buf[128];
            sprintf_s(buf, sizeof(buf), "[Proxy-Input] MinHook init error: %d", st);
            LogMsg(buf);
            return;
        }
    }

    // 1. Hook RealVR64.dll's XInputGetState if present
    if (g_hRealVR && !g_origRealVR_GetState) {
        void* pTarget = (void*)GetProcAddress(g_hRealVR, "XInputGetState");
        if (pTarget) {
            if (MH_CreateHook(pTarget, (LPVOID)&Hooked_RealVR_GetState, (LPVOID*)&g_origRealVR_GetState) == MH_OK) {
                MH_EnableHook(pTarget);
                LogMsg("[Proxy-Input] Hooked RealVR64.dll!XInputGetState (D-Pad filter armed)");
            }
        }
    }

    // 2. Hook XINPUT1_4.dll (local game directory or system)
    HMODULE hX14 = GetModuleHandleA("XINPUT1_4.dll");
    if (!hX14) hX14 = LoadLibraryA("XINPUT1_4.dll");
    if (hX14) {
        if (!g_origXInput1_4_GetState) {
            void* pTarget = (void*)GetProcAddress(hX14, "XInputGetState");
            if (pTarget) {
                if (MH_CreateHook(pTarget, (LPVOID)&Hooked_XInput1_4_GetState, (LPVOID*)&g_origXInput1_4_GetState) == MH_OK) {
                    MH_EnableHook(pTarget);
                    LogMsg("[Proxy-Input] Hooked XINPUT1_4.dll!XInputGetState (D-Pad filter armed)");
                }
            }
        }
        if (!g_origXInput1_4_Ex) {
            void* pEx = (void*)GetProcAddress(hX14, (LPCSTR)100);
            if (pEx && pEx != (void*)g_origXInput1_4_GetState) {
                if (MH_CreateHook(pEx, (LPVOID)&Hooked_XInput1_4_Ex, (LPVOID*)&g_origXInput1_4_Ex) == MH_OK) {
                    MH_EnableHook(pEx);
                    LogMsg("[Proxy-Input] Hooked XINPUT1_4.dll!XInputGetStateEx (ordinal 100 armed)");
                }
            }
        }
    }

    // 3. Hook XINPUT1_3.dll if loaded
    HMODULE hX13 = GetModuleHandleA("XINPUT1_3.dll");
    if (hX13 && !g_origXInput1_3_GetState) {
        void* pTarget = (void*)GetProcAddress(hX13, "XInputGetState");
        if (pTarget) {
            if (MH_CreateHook(pTarget, (LPVOID)&Hooked_XInput1_3_GetState, (LPVOID*)&g_origXInput1_3_GetState) == MH_OK) {
                MH_EnableHook(pTarget);
                LogMsg("[Proxy-Input] Hooked XINPUT1_3.dll!XInputGetState (D-Pad filter armed)");
            }
        }
    }

    // 4. Hook XINPUT9_1_0.dll if loaded
    HMODULE hX9 = GetModuleHandleA("XINPUT9_1_0.dll");
    if (hX9 && !g_origXInput9_1_0_GetState) {
        void* pTarget = (void*)GetProcAddress(hX9, "XInputGetState");
        if (pTarget) {
            if (MH_CreateHook(pTarget, (LPVOID)&Hooked_XInput9_1_0_GetState, (LPVOID*)&g_origXInput9_1_0_GetState) == MH_OK) {
                MH_EnableHook(pTarget);
                LogMsg("[Proxy-Input] Hooked XINPUT9_1_0.dll!XInputGetState (D-Pad filter armed)");
            }
        }
    }

    // 5. Hook joyGetPosEx in winmm.dll
    HMODULE hWinmm = GetModuleHandleA("winmm.dll");
    if (hWinmm && !g_origJoyGetPosEx) {
        void* pTarget = (void*)GetProcAddress(hWinmm, "joyGetPosEx");
        if (pTarget) {
            if (MH_CreateHook(pTarget, (LPVOID)&Hooked_joyGetPosEx, (LPVOID*)&g_origJoyGetPosEx) == MH_OK) {
                MH_EnableHook(pTarget);
                LogMsg("[Proxy-Input] Hooked winmm.dll!joyGetPosEx (POV hat neutralizer armed)");
            }
        }
    }
}

static void InitProxy()
{
    static BOOL initialized = FALSE;
    if (initialized) return;
    initialized = TRUE;

    LogMsg("[Proxy] Initializing VR-DLSS5 Dual Proxy...");

    // 1. Charger ReShade 6.8 (DLSS 5 host) d'abord pour installer les hooks NGX proprement
    g_hReShade = LoadLibraryA("ReShade64_dlss5.dll");
    if (g_hReShade)
    {
        LogMsg("[Proxy] Successfully loaded ReShade64_dlss5.dll (NGX hooks armed)");
    }
    else
    {
        LogMsg("[Proxy] WARNING: Could not load ReShade64_dlss5.dll");
    }

    // 2. Charger RealVR64.dll (LukeRoss VR mod) ensuite
    g_hRealVR = LoadLibraryA("RealVR64.dll");
    if (g_hRealVR)
    {
        LogMsg("[Proxy] Successfully loaded RealVR64.dll");
        g_pfnCreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g_hRealVR, "CreateDXGIFactory");
        g_pfnCreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g_hRealVR, "CreateDXGIFactory1");
        g_pfnCreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g_hRealVR, "CreateDXGIFactory2");
        g_pfnDXGIDeclareAdapterRemovalSupport = (PFN_DXGIDeclareAdapterRemovalSupport)GetProcAddress(g_hRealVR, "DXGIDeclareAdapterRemovalSupport");
        g_pfnDXGIGetDebugInterface1 = (PFN_DXGIGetDebugInterface1)GetProcAddress(g_hRealVR, "DXGIGetDebugInterface1");

        // Résolution directe sans altération de code ni trampoline instable
        g_pfnNGXCreateFeature = (PFN_NVSDK_NGX_D3D12_CreateFeature)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_CreateFeature");
        g_pfnNGXEvaluateFeature = (PFN_NVSDK_NGX_D3D12_EvaluateFeature)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_EvaluateFeature");
        g_pfnNGXReleaseFeature = (PFN_NVSDK_NGX_D3D12_ReleaseFeature)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_ReleaseFeature");
        LogMsg("[Proxy] RealVR64 exports resolved natively (zero memory corruption)");
    }
    else
    {
        LogMsg("[Proxy] WARNING: RealVR64.dll not found, falling back to system dxgi.dll");
    }

    // 3. Repli de secours vers system32 dxgi si une fonction n'est pas dans RealVR
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat_s(sysPath, MAX_PATH, "\\dxgi.dll");
    g_hSysDxgi = LoadLibraryA(sysPath);

    if (g_hSysDxgi)
    {
        if (!g_pfnCreateDXGIFactory) g_pfnCreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g_hSysDxgi, "CreateDXGIFactory");
        if (!g_pfnCreateDXGIFactory1) g_pfnCreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g_hSysDxgi, "CreateDXGIFactory1");
        if (!g_pfnCreateDXGIFactory2) g_pfnCreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g_hSysDxgi, "CreateDXGIFactory2");
        if (!g_pfnDXGIDeclareAdapterRemovalSupport) g_pfnDXGIDeclareAdapterRemovalSupport = (PFN_DXGIDeclareAdapterRemovalSupport)GetProcAddress(g_hSysDxgi, "DXGIDeclareAdapterRemovalSupport");
        if (!g_pfnDXGIGetDebugInterface1) g_pfnDXGIGetDebugInterface1 = (PFN_DXGIGetDebugInterface1)GetProcAddress(g_hSysDxgi, "DXGIGetDebugInterface1");
    }

    // 4. Installer les hooks d'interception D-Pad (MinHook)
    InstallXInputHooks();

    // 5. Lancer le thread d'écoute autonome pour F6 et Select+L3
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InputWatcherThread, NULL, 0, &g_inputWatcherThreadId);

    // 6. Fixer la priorité haute pour garantir la stabilité de l'ordonnancement en VR
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    LogMsg("[Proxy] Proxy ready (Autonomous Input Thread armed, High Priority set).");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        InitProxy();
    }
    return TRUE;
}

extern "C" {

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void **ppFactory)
{
    InitProxy();
    LogMsg("[Proxy] Proxy_CreateDXGIFactory called");
    if (g_pfnCreateDXGIFactory) return g_pfnCreateDXGIFactory(riid, ppFactory);
    return E_FAIL;
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void **ppFactory)
{
    InitProxy();
    LogMsg("[Proxy] Proxy_CreateDXGIFactory1 called");
    if (g_pfnCreateDXGIFactory1) return g_pfnCreateDXGIFactory1(riid, ppFactory);
    return E_FAIL;
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void **ppFactory)
{
    InitProxy();
    LogMsg("[Proxy] Proxy_CreateDXGIFactory2 called");
    if (g_pfnCreateDXGIFactory2) return g_pfnCreateDXGIFactory2(Flags, riid, ppFactory);
    return E_FAIL;
}

HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport()
{
    InitProxy();
    if (g_pfnDXGIDeclareAdapterRemovalSupport) return g_pfnDXGIDeclareAdapterRemovalSupport();
    return S_OK;
}

HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT Flags, REFIID riid, void **pDebug)
{
    InitProxy();
    if (g_pfnDXGIGetDebugInterface1) return g_pfnDXGIGetDebugInterface1(Flags, riid, pDebug);
    return E_FAIL;
}

typedef HRESULT (WINAPI *PFN_ApplyCompatResolutionQuirking)();
typedef HRESULT (WINAPI *PFN_CompatString)();
typedef HRESULT (WINAPI *PFN_CompatValue)();
typedef HRESULT (WINAPI *PFN_DXGID3D10CreateDevice)();
typedef HRESULT (WINAPI *PFN_DXGID3D10CreateLayeredDevice)();
typedef HRESULT (WINAPI *PFN_DXGID3D10GetLayeredDeviceSize)();
typedef HRESULT (WINAPI *PFN_DXGID3D10RegisterLayers)();
typedef HRESULT (WINAPI *PFN_DXGIDisableVBlankVirtualization)();
typedef HRESULT (WINAPI *PFN_DXGIDumpJournal)();
typedef HRESULT (WINAPI *PFN_DXGIReportAdapterConfiguration)();
typedef HRESULT (WINAPI *PFN_PIXBeginCapture)();
typedef HRESULT (WINAPI *PFN_PIXEndCapture)();
typedef HRESULT (WINAPI *PFN_PIXGetCaptureState)();
typedef HRESULT (WINAPI *PFN_SetAppCompatStringPointer)();
typedef HRESULT (WINAPI *PFN_UpdateHMDEmulationStatus)();

static void* ResolveProc(const char *name)
{
    void *p = NULL;
    if (g_hRealVR) p = (void*)GetProcAddress(g_hRealVR, name);
    if (!p && g_hSysDxgi) p = (void*)GetProcAddress(g_hSysDxgi, name);
    return p;
}

#define FORWARD_HR(name) \
    static PFN_##name s_pfn_##name = NULL; \
    if (!s_pfn_##name) s_pfn_##name = (PFN_##name)ResolveProc(#name); \
    if (s_pfn_##name) return s_pfn_##name(); \
    return S_OK;

HRESULT WINAPI Proxy_ApplyCompatResolutionQuirking() { InitProxy(); FORWARD_HR(ApplyCompatResolutionQuirking); }
HRESULT WINAPI Proxy_CompatString() { InitProxy(); FORWARD_HR(CompatString); }
HRESULT WINAPI Proxy_CompatValue() { InitProxy(); FORWARD_HR(CompatValue); }
HRESULT WINAPI Proxy_DXGID3D10CreateDevice() { InitProxy(); FORWARD_HR(DXGID3D10CreateDevice); }
HRESULT WINAPI Proxy_DXGID3D10CreateLayeredDevice() { InitProxy(); FORWARD_HR(DXGID3D10CreateLayeredDevice); }
HRESULT WINAPI Proxy_DXGID3D10GetLayeredDeviceSize() { InitProxy(); FORWARD_HR(DXGID3D10GetLayeredDeviceSize); }
HRESULT WINAPI Proxy_DXGID3D10RegisterLayers() { InitProxy(); FORWARD_HR(DXGID3D10RegisterLayers); }
HRESULT WINAPI Proxy_DXGIDisableVBlankVirtualization() { InitProxy(); FORWARD_HR(DXGIDisableVBlankVirtualization); }
HRESULT WINAPI Proxy_DXGIDumpJournal() { InitProxy(); FORWARD_HR(DXGIDumpJournal); }
HRESULT WINAPI Proxy_DXGIReportAdapterConfiguration() { InitProxy(); FORWARD_HR(DXGIReportAdapterConfiguration); }
HRESULT WINAPI Proxy_PIXBeginCapture() { InitProxy(); FORWARD_HR(PIXBeginCapture); }
HRESULT WINAPI Proxy_PIXEndCapture() { InitProxy(); FORWARD_HR(PIXEndCapture); }
HRESULT WINAPI Proxy_PIXGetCaptureState() { InitProxy(); FORWARD_HR(PIXGetCaptureState); }
HRESULT WINAPI Proxy_SetAppCompatStringPointer() { InitProxy(); FORWARD_HR(SetAppCompatStringPointer); }
HRESULT WINAPI Proxy_UpdateHMDEmulationStatus() { InitProxy(); FORWARD_HR(UpdateHMDEmulationStatus); }

int WINAPI Proxy_NVSDK_NGX_D3D12_CreateFeature(void* pCmdList, int FeatureId, void* pParameters, void** ppHandle)
{
    InitProxy();
    if (!g_pfnNGXCreateFeature && g_hRealVR)
        g_pfnNGXCreateFeature = (PFN_NVSDK_NGX_D3D12_CreateFeature)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_CreateFeature");
    
    char buf[128];
    sprintf_s(buf, sizeof(buf), "[VR-DLSS5] CreateFeature called: FeatureId=%d", FeatureId);
    LogMsg(buf);

    if (g_pfnNGXCreateFeature)
        return g_pfnNGXCreateFeature(pCmdList, FeatureId, pParameters, ppHandle);
    return 1;
}

} // extern "C"

// ============================================================================
// VR-DLSS 5 HUD & REAL-TIME CONTROLLER (Quest 3 & Pimax Multi-Res)
// ============================================================================

#define HUD_WIDTH  480
#define HUD_HEIGHT 220

struct HUDColor {
    uint8_t r, g, b, a;
};

static inline HUDColor MakeHUDColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    HUDColor c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}

// Runtime memory offsets inside renodx-dlss5.addon64
static uintptr_t g_renodxBase = 0;
static bool g_varsInitialized = false;

// HUD State
static bool g_hudDirty = true;
static bool g_masterEnable = true;
static float g_nrIntensity = 2.50f;
static float g_nrGlobalTone = 1.00f;
static int g_nrPreset = 2; // Default: Preset 2 [Performance] for high-FPS VR
static int g_hudScale = 0; // 0: 1.0x (Compact / Pimax Fin), 1: 1.5x (Equilibre), 2: 2.0x (Confort)
static int g_activeRow = 0; // 0 to 5
static int g_hudPosIndex = 0; // 0: Bas-Centre, 1: Haut-Centre, 2: Haut-Droite, 3: Haut-Gauche
static int g_liveHz = 72;

// 500 ms Debounce State
static bool g_hasPendingSave = false;
static uint64_t g_lastChangeTick = 0;
static char g_iniPath[MAX_PATH] = ".\\ReShade.ini";

// Key & Gamepad repetition
struct KeyTracker {
    bool isDown;
    bool justPressed;
    uint64_t downSince;
    uint64_t lastRepeat;
};
static KeyTracker g_keys[256] = {};

static void InitPaths()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
            strcat_s(exePath, MAX_PATH, "ReShade.ini");
            strcpy_s(g_iniPath, MAX_PATH, exePath);
        }
    }
}

static void InitVariablesFromAddonOrIni()
{
    if (g_varsInitialized) return;

    if (!g_renodxBase)
    {
        g_renodxBase = (uintptr_t)GetModuleHandleA("renodx-dlss5.addon64");
    }

    InitPaths();

    if (g_renodxBase)
    {
        g_nrIntensity = *(float*)(g_renodxBase + 0x19364C);
        g_nrGlobalTone = *(float*)(g_renodxBase + 0x193650);
        g_nrPreset = *(int32_t*)(g_renodxBase + 0x196B98);

        if (g_nrIntensity <= 0.01f) {
            g_masterEnable = false;
            g_nrIntensity = 2.00f;
        } else {
            g_masterEnable = true;
            if (g_nrIntensity > 10.0f) g_nrIntensity = 2.50f;
        }
        if (g_nrGlobalTone <= 0.0f || g_nrGlobalTone > 5.0f) g_nrGlobalTone = 1.00f;

        // Load Preset from INI (default: 2 - Performance preset)
        g_nrPreset = GetPrivateProfileIntA("RenoDX.DLSS5", "NRPreset", 2, g_iniPath);
        if (g_nrPreset < 0 || g_nrPreset > 2) g_nrPreset = 2;

        // Apply Performance preset to addon RAM immediately
        *(int32_t*)(g_renodxBase + 0x196B98) = g_nrPreset;
        // Keep NRStyle neutral (0) to prevent mixing filmic tone mapping with AI preset
        *(int32_t*)(g_renodxBase + 0x196C2C) = 0;

        // Load scale & position from INI (default: 1.5x for Quest 3 comfort)
        g_hudScale = GetPrivateProfileIntA("RenoDX.DLSS5", "HUDScale", 1, g_iniPath);
        if (g_hudScale < 0 || g_hudScale > 2) g_hudScale = 1;

        g_hudPosIndex = GetPrivateProfileIntA("RenoDX.DLSS5", "HUDPosition", 0, g_iniPath);
        if (g_hudPosIndex < 0 || g_hudPosIndex > 3) g_hudPosIndex = 0;

        g_varsInitialized = true;
        char buf[256];
        sprintf_s(buf, sizeof(buf), 
            "[VR-DLSS5-HUD] Initialized: Enable=%d, Intensity=%.2f, Tone=%.2f, Preset=%d (Performance), Scale=%d, Pos=%d",
            g_masterEnable ? 1 : 0, g_nrIntensity, g_nrGlobalTone, g_nrPreset, g_hudScale, g_hudPosIndex);
        LogMsg(buf);
    }
}

static void CommitSettingsToDisk()
{
    // High-performance single-pass section serialization (replaces 6 separate file opens/parses)
    char secBuf[512];
    int offset = 0;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "EnableHooks=2") + 1; // NGX direct hooks only (skip Streamline interposer)
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "NRUICorrection=0") + 1; // Skip redundant UI mask pass in VR
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "NRIntensity=%.2f", g_masterEnable ? g_nrIntensity : 0.0f) + 1;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "NRGlobalTone=%.2f", g_nrGlobalTone) + 1;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "NRPreset=%d", g_nrPreset) + 1;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "NRStyle=0") + 1;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "HUDScale=%d", g_hudScale) + 1;
    offset += sprintf_s(secBuf + offset, sizeof(secBuf) - offset, "HUDPosition=%d", g_hudPosIndex) + 1;
    secBuf[offset] = '\0'; // Double null terminator for WritePrivateProfileSectionA

    WritePrivateProfileSectionA("RenoDX.DLSS5", secBuf, g_iniPath);

    char logBuf[256];
    sprintf_s(logBuf, sizeof(logBuf), 
        "[VR-DLSS5-HUD] 500ms Debounce Save committed (single-pass): Enable=%d, Int=%.2f, Tone=%.2f, Preset=%d, Scale=%d -> %s",
        g_masterEnable ? 1 : 0, g_masterEnable ? g_nrIntensity : 0.0f, g_nrGlobalTone, g_nrPreset, g_hudScale, g_iniPath);
    LogMsg(logBuf);
}

// ----------------------------------------------------------------------------
// Modern High-DPI GDI Vector Rasterizer (Anti-Aliased Segoe UI, Zero-Crash)
// ----------------------------------------------------------------------------
static uint32_t s_hudPixelsOSD[HUD_HEIGHT * HUD_WIDTH];   // Premultiplied BGRA for Desktop Layered Window
static uint8_t  s_hudPixelsVR[HUD_HEIGHT * HUD_WIDTH * 4]; // RGBA for OpenVR SetOverlayRaw
static HDC      g_hGdiMemDC = NULL;
static HBITMAP  g_hGdiBmp = NULL;
static uint32_t* g_pGdiBits = NULL;

static void InitGDIRasterizer()
{
    if (g_hGdiMemDC) return;
    HDC hdcScreen = GetDC(NULL);
    g_hGdiMemDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = HUD_WIDTH;
    bmi.bmiHeader.biHeight = -HUD_HEIGHT; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_hGdiBmp = CreateDIBSection(g_hGdiMemDC, &bmi, DIB_RGB_COLORS, (void**)&g_pGdiBits, NULL, 0);
    SelectObject(g_hGdiMemDC, g_hGdiBmp);
    ReleaseDC(NULL, hdcScreen);
}

static void RenderModernHUD(HDC hdc, uint32_t* pGdiBits, bool masterEnable, float intensity, float tone, 
                            int preset, int posIdx, int scaleMode, int activeRow, int liveHz)
{
    if (!hdc || !pGdiBits) return;

    // 1. Clear GDI buffer to 0
    memset(pGdiBits, 0, HUD_WIDTH * HUD_HEIGHT * 4);

    // 2. Setup GDI state
    SetBkMode(hdc, TRANSPARENT);

    HFONT hFontTitle = CreateFontA(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontMain  = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontValue = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBadge = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontHelp  = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    // 3. Draw Background Card (Translucent slate with rounded corners)
    HBRUSH hBrushCard = CreateSolidBrush(RGB(14, 18, 26));
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(0, 180, 235)); // Cyan neon outline
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrushCard);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenBorder);
    RoundRect(hdc, 1, 1, HUD_WIDTH - 1, HUD_HEIGHT - 1, 14, 14);

    // 4. Header Bar: Title
    SelectObject(hdc, hFontTitle);
    SetTextColor(hdc, RGB(0, 220, 255));
    TextOutA(hdc, 16, 8, "DLSS 5 <> VR", 12);

    // Solid Glowing Neon Green Dot (Rock-solid, no periodic blinking to prevent VR flicker)
    HBRUSH hBrushDot = CreateSolidBrush(RGB(0, 255, 140));
    HPEN hPenDot = CreatePen(PS_SOLID, 1, RGB(0, 255, 140));
    SelectObject(hdc, hBrushDot);
    SelectObject(hdc, hPenDot);
    Ellipse(hdc, HUD_WIDTH - 148, 12, HUD_WIDTH - 138, 22);
    DeleteObject(hBrushDot);
    DeleteObject(hPenDot);

    // Hz & Status Pill Badge (Wider box: 122px wide to comfortably fit 144 Hz | ACTIVE)
    SelectObject(hdc, hFontBadge);
    char badgeBuf[32];
    sprintf_s(badgeBuf, sizeof(badgeBuf), "%d Hz  |  %s", liveHz, masterEnable ? "ACTIVE" : "BYPASS");
    COLORREF badgeBg = masterEnable ? RGB(16, 75, 42) : RGB(100, 24, 24);
    COLORREF badgeBorder = masterEnable ? RGB(45, 200, 100) : RGB(220, 60, 60);
    COLORREF badgeText = masterEnable ? RGB(220, 255, 230) : RGB(255, 220, 220);

    HBRUSH hBrushBadge = CreateSolidBrush(badgeBg);
    HPEN hPenBadge = CreatePen(PS_SOLID, 1, badgeBorder);
    SelectObject(hdc, hBrushBadge);
    SelectObject(hdc, hPenBadge);
    RoundRect(hdc, HUD_WIDTH - 134, 6, HUD_WIDTH - 12, 28, 8, 8);
    DeleteObject(hBrushBadge);
    DeleteObject(hPenBadge);

    SetTextColor(hdc, badgeText);
    RECT rcBadge = { HUD_WIDTH - 134, 6, HUD_WIDTH - 12, 28 };
    DrawTextA(hdc, badgeBuf, -1, &rcBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Header Separator Line
    HPEN hPenSep = CreatePen(PS_SOLID, 1, RGB(32, 54, 82));
    SelectObject(hdc, hPenSep);
    MoveToEx(hdc, 14, 34, NULL);
    LineTo(hdc, HUD_WIDTH - 14, 34);
    DeleteObject(hPenSep);

    // 5. Six Menu Rows: Y positions
    int rowY[6] = { 38, 64, 90, 116, 142, 168 };

    for (int i = 0; i < 6; i++) {
        int y = rowY[i];
        bool isActive = (i == activeRow);

        if (isActive) {
            HBRUSH hBrushRow = CreateSolidBrush(RGB(24, 46, 76));
            HPEN hPenRow = CreatePen(PS_SOLID, 1, RGB(0, 170, 255));
            SelectObject(hdc, hBrushRow);
            SelectObject(hdc, hPenRow);
            RoundRect(hdc, 10, y, HUD_WIDTH - 10, y + 23, 8, 8);
            DeleteObject(hBrushRow);
            DeleteObject(hPenRow);

            SelectObject(hdc, hFontMain);
            SetTextColor(hdc, RGB(255, 230, 0));
            TextOutA(hdc, 16, y + 2, ">", 1);
        }

        SelectObject(hdc, hFontMain);
        COLORREF labelColor = isActive ? RGB(255, 255, 255) : RGB(170, 185, 205);
        SetTextColor(hdc, labelColor);

        // ROW 0: Neural Engine Toggle
        if (i == 0) {
            TextOutA(hdc, 30, y + 2, "Neural Engine", 13);
            SelectObject(hdc, hFontBadge);
            const char* txt = masterEnable ? "[ ACTIVE ]" : "[ BYPASS ]";
            COLORREF cBg = masterEnable ? RGB(15, 120, 55) : RGB(130, 28, 28);
            COLORREF cBd = masterEnable ? RGB(60, 240, 120) : RGB(250, 70, 70);
            HBRUSH hb = CreateSolidBrush(cBg);
            HPEN hp = CreatePen(PS_SOLID, 1, cBd);
            SelectObject(hdc, hb);
            SelectObject(hdc, hp);
            RoundRect(hdc, 220, y + 2, 310, y + 21, 6, 6);
            DeleteObject(hb);
            DeleteObject(hp);

            SetTextColor(hdc, RGB(255, 255, 255));
            RECT rc = { 220, y + 2, 310, y + 21 };
            DrawTextA(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        // ROW 1: NR Intensity Slider (0.0 to 5.0)
        else if (i == 1) {
            TextOutA(hdc, 30, y + 2, "NR Intensity", 12);

            char valBuf[16];
            sprintf_s(valBuf, sizeof(valBuf), "%.2f", intensity);
            SelectObject(hdc, hFontValue);
            SetTextColor(hdc, isActive ? RGB(0, 240, 255) : RGB(140, 200, 230));
            TextOutA(hdc, 170, y + 2, valBuf, (int)strlen(valBuf));

            int sx = 220, sy = y + 7, sw = 230, sh = 8;
            HBRUSH hTrackBg = CreateSolidBrush(RGB(22, 32, 48));
            HPEN hTrackPen = CreatePen(PS_SOLID, 1, RGB(45, 68, 98));
            SelectObject(hdc, hTrackBg);
            SelectObject(hdc, hTrackPen);
            RoundRect(hdc, sx, sy, sx + sw, sy + sh, 4, 4);
            DeleteObject(hTrackBg);
            DeleteObject(hTrackPen);

            float norm = intensity / 5.0f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            int fillW = (int)(sw * norm);
            if (fillW > 0) {
                HBRUSH hFill = CreateSolidBrush(RGB(0, 160, 225));
                HPEN hFillPen = CreatePen(PS_NULL, 0, 0);
                SelectObject(hdc, hFill);
                SelectObject(hdc, hFillPen);
                RoundRect(hdc, sx, sy, sx + fillW, sy + sh, 4, 4);
                DeleteObject(hFill);
                DeleteObject(hFillPen);
            }

            int thumbX = sx + fillW;
            if (thumbX > sx + sw) thumbX = sx + sw;
            HBRUSH hThumb = CreateSolidBrush(RGB(0, 255, 255));
            HPEN hThumbPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            SelectObject(hdc, hThumb);
            SelectObject(hdc, hThumbPen);
            RoundRect(hdc, thumbX - 3, sy - 3, thumbX + 3, sy + sh + 3, 4, 4);
            DeleteObject(hThumb);
            DeleteObject(hThumbPen);
        }
        // ROW 2: Sharpness / Tone Slider (0.0 to 2.0)
        else if (i == 2) {
            TextOutA(hdc, 30, y + 2, "Sharpness / Tone", 16);

            char valBuf[16];
            sprintf_s(valBuf, sizeof(valBuf), "%.2f", tone);
            SelectObject(hdc, hFontValue);
            SetTextColor(hdc, isActive ? RGB(0, 255, 200) : RGB(130, 220, 190));
            TextOutA(hdc, 170, y + 2, valBuf, (int)strlen(valBuf));

            int sx = 220, sy = y + 7, sw = 230, sh = 8;
            HBRUSH hTrackBg = CreateSolidBrush(RGB(22, 32, 48));
            HPEN hTrackPen = CreatePen(PS_SOLID, 1, RGB(45, 68, 98));
            SelectObject(hdc, hTrackBg);
            SelectObject(hdc, hTrackPen);
            RoundRect(hdc, sx, sy, sx + sw, sy + sh, 4, 4);
            DeleteObject(hTrackBg);
            DeleteObject(hTrackPen);

            float norm = tone / 2.0f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            int fillW = (int)(sw * norm);
            if (fillW > 0) {
                HBRUSH hFill = CreateSolidBrush(RGB(0, 180, 150));
                HPEN hFillPen = CreatePen(PS_NULL, 0, 0);
                SelectObject(hdc, hFill);
                SelectObject(hdc, hFillPen);
                RoundRect(hdc, sx, sy, sx + fillW, sy + sh, 4, 4);
                DeleteObject(hFill);
                DeleteObject(hFillPen);
            }

            int thumbX = sx + fillW;
            if (thumbX > sx + sw) thumbX = sx + sw;
            HBRUSH hThumb = CreateSolidBrush(RGB(50, 255, 200));
            HPEN hThumbPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            SelectObject(hdc, hThumb);
            SelectObject(hdc, hThumbPen);
            RoundRect(hdc, thumbX - 3, sy - 3, thumbX + 3, sy + sh + 3, 4, 4);
            DeleteObject(hThumb);
            DeleteObject(hThumbPen);
        }
        // ROW 3: AI Model Preset (0, 1, 2)
        else if (i == 3) {
            TextOutA(hdc, 30, y + 2, "AI Model Preset", 15);

            const char* presetNames[3] = { 
                "Preset 0  [DLSS-D Neural RR]", 
                "Preset 1  [Ultra Quality]", 
                "Preset 2  [Performance]" 
            };
            SelectObject(hdc, hFontValue);
            SetTextColor(hdc, isActive ? RGB(255, 240, 120) : RGB(210, 200, 160));
            TextOutA(hdc, 170, y + 2, presetNames[preset % 3], (int)strlen(presetNames[preset % 3]));
        }
        // ROW 4: HUD Position (Bottom-Center, Top-Center, Top-Right, Top-Left)
        else if (i == 4) {
            TextOutA(hdc, 30, y + 2, "HUD Position", 12);

            const char* posNames[4] = { 
                "Bottom-Center  (Default VR)", 
                "Top-Center     (Banner)", 
                "Top-Right      (Discrete)", 
                "Top-Left       (Gauge)" 
            };
            SelectObject(hdc, hFontValue);
            SetTextColor(hdc, isActive ? RGB(255, 220, 100) : RGB(210, 200, 150));
            TextOutA(hdc, 170, y + 2, posNames[posIdx % 4], (int)strlen(posNames[posIdx % 4]));
        }
        // ROW 5: VR UI Scale (1.0x, 1.5x, 2.0x)
        else if (i == 5) {
            TextOutA(hdc, 30, y + 2, "VR UI Scale", 11);

            const char* scaleNames[3] = { 
                "1.0x  [Compact / Pimax]", 
                "1.5x  [Balanced]", 
                "2.0x  [Comfort]" 
            };
            SelectObject(hdc, hFontValue);
            SetTextColor(hdc, isActive ? RGB(255, 220, 100) : RGB(210, 200, 150));
            TextOutA(hdc, 170, y + 2, scaleNames[scaleMode % 3], (int)strlen(scaleNames[scaleMode % 3]));
        }
    }

    // 6. Footer Help Bar
    HPEN hPenFoot = CreatePen(PS_SOLID, 1, RGB(30, 50, 75));
    SelectObject(hdc, hPenFoot);
    MoveToEx(hdc, 14, 196, NULL);
    LineTo(hdc, HUD_WIDTH - 14, 196);
    DeleteObject(hPenFoot);

    SelectObject(hdc, hFontHelp);
    SetTextColor(hdc, RGB(120, 160, 200));
    RECT rcHelp = { 16, 198, HUD_WIDTH - 16, HUD_HEIGHT - 2 };
    DrawTextA(hdc, "D-Pad: Navigate / Adjust  |  A: Toggle  |  Select+L3 / F6: Close", -1, &rcHelp, DT_CENTER | DT_SINGLELINE);

    // Cleanup GDI objects
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrushCard);
    DeleteObject(hPenBorder);
    DeleteObject(hFontTitle);
    DeleteObject(hFontMain);
    DeleteObject(hFontValue);
    DeleteObject(hFontBadge);
    DeleteObject(hFontHelp);

    // 7. Fast Alpha channel post-processing for both Desktop (OSD) and VR (OpenVR)
    for (int y = 0; y < HUD_HEIGHT; y++) {
        int rowIdx = y * HUD_WIDTH;
        for (int x = 0; x < HUD_WIDTH; x++) {
            int idx = rowIdx + x;
            uint32_t px = pGdiBits[idx];
            uint8_t b = (uint8_t)(px & 0xFF);
            uint8_t g = (uint8_t)((px >> 8) & 0xFF);
            uint8_t r = (uint8_t)((px >> 16) & 0xFF);

            uint32_t a = 0;
            uint32_t pr, pg, pb;
            if (r | g | b) {
                if (r <= 20 && g <= 24 && b <= 32) {
                    a = 230; // 90% translucent dark background
                    pr = (r * 230) >> 8;
                    pg = (g * 230) >> 8;
                    pb = (b * 230) >> 8;
                } else {
                    a = 255; // 100% solid for text, neon borders, sliders, badges
                    pr = r;
                    pg = g;
                    pb = b;
                }
            } else {
                pr = 0; pg = 0; pb = 0;
            }

            // Premultiplied BGRA for Windows UpdateLayeredWindow
            s_hudPixelsOSD[idx] = (a << 24) | (pr << 16) | (pg << 8) | pb;

            // Straight RGBA for OpenVR SetOverlayRaw (direct 32-bit dword write)
            *(uint32_t*)&s_hudPixelsVR[idx * 4] = (a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    }
}


// ----------------------------------------------------------------------------
// Dynamic OpenVR API Loader (Zero Static Dependency, Zero Loader Lock Deadlock)
// ----------------------------------------------------------------------------
typedef uint32_t (VR_CALLTYPE *PFN_VR_InitInternal2)(vr::EVRInitError *peError, vr::EVRApplicationType eApplicationType, const char *pStartupInfo);
typedef void (VR_CALLTYPE *PFN_VR_ShutdownInternal)();
typedef void* (VR_CALLTYPE *PFN_VR_GetGenericInterface)(const char *pchInterfaceVersion, vr::EVRInitError *peError);
typedef bool (VR_CALLTYPE *PFN_VR_IsInterfaceVersionValid)(const char *pchInterfaceVersion);
typedef const char* (VR_CALLTYPE *PFN_VR_GetVRInitErrorAsEnglishDescription)(vr::EVRInitError error);
typedef uint32_t (VR_CALLTYPE *PFN_VR_GetInitToken)();

static HMODULE g_hOpenVRDll = NULL;
static PFN_VR_InitInternal2 s_pfnVR_InitInternal2 = NULL;
static PFN_VR_ShutdownInternal s_pfnVR_ShutdownInternal = NULL;
static PFN_VR_GetGenericInterface s_pfnVR_GetGenericInterface = NULL;
static PFN_VR_IsInterfaceVersionValid s_pfnVR_IsInterfaceVersionValid = NULL;
static PFN_VR_GetVRInitErrorAsEnglishDescription s_pfnVR_GetVRInitErrorAsEnglishDescription = NULL;
static PFN_VR_GetInitToken s_pfnVR_GetInitToken = NULL;

static bool LoadOpenVRAPI()
{
    if (g_hOpenVRDll) return true;

    // 1. Essayer dans le dossier du jeu en cours
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
        char* slash = strrchr(path, '\\');
        if (slash) {
            *(slash + 1) = '\0';
            strcat_s(path, MAX_PATH, "openvr_api.dll");
            g_hOpenVRDll = LoadLibraryA(path);
        }
    }
    // 2. Repli standard
    if (!g_hOpenVRDll) {
        g_hOpenVRDll = LoadLibraryA("openvr_api.dll");
    }
    if (!g_hOpenVRDll) {
        LogMsg("[OpenVR-Dynamic] WARNING: openvr_api.dll could not be loaded");
        return false;
    }

    s_pfnVR_InitInternal2 = (PFN_VR_InitInternal2)GetProcAddress(g_hOpenVRDll, "VR_InitInternal2");
    s_pfnVR_ShutdownInternal = (PFN_VR_ShutdownInternal)GetProcAddress(g_hOpenVRDll, "VR_ShutdownInternal");
    s_pfnVR_GetGenericInterface = (PFN_VR_GetGenericInterface)GetProcAddress(g_hOpenVRDll, "VR_GetGenericInterface");
    s_pfnVR_IsInterfaceVersionValid = (PFN_VR_IsInterfaceVersionValid)GetProcAddress(g_hOpenVRDll, "VR_IsInterfaceVersionValid");
    s_pfnVR_GetVRInitErrorAsEnglishDescription = (PFN_VR_GetVRInitErrorAsEnglishDescription)GetProcAddress(g_hOpenVRDll, "VR_GetVRInitErrorAsEnglishDescription");
    s_pfnVR_GetInitToken = (PFN_VR_GetInitToken)GetProcAddress(g_hOpenVRDll, "VR_GetInitToken");

    bool ok = (s_pfnVR_InitInternal2 && s_pfnVR_GetGenericInterface && s_pfnVR_IsInterfaceVersionValid);
    if (ok) {
        LogMsg("[OpenVR-Dynamic] openvr_api.dll dynamically resolved successfully!");
    } else {
        LogMsg("[OpenVR-Dynamic] ERROR: Failed to resolve OpenVR core entry points");
    }
    return ok;
}

namespace vr {
uint32_t VR_CALLTYPE VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo)
{
    if (!LoadOpenVRAPI() || !s_pfnVR_InitInternal2) {
        if (peError) *peError = VRInitError_Init_FileNotFound;
        return 0;
    }
    return s_pfnVR_InitInternal2(peError, eApplicationType, pStartupInfo);
}

void VR_CALLTYPE VR_ShutdownInternal()
{
    if (s_pfnVR_ShutdownInternal) s_pfnVR_ShutdownInternal();
}

void* VR_CALLTYPE VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError)
{
    if (!LoadOpenVRAPI() || !s_pfnVR_GetGenericInterface) {
        if (peError) *peError = VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
    return s_pfnVR_GetGenericInterface(pchInterfaceVersion, peError);
}

bool VR_CALLTYPE VR_IsInterfaceVersionValid(const char *pchInterfaceVersion)
{
    if (!LoadOpenVRAPI() || !s_pfnVR_IsInterfaceVersionValid) return false;
    return s_pfnVR_IsInterfaceVersionValid(pchInterfaceVersion);
}

const char* VR_CALLTYPE VR_GetVRInitErrorAsEnglishDescription(EVRInitError error)
{
    if (s_pfnVR_GetVRInitErrorAsEnglishDescription) return s_pfnVR_GetVRInitErrorAsEnglishDescription(error);
    return "OpenVR error";
}

uint32_t VR_CALLTYPE VR_GetInitToken()
{
    if (s_pfnVR_GetInitToken) return s_pfnVR_GetInitToken();
    return 0;
}
}

// ----------------------------------------------------------------------------
// OpenVR SteamVR Native Compositor Overlay (fpsVR Architecture)
// Zero-Crash, 100% Decoupled from Game Engine & D3D12 Pipeline
// ----------------------------------------------------------------------------
static vr::IVROverlay* g_pVROverlay = NULL;
static vr::VROverlayHandle_t g_hVROverlay = vr::k_ulOverlayHandleInvalid;
static bool g_openvrInitialized = false;
static uint64_t g_lastOpenVRInitAttempt = 0;

static void ApplyOverlayTransformAndScale()
{
    if (!g_pVROverlay || g_hVROverlay == vr::k_ulOverlayHandleInvalid) return;

    // Finer, more compact physical size in VR for high-DPI Pimax / Quest 3
    float widthInMeters = 0.22f;
    if (g_hudScale == 0) widthInMeters = 0.22f;      // 1.0x Compact / Pimax
    else if (g_hudScale == 1) widthInMeters = 0.28f; // 1.5x Balanced
    else if (g_hudScale == 2) widthInMeters = 0.36f; // 2.0x Comfort
    g_pVROverlay->SetOverlayWidthInMeters(g_hVROverlay, widthInMeters);

    float posX = 0.0f;
    float posY = -0.22f; // Sweet spot bas (fpsVR / dashboard)
    float posZ = -0.75f; // 75 cm distance

    switch (g_hudPosIndex % 4) {
    case 0: // Bottom-Center
        posX = 0.0f; posY = -0.22f; posZ = -0.75f;
        break;
    case 1: // Top-Center
        posX = 0.0f; posY = +0.20f; posZ = -0.75f;
        break;
    case 2: // Top-Right
        posX = +0.26f; posY = +0.16f; posZ = -0.75f;
        break;
    case 3: // Top-Left
        posX = -0.26f; posY = +0.16f; posZ = -0.75f;
        break;
    }

    vr::HmdMatrix34_t mat = {};
    mat.m[0][0] = 1.0f;
    mat.m[1][1] = 1.0f;
    mat.m[2][2] = 1.0f;
    mat.m[0][3] = posX;
    mat.m[1][3] = posY;
    mat.m[2][3] = posZ;

    g_pVROverlay->SetOverlayTransformTrackedDeviceRelative(g_hVROverlay, vr::k_unTrackedDeviceIndex_Hmd, &mat);
}

static bool EnsureOpenVROverlay()
{
    // If already connected and overlay handle is valid, we are ready!
    if (g_openvrInitialized && g_pVROverlay && g_hVROverlay != vr::k_ulOverlayHandleInvalid) {
        return true;
    }

    // Splashscreen guard for initial connection ONLY:
    // Only connect if user requested HUD (g_hudVisible) or if 3D frames are running or if 15s elapsed
    static uint64_t s_bootTick = 0;
    if (s_bootTick == 0) s_bootTick = GetTickCount64();

    if (!g_hudVisible && g_evalFrameCounter < 5 && (GetTickCount64() - s_bootTick < 15000)) {
        return false;
    }

    if (!g_openvrInitialized) {
        uint64_t now = GetTickCount64();
        if (now - g_lastOpenVRInitAttempt < 2000) {
            return false;
        }
        g_lastOpenVRInitAttempt = now;

        vr::EVRInitError err = vr::VRInitError_None;
        vr::IVRSystem* pSys = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err != vr::VRInitError_None || !pSys) {
            char buf[128];
            sprintf_s(buf, sizeof(buf), "[OpenVR-Overlay] SteamVR waiting... (code %d: %s)", 
                err, vr::VR_GetVRInitErrorAsEnglishDescription(err));
            LogMsg(buf);
            return false;
        }
        g_openvrInitialized = true;
        LogMsg("[OpenVR-Overlay] Successfully connected to SteamVR Compositor (VRApplication_Overlay)");

        // Query native HMD refresh rate from SteamVR
        vr::ETrackedPropertyError propErr = vr::TrackedProp_Success;
        float freq = pSys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &propErr);
        if (propErr == vr::TrackedProp_Success && freq >= 60.0f && freq <= 240.0f) {
            g_liveHz = (int)(freq + 0.5f);
            char hzBuf[128];
            sprintf_s(hzBuf, sizeof(hzBuf), "[OpenVR-Overlay] Native HMD refresh rate detected: %d Hz", g_liveHz);
            LogMsg(hzBuf);
        }
    }

    if (!g_pVROverlay) {
        g_pVROverlay = vr::VROverlay();
        if (!g_pVROverlay) {
            LogMsg("[OpenVR-Overlay] ERROR: VROverlay interface is NULL");
            return false;
        }
    }

    if (g_hVROverlay == vr::k_ulOverlayHandleInvalid) {
        vr::EVROverlayError ovrErr = g_pVROverlay->CreateOverlay("VRDLSS5_HUD", "DLSS 5 VR Controller", &g_hVROverlay);
        if (ovrErr != vr::VROverlayError_None) {
            char buf[128];
            sprintf_s(buf, sizeof(buf), "[OpenVR-Overlay] CreateOverlay failed: %d", ovrErr);
            LogMsg(buf);
            return false;
        }
        g_pVROverlay->SetOverlayAlpha(g_hVROverlay, 0.96f);
        ApplyOverlayTransformAndScale();
        LogMsg("[OpenVR-Overlay] SteamVR Overlay created and armed successfully!");
    }

    return true;
}

static void UpdateOpenVROverlay(bool visible, bool isDirty)
{
    static bool s_lastVisible = false;
    static int s_lastScale = -1;
    static int s_lastPos = -1;

    if (!visible) {
        if (s_lastVisible) {
            if (g_pVROverlay && g_hVROverlay != vr::k_ulOverlayHandleInvalid) {
                g_pVROverlay->HideOverlay(g_hVROverlay);
            }
            s_lastVisible = false;
        }
        return;
    }

    if (!EnsureOpenVROverlay()) {
        return;
    }

    if (!s_lastVisible) {
        g_pVROverlay->ShowOverlay(g_hVROverlay);
        s_lastVisible = true;
        isDirty = true; // Force fresh texture upload on reveal!
    }

    if (s_lastScale != g_hudScale || s_lastPos != g_hudPosIndex) {
        s_lastScale = g_hudScale;
        s_lastPos = g_hudPosIndex;
        ApplyOverlayTransformAndScale();
    }

    if (isDirty) {
        vr::EVROverlayError ovrErr = g_pVROverlay->SetOverlayRaw(g_hVROverlay, s_hudPixelsVR, HUD_WIDTH, HUD_HEIGHT, 4);
        if (ovrErr != vr::VROverlayError_None) {
            char buf[128];
            sprintf_s(buf, sizeof(buf), "[OpenVR-Overlay] SetOverlayRaw error: %d - initiating self-healing recovery...", ovrErr);
            LogMsg(buf);

            // Self-healing recovery: destroy and recreate overlay handle
            g_pVROverlay->DestroyOverlay(g_hVROverlay);
            g_hVROverlay = vr::k_ulOverlayHandleInvalid;
            s_lastVisible = false;
            s_lastScale = -1;
            s_lastPos = -1;

            if (EnsureOpenVROverlay()) {
                g_pVROverlay->ShowOverlay(g_hVROverlay);
                s_lastVisible = true;
                ApplyOverlayTransformAndScale();
                vr::EVROverlayError retryErr = g_pVROverlay->SetOverlayRaw(g_hVROverlay, s_hudPixelsVR, HUD_WIDTH, HUD_HEIGHT, 4);
                if (retryErr == vr::VROverlayError_None) {
                    LogMsg("[OpenVR-Overlay] Self-healing recovery successful: texture re-uploaded!");
                } else {
                    sprintf_s(buf, sizeof(buf), "[OpenVR-Overlay] Recovery retry error: %d", retryErr);
                    LogMsg(buf);
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Input Polling & 500 ms Debounce Manager
// ----------------------------------------------------------------------------
static void UpdateKey(int vk, uint64_t now)
{
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    KeyTracker &k = g_keys[vk];
    if (down) {
        if (!k.isDown) {
            k.isDown = true;
            k.justPressed = true;
            k.downSince = now;
            k.lastRepeat = now;
        } else {
            k.justPressed = false;
        }
    } else {
        k.isDown = false;
        k.justPressed = false;
        k.downSince = 0;
        k.lastRepeat = 0;
    }
}

static bool CheckAction(int vk, uint64_t now, bool &isHolding)
{
    KeyTracker &k = g_keys[vk];
    if (k.justPressed) {
        isHolding = false;
        return true;
    }
    if (k.isDown && (now - k.downSince >= 300) && (now - k.lastRepeat >= 50)) {
        k.lastRepeat = now;
        isHolding = true;
        return true;
    }
    return false;
}

static void PollInput()
{
    uint64_t now = GetTickCount64();

    // 1. Keyboard F6 & Gamepad Select + L3 Toggle (avec cooldown 350ms anti-rebond)
    static uint64_t s_lastToggleTick = 0;
    bool toggleRequested = false;

    UpdateKey(VK_F6, now);
    if (g_keys[VK_F6].justPressed && (now - s_lastToggleTick >= 350)) {
        s_lastToggleTick = now;
        toggleRequested = true;
    }

    // Polling XInput (Xbox, emulators)
    XINPUT_STATE xstate;
    ZeroMemory(&xstate, sizeof(XINPUT_STATE));
    bool xinputConnected = false;
    for (DWORD i = 0; i < 4; i++) {
        DWORD res = ERROR_DEVICE_NOT_CONNECTED;
        if (g_origRealVR_GetState) {
            res = g_origRealVR_GetState(i, &xstate);
        } else if (g_origXInput1_4_GetState) {
            res = g_origXInput1_4_GetState(i, &xstate);
        } else {
            res = XInputGetState(i, &xstate);
        }
        if (res == ERROR_SUCCESS) {
            xinputConnected = true;
            break;
        }
    }
    WORD xButtons = xinputConnected ? xstate.Gamepad.wButtons : 0;

    // Polling DirectInput / winmm (DualSense PS5, manettes HID natives)
    JOYINFOEX jie;
    ZeroMemory(&jie, sizeof(JOYINFOEX));
    jie.dwSize = sizeof(JOYINFOEX);
    jie.dwFlags = JOY_RETURNALL;
    bool dinputConnected = false;
    for (UINT j = 0; j < 4; j++) {
        MMRESULT jres = g_origJoyGetPosEx ? g_origJoyGetPosEx(j, &jie) : joyGetPosEx(j, &jie);
        if (jres == JOYERR_NOERROR) {
            dinputConnected = true;
            break;
        }
    }
    DWORD dButtons = dinputConnected ? jie.dwButtons : 0;

    // Combinaison universelle Select + L3 (Back + Clic Stick Gauche)
    // Xbox: BACK (0x20) | LEFT_THUMB (0x40) = 0x60
    // DualSense DirectInput: Create/Select (bit 8 = 0x100) | L3 (bit 10 = 0x400) = 0x500
    bool comboDown = ((xButtons & 0x0060) == 0x0060) || 
                     ((dButtons & 0x0500) == 0x0500);
    static bool s_prevCombo = false;
    if (comboDown && !s_prevCombo && (now - s_lastToggleTick >= 350)) {
        s_lastToggleTick = now;
        toggleRequested = true;
    }
    s_prevCombo = comboDown;

    if (toggleRequested) {
        g_hudVisible = !g_hudVisible;
        g_hudDirty = true;
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Overlay toggled: %s (Source: %s)", 
            g_hudVisible ? "OPEN" : "CLOSED",
            g_keys[VK_F6].justPressed ? "Keyboard F6" : "Gamepad Select+L3");
        LogMsg(buf);
    }

    if (!g_hudVisible) return;

    // 2. Active HUD Controls
    UpdateKey(VK_TAB, now);
    UpdateKey(VK_F7, now);
    UpdateKey(VK_ESCAPE, now);
    UpdateKey(VK_UP, now);
    UpdateKey(VK_DOWN, now);
    UpdateKey(VK_LEFT, now);
    UpdateKey(VK_RIGHT, now);
    UpdateKey(VK_SPACE, now);
    UpdateKey(VK_RETURN, now);

    // Close HUD: Escape (Keyboard)
    if (g_keys[VK_ESCAPE].justPressed) {
        g_hudVisible = false;
        g_hudDirty = true;
        s_lastToggleTick = now;
        LogMsg("[VR-DLSS5-HUD] Overlay closed via Escape");
        return;
    }

    // Cycle Position: Tab or Gamepad Y (Xbox Y: 0x8000 / DualSense Triangle: bit 3 = 0x0008)
    static bool s_prevPadY = false;
    bool padY = ((xButtons & XINPUT_GAMEPAD_Y) != 0) || ((dButtons & 0x0008) != 0);
    if (g_keys[VK_TAB].justPressed || (padY && !s_prevPadY)) {
        g_hudPosIndex = (g_hudPosIndex + 1) % 4;
        g_hudDirty = true;
        static const char* posNames[] = { "Bottom-Center", "Top-Center", "Top-Right", "Top-Left" };
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Position changed to: %s", posNames[g_hudPosIndex]);
        LogMsg(buf);
        g_hasPendingSave = true;
        g_lastChangeTick = now;
    }
    s_prevPadY = padY;

    // Direct Scale Toggle: F7 (Keyboard) or R3 (Right Stick Click: Xbox 0x0080 / DualSense bit 11 = 0x0800)
    static bool s_prevPadR3 = false;
    bool padR3 = ((xButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0) || ((dButtons & 0x0800) != 0);
    if (g_keys[VK_F7].justPressed || (padR3 && !s_prevPadR3)) {
        g_hudScale = (g_hudScale + 1) % 3;
        g_hudDirty = true;
        static const char* scaleNames[] = { "1.0x (Compact)", "1.5x (Balanced Q3)", "2.0x (Comfort Q3)" };
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Scale toggled to: %s", scaleNames[g_hudScale]);
        LogMsg(buf);
        g_hasPendingSave = true;
        g_lastChangeTick = now;
    }
    s_prevPadR3 = padR3;

    // Navigate Rows (6 rows: 0 to 5) - PURE DIGITAL D-PAD (pas de parasitage par le stick analogique)
    static bool s_prevPadUp = false;
    static bool s_prevPadDown = false;
    bool padUp = ((xButtons & XINPUT_GAMEPAD_DPAD_UP) != 0) || 
                 (dinputConnected && (jie.dwPOV == 0 || jie.dwPOV == 31500 || jie.dwPOV == 4500));
    bool padDown = ((xButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0) || 
                   (dinputConnected && (jie.dwPOV == 18000 || jie.dwPOV == 13500 || jie.dwPOV == 22500));

    if (g_keys[VK_UP].justPressed || (padUp && !s_prevPadUp)) {
        g_activeRow = (g_activeRow + 5) % 6;
        g_hudDirty = true;
    }
    if (g_keys[VK_DOWN].justPressed || (padDown && !s_prevPadDown)) {
        g_activeRow = (g_activeRow + 1) % 6;
        g_hudDirty = true;
    }
    s_prevPadUp = padUp;
    s_prevPadDown = padDown;

    bool actLeft = false;
    bool actRight = false;
    bool isHoldLeft = false;
    bool isHoldRight = false;

    if (CheckAction(VK_LEFT, now, isHoldLeft)) actLeft = true;
    if (CheckAction(VK_RIGHT, now, isHoldRight)) actRight = true;

    static uint64_t s_padLeftSince = 0, s_padLeftRepeat = 0;
    static uint64_t s_padRightSince = 0, s_padRightRepeat = 0;
    bool padLeft = ((xButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0) || 
                   (dinputConnected && (jie.dwPOV == 27000 || jie.dwPOV == 22500 || jie.dwPOV == 31500));
    bool padRight = ((xButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) || 
                    (dinputConnected && (jie.dwPOV == 9000 || jie.dwPOV == 4500 || jie.dwPOV == 13500));

    if (padLeft) {
        if (s_padLeftSince == 0) {
            s_padLeftSince = now; s_padLeftRepeat = now; actLeft = true; isHoldLeft = false;
        } else if ((now - s_padLeftSince >= 300) && (now - s_padLeftRepeat >= 50)) {
            s_padLeftRepeat = now; actLeft = true; isHoldLeft = true;
        }
    } else {
        s_padLeftSince = 0;
    }

    if (padRight) {
        if (s_padRightSince == 0) {
            s_padRightSince = now; s_padRightRepeat = now; actRight = true; isHoldRight = false;
        } else if ((now - s_padRightSince >= 300) && (now - s_padRightRepeat >= 50)) {
            s_padRightRepeat = now; actRight = true; isHoldRight = true;
        }
    } else {
        s_padRightSince = 0;
    }

    static bool s_prevPadA = false;
    bool padA = ((xButtons & XINPUT_GAMEPAD_A) != 0) || ((dButtons & 0x0002) != 0); // Xbox A or DualSense Cross
    bool actionTrigger = g_keys[VK_SPACE].justPressed || g_keys[VK_RETURN].justPressed || (padA && !s_prevPadA);
    s_prevPadA = padA;

    bool valueChanged = false;

    if (g_activeRow == 0) { // Neural Engine Toggle
        if (actionTrigger || actLeft || actRight) {
            g_masterEnable = !g_masterEnable;
            valueChanged = true;
        }
    }
    else if (g_activeRow == 1) { // Intensity Slider (0.0 to 5.0)
        float step = (isHoldLeft || isHoldRight) ? 0.10f : 0.05f;
        if (actLeft) {
            g_nrIntensity = fmaxf(0.0f, g_nrIntensity - step);
            valueChanged = true;
        }
        if (actRight) {
            g_nrIntensity = fminf(5.0f, g_nrIntensity + step);
            valueChanged = true;
        }
    }
    else if (g_activeRow == 2) { // Sharpness / Tone Slider (0.0 to 2.0)
        float step = (isHoldLeft || isHoldRight) ? 0.05f : 0.02f;
        if (actLeft) {
            g_nrGlobalTone = fmaxf(0.0f, g_nrGlobalTone - step);
            valueChanged = true;
        }
        if (actRight) {
            g_nrGlobalTone = fminf(2.0f, g_nrGlobalTone + step);
            valueChanged = true;
        }
    }
    else if (g_activeRow == 3) { // AI Preset Selector (0, 1, 2)
        if (actionTrigger || actRight) {
            g_nrPreset = (g_nrPreset + 1) % 3;
            valueChanged = true;
        } else if (actLeft) {
            g_nrPreset = (g_nrPreset + 2) % 3;
            valueChanged = true;
        }
    }
    else if (g_activeRow == 4) { // HUD Position (0: Bas-Centre, 1: Haut-Centre, 2: Haut-Droite, 3: Haut-Gauche)
        if (actionTrigger || actRight) {
            g_hudPosIndex = (g_hudPosIndex + 1) % 4;
            valueChanged = true;
        } else if (actLeft) {
            g_hudPosIndex = (g_hudPosIndex + 3) % 4;
            valueChanged = true;
        }
    }
    else if (g_activeRow == 5) { // VR Scale (0: 1.0x Compact, 1: 1.5x Equilibre, 2: 2.0x Confort)
        if (actionTrigger || actRight) {
            g_hudScale = (g_hudScale + 1) % 3;
            valueChanged = true;
        } else if (actLeft) {
            g_hudScale = (g_hudScale + 2) % 3;
            valueChanged = true;
        }
    }

    // Real-time immediate RAM update & Arm 500ms debounce
    if (valueChanged) {
        g_hudDirty = true;
        g_hasPendingSave = true;
        g_lastChangeTick = now;

        if (!g_renodxBase) {
            g_renodxBase = (uintptr_t)GetModuleHandleA("renodx-dlss5.addon64");
        }
        if (g_renodxBase) {
            *(uint8_t*)(g_renodxBase + 0x192F68) = g_masterEnable ? 1 : 0;
            *(float*)(g_renodxBase + 0x19364C) = g_masterEnable ? g_nrIntensity : 0.0f;
            *(float*)(g_renodxBase + 0x193650) = g_nrGlobalTone;
            *(int32_t*)(g_renodxBase + 0x196B98) = g_nrPreset;
            *(uint8_t*)(g_renodxBase + 0x1935E8) = 1; // set dirty flag
        }
    }
}

// ----------------------------------------------------------------------------
// Desktop & VR Mirror Floating OSD Window (Transparent Layered Per-Pixel Alpha)
// ----------------------------------------------------------------------------
static HWND g_hOSDWnd = NULL;
static HDC g_hOSDDC = NULL;
static HBITMAP g_hOSDBmp = NULL;
static uint32_t* g_pOSDBits = NULL;
static int g_currentOSDScale = -1;

static void UpdateOSDWindow(bool visible, bool isDirty)
{
    if (!visible) {
        if (g_hOSDWnd && IsWindowVisible(g_hOSDWnd)) {
            ShowWindow(g_hOSDWnd, SW_HIDE);
        }
        return;
    }

    // 1. Enregistrer la classe de fenêtre
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "VR_DLSS5_OSD_WindowClass";
        RegisterClassExA(&wc);
        s_classRegistered = true;
    }

    // 2. Créer la fenêtre layered transparente si nécessaire
    if (!g_hOSDWnd) {
        g_hOSDWnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            "VR_DLSS5_OSD_WindowClass", "VR_DLSS5_HUD_OSD",
            WS_POPUP,
            0, 0, 100, 100,
            NULL, NULL, GetModuleHandleA(NULL), NULL);
        if (!g_hOSDWnd) return;
    }

    if (!isDirty && IsWindowVisible(g_hOSDWnd)) {
        return; // Zero CPU / GDI work when HUD is static
    }

    // 3. Déterminer les dimensions actuelles selon l'échelle desktop (1.0x, 1.25x, 1.5x)
    int scaleNum = 1, scaleDen = 1;
    if (g_hudScale == 1) { scaleNum = 5; scaleDen = 4; }
    else if (g_hudScale == 2) { scaleNum = 3; scaleDen = 2; }
    int curW = (HUD_WIDTH * scaleNum) / scaleDen;
    int curH = (HUD_HEIGHT * scaleNum) / scaleDen;

    // 4. Allouer ou réallouer le DIBSection si l'échelle a changé
    if (!g_hOSDDC || !g_hOSDBmp || g_currentOSDScale != g_hudScale) {
        if (g_hOSDBmp) { DeleteObject(g_hOSDBmp); g_hOSDBmp = NULL; }
        if (g_hOSDDC) { DeleteDC(g_hOSDDC); g_hOSDDC = NULL; }

        HDC hdcScreen = GetDC(NULL);
        g_hOSDDC = CreateCompatibleDC(hdcScreen);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = curW;
        bmi.bmiHeader.biHeight = -curH; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        g_hOSDBmp = CreateDIBSection(g_hOSDDC, &bmi, DIB_RGB_COLORS, (void**)&g_pOSDBits, NULL, 0);
        SelectObject(g_hOSDDC, g_hOSDBmp);
        ReleaseDC(NULL, hdcScreen);
        g_currentOSDScale = g_hudScale;
    }

    if (!g_pOSDBits) return;

    // 5. Transférer avec alpha prémultiplié pour UpdateLayeredWindow
    for (int y = 0; y < curH; y++) {
        int srcY = (y * scaleDen) / scaleNum;
        if (srcY >= HUD_HEIGHT) srcY = HUD_HEIGHT - 1;
        uint32_t* pDstRow = g_pOSDBits + y * curW;
        for (int x = 0; x < curW; x++) {
            int srcX = (x * scaleDen) / scaleNum;
            if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
            pDstRow[x] = s_hudPixelsOSD[srcY * HUD_WIDTH + srcX];
        }
    }

    // 6. Calculer la position sur l'écran principal
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (scrW <= 0) scrW = 1920;
    if (scrH <= 0) scrH = 1080;

    int dstX = (scrW - curW) / 2;
    int dstY = scrH - curH - 80;

    switch (g_hudPosIndex % 4) {
    case 0: // Bas-Centre
        dstX = (scrW - curW) / 2;
        dstY = scrH - curH - 80;
        break;
    case 1: // Haut-Centre
        dstX = (scrW - curW) / 2;
        dstY = 60;
        break;
    case 2: // Haut-Droite
        dstX = scrW - curW - 80;
        dstY = 60;
        break;
    case 3: // Haut-Gauche
        dstX = 80;
        dstY = 60;
        break;
    }

    HDC hdcScreen = GetDC(NULL);
    POINT ptDst = { dstX, dstY };
    SIZE szDst = { curW, curH };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hOSDWnd, hdcScreen, &ptDst, &szDst, g_hOSDDC, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);

    // Maintenir en permanence au premier plan absolu au-dessus du jeu plein écran
    SetWindowPos(g_hOSDWnd, HWND_TOPMOST, dstX, dstY, curW, curH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ----------------------------------------------------------------------------
// Autonomous Input Watcher Thread (100% Découplé, Zero Crash, 0 ms RAM Sync)
// ----------------------------------------------------------------------------
static DWORD WINAPI InputWatcherThread(LPVOID lpParam)
{
    LogMsg("[Proxy] Input Watcher Thread started.");

    HDC hdc = GetDC(NULL);
    if (hdc) {
        int vRef = GetDeviceCaps(hdc, VREFRESH);
        ReleaseDC(NULL, hdc);
        if (vRef >= 60 && vRef <= 240) g_liveHz = vRef;
    }

    // Initialize the offscreen GDI rasterizer DC and DIB section
    InitGDIRasterizer();

    LogMsg("[Proxy] Input Watcher Thread ready: polling F6 and Select+L3...");

    while (true)
    {
        uint64_t now = GetTickCount64();

        // 1. Initialiser paresseusement les variables au premier lancement
        InitVariablesFromAddonOrIni();

        // 2. Maintenir la connexion OpenVR active et purger la file IPC d'événements (anti-saturation erreur 23)
        EnsureOpenVROverlay();
        if (g_pVROverlay && g_hVROverlay != vr::k_ulOverlayHandleInvalid) {
            vr::VREvent_t vrEvent;
            while (g_pVROverlay->PollNextOverlayEvent(g_hVROverlay, &vrEvent, sizeof(vrEvent))) {
                if (vrEvent.eventType == vr::VREvent_Quit || vrEvent.eventType == vr::VREvent_ProcessQuit) {
                    LogMsg("[OpenVR-Overlay] SteamVR quit event detected, resetting overlay connection");
                    g_pVROverlay = NULL;
                    g_hVROverlay = vr::k_ulOverlayHandleInvalid;
                    g_openvrInitialized = false;
                    break;
                }
            }
        }

        // Intercepter dynamiquement de nouveaux modules XInput si charges tardivement
        static uint64_t s_lastHookCheck = 0;
        static int s_hookChecks = 0;
        if (s_hookChecks < 10 && (now - s_lastHookCheck >= 1000)) {
            s_lastHookCheck = now;
            s_hookChecks++;
            InstallXInputHooks();
        }

        // Mesurer le framerate reel de maniere 100% asynchrone sans surcharger le thread de rendu
        static uint64_t s_lastFpsMeasureTick = 0;
        static unsigned long long s_lastFpsFrameCount = 0;
        if (s_lastFpsMeasureTick == 0) s_lastFpsMeasureTick = now;
        if (now - s_lastFpsMeasureTick >= 1000)
        {
            uint64_t elapsed = now - s_lastFpsMeasureTick;
            unsigned long long curFrames = g_evalFrameCounter;
            if (elapsed > 0 && curFrames >= s_lastFpsFrameCount)
            {
                int measuredFps = (int)((curFrames - s_lastFpsFrameCount) * 1000 / elapsed);
                if (measuredFps > 0 && measuredFps <= 240)
                {
                    if (abs(measuredFps - g_liveHz) > 2) {
                        g_liveHz = measuredFps;
                        if (g_hudVisible) g_hudDirty = true;
                    }
                }
            }
            s_lastFpsFrameCount = curFrames;
            s_lastFpsMeasureTick = now;
        }

        // 3. Écouter les entrées clavier (F6) et manettes (Select+L3)
        PollInput();

        // 4. Mettre à jour les affichages Bureau et Casque VR uniquement lors d'un changement
        bool isDirty = g_hudDirty;
        if (g_hudVisible && isDirty) {
            RenderModernHUD(g_hGdiMemDC, g_pGdiBits, g_masterEnable, g_nrIntensity, g_nrGlobalTone, 
                            g_nrPreset, g_hudPosIndex, g_hudScale, g_activeRow, g_liveHz);
        }
        UpdateOSDWindow(g_hudVisible, isDirty);
        UpdateOpenVROverlay(g_hudVisible, isDirty);
        if (isDirty) {
            g_hudDirty = false;
        }

        // 5. Persistence différée (500 ms debounce sans micro-stutter)
        if (g_hasPendingSave && (now - g_lastChangeTick >= 500))
        {
            g_hasPendingSave = false;
            CommitSettingsToDisk();
        }

        // Mode ultra-leger zero-stutter pour VR (LukeRoss 72Hz Quest 3 / Pimax) :
        // - HUD ferme : 20 Hz (Sleep 50ms) -> CPU quasi 0.000%, reactivite F6 / Select+L3 instantanee (50ms)
        // - HUD ouvert : 30 Hz (Sleep 33ms) -> navigation fluide, et zero recalcul/blit si inactif (g_hudDirty)
        Sleep(g_hudVisible ? 33 : 50);
    }
    return 0;
}

extern "C" {

int WINAPI Proxy_NVSDK_NGX_D3D12_EvaluateFeature(void* pCmdList, void* pHandle, void* pParameters, void* pCallback)
{
    g_evalFrameCounter++;

    // Ultra-lean zero-overhead pass-through (zero disk I/O, zero string serialization, zero FP math)
    if (g_pfnNGXEvaluateFeature)
    {
        return g_pfnNGXEvaluateFeature(pCmdList, pHandle, pParameters, pCallback);
    }
    return 0;
}



int WINAPI Proxy_NVSDK_NGX_D3D12_ReleaseFeature(void* pHandle)
{
    InitProxy();
    if (!g_pfnNGXReleaseFeature && g_hRealVR)
        g_pfnNGXReleaseFeature = (PFN_NVSDK_NGX_D3D12_ReleaseFeature)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_ReleaseFeature");

    char buf[128];
    sprintf_s(buf, sizeof(buf), "[VR-DLSS5] ReleaseFeature called: handle=%p", pHandle);
    LogMsg(buf);

    if (g_pfnNGXReleaseFeature)
        return g_pfnNGXReleaseFeature(pHandle);
    return 1;
}

DWORD WINAPI Proxy_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    InitProxy();
    DWORD res = ERROR_DEVICE_NOT_CONNECTED;
    if (g_origRealVR_GetState) {
        res = g_origRealVR_GetState(dwUserIndex, pState);
    } else if (g_origXInput1_4_GetState) {
        res = g_origXInput1_4_GetState(dwUserIndex, pState);
    } else {
        res = XInputGetState(dwUserIndex, pState);
    }
    return FilterXInputState(dwUserIndex, pState, res);
}

}


