#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <xinput.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "font8x14.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "user32.lib")

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
    FILE *f = fopen("vr_dlss5_proxy.log", "a");
    if (f)
    {
        fprintf(f, "%s\n", msg);
        fclose(f);
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
    LogMsg("[Proxy] Proxy ready.");
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

#define HUD_WIDTH 384
#define HUD_HEIGHT 144

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
static bool g_hudVisible = false;
static bool g_masterEnable = true;
static float g_nrIntensity = 2.50f;
static float g_nrGlobalTone = 1.00f;
static int g_nrPreset = 0;
static int g_hudScale = 1; // 0: 1.0x (Compact / Pimax), 1: 1.5x (Balanced / Quest 3 Default), 2: 2.0x (Comfort / Quest 3)
static int g_activeRow = 0; // 0 to 4
static int g_hudPosIndex = 0; // 0: Bottom-Center (Default), 1: Top-Center, 2: Top-Right, 3: Top-Left
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
        g_masterEnable = (*(uint8_t*)(g_renodxBase + 0x192F68)) != 0;
        g_nrIntensity = *(float*)(g_renodxBase + 0x19364C);
        g_nrGlobalTone = *(float*)(g_renodxBase + 0x193650);
        g_nrPreset = *(int32_t*)(g_renodxBase + 0x196B98);

        if (g_nrIntensity <= 0.0f || g_nrIntensity > 10.0f) g_nrIntensity = 2.50f;
        if (g_nrGlobalTone <= 0.0f || g_nrGlobalTone > 5.0f) g_nrGlobalTone = 1.00f;
        if (g_nrPreset < 0 || g_nrPreset > 2) g_nrPreset = 0;

        // Load scale & position from INI (default: 1.5x for Quest 3 comfort)
        g_hudScale = GetPrivateProfileIntA("RenoDX.DLSS5", "HUDScale", 1, g_iniPath);
        if (g_hudScale < 0 || g_hudScale > 2) g_hudScale = 1;

        g_hudPosIndex = GetPrivateProfileIntA("RenoDX.DLSS5", "HUDPosition", 0, g_iniPath);
        if (g_hudPosIndex < 0 || g_hudPosIndex > 3) g_hudPosIndex = 0;

        g_varsInitialized = true;
        char buf[256];
        sprintf_s(buf, sizeof(buf), 
            "[VR-DLSS5-HUD] Initialized: Enable=%d, Intensity=%.2f, Tone=%.2f, Preset=%d, Scale=%d, Pos=%d",
            g_masterEnable ? 1 : 0, g_nrIntensity, g_nrGlobalTone, g_nrPreset, g_hudScale, g_hudPosIndex);
        LogMsg(buf);
    }
}

static void CommitSettingsToDisk()
{
    char valStr[64];
    sprintf_s(valStr, sizeof(valStr), "%d", g_masterEnable ? 1 : 0);
    WritePrivateProfileStringA("RenoDX.DLSS5", "EnableHooks", valStr, g_iniPath);

    sprintf_s(valStr, sizeof(valStr), "%.2f", g_nrIntensity);
    WritePrivateProfileStringA("RenoDX.DLSS5", "NRIntensity", valStr, g_iniPath);

    sprintf_s(valStr, sizeof(valStr), "%.2f", g_nrGlobalTone);
    WritePrivateProfileStringA("RenoDX.DLSS5", "NRGlobalTone", valStr, g_iniPath);

    sprintf_s(valStr, sizeof(valStr), "%d", g_nrPreset);
    WritePrivateProfileStringA("RenoDX.DLSS5", "NRPreset", valStr, g_iniPath);

    sprintf_s(valStr, sizeof(valStr), "%d", g_hudScale);
    WritePrivateProfileStringA("RenoDX.DLSS5", "HUDScale", valStr, g_iniPath);

    sprintf_s(valStr, sizeof(valStr), "%d", g_hudPosIndex);
    WritePrivateProfileStringA("RenoDX.DLSS5", "HUDPosition", valStr, g_iniPath);

    char logBuf[256];
    sprintf_s(logBuf, sizeof(logBuf), 
        "[VR-DLSS5-HUD] 500ms Debounce Save committed: Enable=%d, Int=%.2f, Tone=%.2f, Preset=%d, Scale=%d -> %s",
        g_masterEnable ? 1 : 0, g_nrIntensity, g_nrGlobalTone, g_nrPreset, g_hudScale, g_iniPath);
    LogMsg(logBuf);
}

// ----------------------------------------------------------------------------
// Compact Software Rasterizer
// ----------------------------------------------------------------------------
static HUDColor s_hudPixels[HUD_HEIGHT][HUD_WIDTH];

static void HUD_Clear(HUDColor color)
{
    for (int y = 0; y < HUD_HEIGHT; y++) {
        for (int x = 0; x < HUD_WIDTH; x++) {
            s_hudPixels[y][x] = color;
        }
    }
}

static void HUD_FillRect(int x0, int y0, int w, int h, HUDColor color)
{
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > HUD_WIDTH) w = HUD_WIDTH - x0;
    if (y0 + h > HUD_HEIGHT) h = HUD_HEIGHT - y0;
    if (w <= 0 || h <= 0) return;

    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            s_hudPixels[y][x] = color;
        }
    }
}

static void HUD_DrawRect(int x0, int y0, int w, int h, HUDColor color)
{
    HUD_FillRect(x0, y0, w, 1, color);
    HUD_FillRect(x0, y0 + h - 1, w, 1, color);
    HUD_FillRect(x0, y0, 1, h, color);
    HUD_FillRect(x0 + w - 1, y0, 1, h, color);
}

static void HUD_DrawChar(int x, int y, char c, HUDColor color)
{
    if (c < 32 || c > 126) c = '?';
    int idx = c - 32;
    for (int r = 0; r < 14; r++) {
        int py = y + r;
        if (py < 0 || py >= HUD_HEIGHT) continue;
        uint8_t rowBits = g_font8x14[idx][r];
        for (int b = 0; b < 8; b++) {
            int px = x + b;
            if (px < 0 || px >= HUD_WIDTH) continue;
            if (rowBits & (0x80 >> b)) {
                s_hudPixels[py][px] = color;
            }
        }
    }
}

static void HUD_DrawText(int x, int y, const char* str, HUDColor color)
{
    while (*str) {
        HUD_DrawChar(x, y, *str, color);
        x += 8;
        str++;
    }
}

static void HUD_DrawSlider(int x, int y, int w, int h, float valNorm, HUDColor fillColor, HUDColor bgColor, HUDColor borderColor, HUDColor thumbColor)
{
    HUD_FillRect(x, y, w, h, bgColor);
    int fillW = (int)(w * valNorm);
    if (fillW > w) fillW = w;
    if (fillW < 0) fillW = 0;
    if (fillW > 0) {
        HUD_FillRect(x, y, fillW, h, fillColor);
    }
    HUD_DrawRect(x, y, w, h, borderColor);
    int thumbX = x + fillW;
    if (thumbX >= x + w) thumbX = x + w - 1;
    HUD_FillRect(thumbX - 1, y - 1, 3, h + 2, thumbColor);
}

static void RenderHUD(bool masterEnable, float intensity, float tone, int preset, int scaleMode, int activeRow, int posIdx, int liveHz)
{
    // Dark slate background
    HUD_Clear(MakeHUDColor(14, 18, 26, 245));
    // Cyan neon border
    HUD_DrawRect(0, 0, HUD_WIDTH, HUD_HEIGHT, MakeHUDColor(0, 180, 230, 255));
    HUD_DrawRect(1, 1, HUD_WIDTH - 2, HUD_HEIGHT - 2, MakeHUDColor(20, 40, 60, 200));

    // Title Header
    const char* posNames[] = { "BOT-C", "TOP-C", "TOP-R", "TOP-L" };
    char titleBuf[64];
    sprintf_s(titleBuf, sizeof(titleBuf), "DLSS 5 NEURAL RECON [%s]", posNames[posIdx & 3]);
    HUD_DrawText(10, 5, titleBuf, MakeHUDColor(0, 220, 255, 255));

    // Dynamic Live Hz status badge (72Hz, 80Hz, 90Hz, 120Hz)
    char badgeBuf[32];
    if (masterEnable) {
        sprintf_s(badgeBuf, sizeof(badgeBuf), "%dHz ON", liveHz);
        HUD_FillRect(HUD_WIDTH - 65, 4, 55, 14, MakeHUDColor(20, 120, 50, 255));
        HUD_DrawRect(HUD_WIDTH - 65, 4, 55, 14, MakeHUDColor(80, 255, 120, 255));
        HUD_DrawText(HUD_WIDTH - 57, 5, badgeBuf, MakeHUDColor(255, 255, 255, 255));
    } else {
        HUD_FillRect(HUD_WIDTH - 65, 4, 55, 14, MakeHUDColor(120, 30, 30, 255));
        HUD_DrawRect(HUD_WIDTH - 65, 4, 55, 14, MakeHUDColor(255, 80, 80, 255));
        HUD_DrawText(HUD_WIDTH - 57, 5, "BYPASS", MakeHUDColor(255, 255, 255, 255));
    }

    // Separator line
    HUD_FillRect(8, 19, HUD_WIDTH - 16, 1, MakeHUDColor(40, 70, 100, 255));

    // 5 Rows
    int rowY[5] = { 22, 42, 62, 82, 102 };
    for (int i = 0; i < 5; i++) {
        if (i == activeRow) {
            HUD_FillRect(6, rowY[i] - 1, HUD_WIDTH - 12, 17, MakeHUDColor(25, 45, 75, 255));
            HUD_DrawRect(6, rowY[i] - 1, HUD_WIDTH - 12, 17, MakeHUDColor(0, 180, 255, 200));
            HUD_DrawText(10, rowY[i] + 1, ">", MakeHUDColor(255, 255, 0, 255));
        } else {
            HUD_DrawText(10, rowY[i] + 1, " ", MakeHUDColor(100, 120, 140, 255));
        }
    }

    // ROW 0: Master Enable / Bypass
    HUD_DrawText(22, rowY[0] + 1, "Neural Engine :", activeRow == 0 ? MakeHUDColor(255, 255, 255) : MakeHUDColor(190, 200, 210));
    if (masterEnable) {
        HUD_FillRect(155, rowY[0] + 1, 62, 13, MakeHUDColor(15, 140, 60, 255));
        HUD_DrawRect(155, rowY[0] + 1, 62, 13, MakeHUDColor(60, 255, 120, 255));
        HUD_DrawText(160, rowY[0] + 1, "[ ACTIVE ]", MakeHUDColor(255, 255, 255));
    } else {
        HUD_FillRect(155, rowY[0] + 1, 62, 13, MakeHUDColor(140, 30, 30, 255));
        HUD_DrawRect(155, rowY[0] + 1, 62, 13, MakeHUDColor(255, 80, 80, 255));
        HUD_DrawText(160, rowY[0] + 1, "[ BYPASS ]", MakeHUDColor(255, 200, 200));
    }

    // ROW 1: Intensity Slider (0.0 to 5.0)
    char intBuf[32];
    sprintf_s(intBuf, sizeof(intBuf), "Intensity   : %4.2f", intensity);
    HUD_DrawText(22, rowY[1] + 1, intBuf, activeRow == 1 ? MakeHUDColor(255, 255, 255) : MakeHUDColor(190, 200, 210));
    float normInt = intensity / 5.0f;
    HUD_DrawSlider(175, rowY[1] + 3, 195, 9, normInt, MakeHUDColor(0, 160, 220), MakeHUDColor(20, 30, 45), MakeHUDColor(60, 90, 130), MakeHUDColor(0, 255, 255));

    // ROW 2: Sharpness / Tone Slider (0.0 to 2.0)
    char toneBuf[32];
    sprintf_s(toneBuf, sizeof(toneBuf), "Sharp / Tone: %4.2f", tone);
    HUD_DrawText(22, rowY[2] + 1, toneBuf, activeRow == 2 ? MakeHUDColor(255, 255, 255) : MakeHUDColor(190, 200, 210));
    float normTone = tone / 2.0f;
    HUD_DrawSlider(175, rowY[2] + 3, 195, 9, normTone, MakeHUDColor(0, 180, 160), MakeHUDColor(20, 30, 45), MakeHUDColor(60, 90, 130), MakeHUDColor(0, 255, 200));

    // ROW 3: AI Model Preset (0, 1, 2)
    const char* presetNames[3] = { "Preset 0 (DLSS-D RR)", "Preset 1 (Ultra Quality)", "Preset 2 (Performance)" };
    char preBuf[64];
    sprintf_s(preBuf, sizeof(preBuf), "AI Preset   : %s", presetNames[preset % 3]);
    HUD_DrawText(22, rowY[3] + 1, preBuf, activeRow == 3 ? MakeHUDColor(255, 255, 100) : MakeHUDColor(190, 200, 210));

    // ROW 4: UI Scale Mode (Quest 3 & Pimax)
    const char* scaleNames[3] = { "1.0x [Compact/Pimax]", "1.5x [Balanced/Quest3]", "2.0x [Comfort/Quest3]" };
    char scaleBuf[64];
    sprintf_s(scaleBuf, sizeof(scaleBuf), "UI Scale    : %s", scaleNames[scaleMode % 3]);
    HUD_DrawText(22, rowY[4] + 1, scaleBuf, activeRow == 4 ? MakeHUDColor(255, 255, 100) : MakeHUDColor(190, 200, 210));

    // Footer Help Bar
    HUD_FillRect(6, 122, HUD_WIDTH - 12, 1, MakeHUDColor(35, 60, 90, 255));
    HUD_DrawText(10, 126, "^v:Row <>:Adj A:Tgl Y:Pos R3/F7:Scale F6:Off", MakeHUDColor(120, 160, 190, 255));
}


// ----------------------------------------------------------------------------
// Format Encoders & Upload Buffer Management
// ----------------------------------------------------------------------------
static inline uint16_t FloatToHalf(float f)
{
    uint32_t x = *(uint32_t*)&f;
    uint32_t sign = (x >> 31) & 1;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;

    if (exp <= 0) return (uint16_t)(sign << 15);
    if (exp >= 31) return (uint16_t)((sign << 15) | 0x7C00);
    return (uint16_t)((sign << 15) | (exp << 10) | (mant >> 13));
}

static inline uint32_t FloatToR11(float f)
{
    if (f <= 0.0f) return 0;
    uint32_t x = *(uint32_t*)&f;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (exp <= 0) return 0;
    if (exp >= 31) return 0x7E0;
    return ((exp << 6) | (mant >> 17)) & 0x7FF;
}

static inline uint32_t FloatToR10(float f)
{
    if (f <= 0.0f) return 0;
    uint32_t x = *(uint32_t*)&f;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (exp <= 0) return 0;
    if (exp >= 31) return 0x3E0;
    return ((exp << 5) | (mant >> 18)) & 0x3FF;
}

static ID3D12Resource* g_pUploadBuffer = NULL;
static ID3D12Device* g_pDevice = NULL;
static void* g_pMappedData = NULL;

static bool EnsureUploadBuffer(ID3D12Device* pDev, UINT64 requiredSize)
{
    if (g_pUploadBuffer && g_pDevice == pDev) {
        return true;
    }
    if (g_pUploadBuffer) {
        g_pUploadBuffer->Unmap(0, NULL);
        g_pUploadBuffer->Release();
        g_pUploadBuffer = NULL;
        g_pMappedData = NULL;
    }
    g_pDevice = pDev;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = requiredSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = pDev->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL,
        IID_PPV_ARGS(&g_pUploadBuffer)
    );

    if (FAILED(hr) || !g_pUploadBuffer) {
        return false;
    }

    D3D12_RANGE readRange = { 0, 0 };
    hr = g_pUploadBuffer->Map(0, &readRange, &g_pMappedData);
    if (FAILED(hr) || !g_pMappedData) {
        return false;
    }

    LogMsg("[VR-DLSS5-HUD] Staging upload buffer armed successfully");
    return true;
}

static bool WriteHUDToMappedData(DXGI_FORMAT format, UINT rowPitch, int scaleMode)
{
    if (!g_pMappedData) return false;

    int scaleNum = 1, scaleDen = 1;
    if (scaleMode == 1) { scaleNum = 3; scaleDen = 2; }      // 1.5x (Balanced / Quest 3)
    else if (scaleMode == 2) { scaleNum = 2; scaleDen = 1; } // 2.0x (Comfort / Large)

    int curW = (HUD_WIDTH * scaleNum) / scaleDen;
    int curH = (HUD_HEIGHT * scaleNum) / scaleDen;

    for (int y = 0; y < curH; y++) {
        int srcY = (y * scaleDen) / scaleNum;
        if (srcY >= HUD_HEIGHT) srcY = HUD_HEIGHT - 1;

        uint8_t* pRow = (uint8_t*)g_pMappedData + y * rowPitch;

        if (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            for (int x = 0; x < curW; x++) {
                int srcX = (x * scaleDen) / scaleNum;
                if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
                HUDColor c = s_hudPixels[srcY][srcX];
                pRow[x * 4 + 0] = c.r;
                pRow[x * 4 + 1] = c.g;
                pRow[x * 4 + 2] = c.b;
                pRow[x * 4 + 3] = c.a;
            }
        }
        else if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            for (int x = 0; x < curW; x++) {
                int srcX = (x * scaleDen) / scaleNum;
                if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
                HUDColor c = s_hudPixels[srcY][srcX];
                pRow[x * 4 + 0] = c.b;
                pRow[x * 4 + 1] = c.g;
                pRow[x * 4 + 2] = c.r;
                pRow[x * 4 + 3] = c.a;
            }
        }
        else if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            uint16_t* pRow16 = (uint16_t*)pRow;
            for (int x = 0; x < curW; x++) {
                int srcX = (x * scaleDen) / scaleNum;
                if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
                HUDColor c = s_hudPixels[srcY][srcX];
                pRow16[x * 4 + 0] = FloatToHalf((float)c.r / 255.0f);
                pRow16[x * 4 + 1] = FloatToHalf((float)c.g / 255.0f);
                pRow16[x * 4 + 2] = FloatToHalf((float)c.b / 255.0f);
                pRow16[x * 4 + 3] = FloatToHalf((float)c.a / 255.0f);
            }
        }
        else if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
            uint32_t* pRow32 = (uint32_t*)pRow;
            for (int x = 0; x < curW; x++) {
                int srcX = (x * scaleDen) / scaleNum;
                if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
                HUDColor c = s_hudPixels[srcY][srcX];
                uint32_t r10 = (uint32_t)c.r * 1023 / 255;
                uint32_t g10 = (uint32_t)c.g * 1023 / 255;
                uint32_t b10 = (uint32_t)c.b * 1023 / 255;
                uint32_t a2 = (uint32_t)c.a * 3 / 255;
                pRow32[x] = (r10) | (g10 << 10) | (b10 << 20) | (a2 << 30);
            }
        }
        else if (format == DXGI_FORMAT_R11G11B10_FLOAT) {
            uint32_t* pRow32 = (uint32_t*)pRow;
            for (int x = 0; x < curW; x++) {
                int srcX = (x * scaleDen) / scaleNum;
                if (srcX >= HUD_WIDTH) srcX = HUD_WIDTH - 1;
                HUDColor c = s_hudPixels[srcY][srcX];
                uint32_t r11 = FloatToR11((float)c.r / 255.0f);
                uint32_t g11 = FloatToR11((float)c.g / 255.0f);
                uint32_t b10 = FloatToR10((float)c.b / 255.0f);
                pRow32[x] = (r11) | (g11 << 11) | (b10 << 22);
            }
        }
        else {
            return false;
        }
    }
    return true;
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

    // 1. Keyboard F6 & Gamepad Select + L3 Toggle
    bool toggleRequested = false;

    UpdateKey(VK_F6, now);
    if (g_keys[VK_F6].justPressed) {
        toggleRequested = true;
    }

    XINPUT_STATE xstate;
    ZeroMemory(&xstate, sizeof(XINPUT_STATE));
    bool padConnected = false;
    for (DWORD i = 0; i < 4; i++) {
        if (XInputGetState(i, &xstate) == ERROR_SUCCESS) {
            padConnected = true;
            break;
        }
    }

    WORD buttons = padConnected ? xstate.Gamepad.wButtons : 0;
    bool comboDown = ((buttons & 0x0060) == 0x0060); // BACK (0x20) | LEFT_THUMB (0x40)
    static bool s_prevCombo = false;
    if (comboDown && !s_prevCombo) {
        toggleRequested = true;
    }
    s_prevCombo = comboDown;

    if (toggleRequested) {
        g_hudVisible = !g_hudVisible;
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Overlay toggled: %s", g_hudVisible ? "OPEN" : "CLOSED");
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

    // Close HUD
    static bool s_prevPadB = false;
    bool padB = (buttons & XINPUT_GAMEPAD_B) != 0;
    if (g_keys[VK_ESCAPE].justPressed || (padB && !s_prevPadB)) {
        g_hudVisible = false;
        LogMsg("[VR-DLSS5-HUD] Overlay closed");
        return;
    }
    s_prevPadB = padB;

    // Cycle Position: Tab or Gamepad Y
    static bool s_prevPadY = false;
    bool padY = (buttons & XINPUT_GAMEPAD_Y) != 0;
    if (g_keys[VK_TAB].justPressed || (padY && !s_prevPadY)) {
        g_hudPosIndex = (g_hudPosIndex + 1) % 4;
        static const char* posNames[] = { "Bottom-Center", "Top-Center", "Top-Right", "Top-Left" };
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Position changed to: %s", posNames[g_hudPosIndex]);
        LogMsg(buf);
        g_hasPendingSave = true;
        g_lastChangeTick = now;
    }
    s_prevPadY = padY;

    // Direct Scale Toggle: F7 (Keyboard) or R3 (Right Stick Click)
    static bool s_prevPadR3 = false;
    bool padR3 = (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    if (g_keys[VK_F7].justPressed || (padR3 && !s_prevPadR3)) {
        g_hudScale = (g_hudScale + 1) % 3;
        static const char* scaleNames[] = { "1.0x (Compact)", "1.5x (Balanced Q3)", "2.0x (Comfort Q3)" };
        char buf[128];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-HUD] Scale toggled to: %s", scaleNames[g_hudScale]);
        LogMsg(buf);
        g_hasPendingSave = true;
        g_lastChangeTick = now;
    }
    s_prevPadR3 = padR3;

    // Navigate Rows (5 rows: 0 to 4)
    static bool s_prevPadUp = false;
    static bool s_prevPadDown = false;
    bool padUp = (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    bool padDown = (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;

    if (g_keys[VK_UP].justPressed || (padUp && !s_prevPadUp)) {
        g_activeRow = (g_activeRow + 4) % 5;
    }
    if (g_keys[VK_DOWN].justPressed || (padDown && !s_prevPadDown)) {
        g_activeRow = (g_activeRow + 1) % 5;
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
    bool padLeft = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    bool padRight = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

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
    bool padA = (buttons & XINPUT_GAMEPAD_A) != 0;
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
    else if (g_activeRow == 4) { // UI Scale (1.0x, 1.5x, 2.0x)
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
        g_hasPendingSave = true;
        g_lastChangeTick = now;

        if (g_renodxBase) {
            *(uint8_t*)(g_renodxBase + 0x192F68) = g_masterEnable ? 1 : 0;
            *(float*)(g_renodxBase + 0x19364C) = g_nrIntensity;
            *(float*)(g_renodxBase + 0x193650) = g_nrGlobalTone;
            *(int32_t*)(g_renodxBase + 0x196B98) = g_nrPreset;
            *(int32_t*)(g_renodxBase + 0x196C2C) = g_nrPreset;
            *(uint8_t*)(g_renodxBase + 0x1935E8) = 1; // set dirty flag
        }
    }
}

// ----------------------------------------------------------------------------
// D3D12 In-Game VR & Desktop Overlay Blitter (Pimax & Quest 3 Sweet-Spot)
// ----------------------------------------------------------------------------
static void BlitHUDToOutput(ID3D12GraphicsCommandList* pCmdList, void* pParameters)
{
    if (!pCmdList || !pParameters || !g_hudVisible) return;

    __try {
        void** vtable = *(void***)pParameters;
        if (!vtable || !vtable[9]) return;

        typedef int (__fastcall *PFN_NGX_GetD3D12Resource)(void* thisPtr, const char* name, ID3D12Resource** ppOut);
        PFN_NGX_GetD3D12Resource getRes = (PFN_NGX_GetD3D12Resource)vtable[9];

        ID3D12Resource* pOutput = NULL;
        int ret = getRes(pParameters, "Output", &pOutput);
        if (ret < 0 || (ret & 0xFFF00000) == 0xBAD00000 || !pOutput) return;

        D3D12_RESOURCE_DESC desc = pOutput->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) return;

        ID3D12Device* pDevice = NULL;
        HRESULT hr = pOutput->GetDevice(__uuidof(ID3D12Device), (void**)&pDevice);
        if (FAILED(hr) || !pDevice) return;

        // Determine current width and height based on scale
        int scaleNum = 1, scaleDen = 1;
        if (g_hudScale == 1) { scaleNum = 3; scaleDen = 2; }      // 1.5x (Balanced / Quest 3 default)
        else if (g_hudScale == 2) { scaleNum = 2; scaleDen = 1; } // 2.0x (Comfort / Large Quest 3)

        UINT curW = (HUD_WIDTH * scaleNum) / scaleDen;
        UINT curH = (HUD_HEIGHT * scaleNum) / scaleDen;

        UINT bytesPerPixel = 4;
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM) {
            bytesPerPixel = 8;
        }

        UINT rowPitch = (curW * bytesPerPixel + 255) & ~255;
        UINT requiredSize = 2 * 1024 * 1024; // 2 MB buffer covers up to 2.0x scale in FP16

        if (!EnsureUploadBuffer(pDevice, requiredSize)) {
            pDevice->Release();
            return;
        }

        RenderHUD(g_masterEnable, g_nrIntensity, g_nrGlobalTone, g_nrPreset, g_hudScale, g_activeRow, g_hudPosIndex, g_liveHz);

        if (!WriteHUDToMappedData(desc.Format, rowPitch, g_hudScale)) {
            pDevice->Release();
            return;
        }

        UINT texW = (UINT)desc.Width;
        UINT texH = desc.Height;
        UINT dstX = 0;
        UINT dstY = 0;

        // Proportional safety margins for VR lenses (Quest 3 Pancake & Pimax Fresnel):
        // 10% horizontal margin and 12% vertical margin keep HUD perfectly within
        // the circular optical sweet spot and clear of the nasal cutout / lens mask.
        UINT marginX = (UINT)(texW * 0.10f);
        if (marginX < 40) marginX = 40;

        UINT marginY = (UINT)(texH * 0.12f);
        if (marginY < 60) marginY = 60;

        switch (g_hudPosIndex % 4) {
        case 0: // Bottom-Center (Default)
            dstX = (texW > curW) ? (texW - curW) / 2 : 0;
            dstY = (texH > (curH + marginY)) ? (texH - curH - marginY) : 0;
            break;
        case 1: // Top-Center
            dstX = (texW > curW) ? (texW - curW) / 2 : 0;
            dstY = (texH > (curH + marginY)) ? marginY : 0;
            break;
        case 2: // Top-Right (45 deg)
            dstX = (texW > (curW + marginX)) ? (texW - curW - marginX) : 0;
            dstY = (texH > (curH + marginY)) ? marginY : 0;
            break;
        case 3: // Top-Left (45 deg)
            dstX = (texW > (curW + marginX)) ? marginX : 0;
            dstY = (texH > (curH + marginY)) ? marginY : 0;
            break;
        }

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[0].Transition.pResource = pOutput;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        pCmdList->ResourceBarrier(1, &barriers[0]);

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = pOutput;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = g_pUploadBuffer;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = desc.Format;
        srcLoc.PlacedFootprint.Footprint.Width = curW;
        srcLoc.PlacedFootprint.Footprint.Height = curH;
        srcLoc.PlacedFootprint.Footprint.Depth = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_BOX box = { 0, 0, 0, curW, curH, 1 };
        pCmdList->CopyTextureRegion(&dstLoc, dstX, dstY, 0, &srcLoc, &box);

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[1].Transition.pResource = pOutput;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pCmdList->ResourceBarrier(1, &barriers[1]);

        pDevice->Release();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            LogMsg("[VR-DLSS5-HUD] EXCEPTION safely caught during HUD blit");
        }
    }
}

extern "C" {

int WINAPI Proxy_NVSDK_NGX_D3D12_EvaluateFeature(void* pCmdList, void* pHandle, void* pParameters, void* pCallback)
{
    InitProxy();

    g_evalFrameCounter++;

    // 1. Toujours exécuter l'évaluation DLSS native de LukeRoss (RealVR64)
    int result = 0;
    if (g_pfnNGXEvaluateFeature)
    {
        result = g_pfnNGXEvaluateFeature(pCmdList, pHandle, pParameters, pCallback);
    }

    // 2. Synchronisation de la mémoire et lecture paresseuse des variables
    InitVariablesFromAddonOrIni();

    // 3. Calcul dynamique du taux de rafraîchissement VR (72Hz, 80Hz, 90Hz, 120Hz)
    uint64_t now = GetTickCount64();
    static uint64_t s_lastFrameTick = 0;
    static float s_smoothedFps = 72.0f;
    if (s_lastFrameTick > 0)
    {
        uint64_t delta = now - s_lastFrameTick;
        if (delta > 0 && delta < 500)
        {
            float instantFps = 1000.0f / (float)delta;
            s_smoothedFps = s_smoothedFps * 0.95f + instantFps * 0.05f;
        }
    }
    s_lastFrameTick = now;

    int displayHz = (int)(s_smoothedFps + 0.5f);
    if (abs(displayHz - 72) <= 3) displayHz = 72;
    else if (abs(displayHz - 80) <= 3) displayHz = 80;
    else if (abs(displayHz - 90) <= 3) displayHz = 90;
    else if (abs(displayHz - 120) <= 3) displayHz = 120;
    g_liveHz = displayHz;

    // 4. Polling utilisateur (Clavier + Manette)
    static uint64_t s_lastInputTick = 0;
    if (now - s_lastInputTick >= 8)
    {
        s_lastInputTick = now;
        PollInput();
    }

    // 5. Persistence différée (500 ms debounce sans micro-stutter)
    if (g_hasPendingSave && (now - g_lastChangeTick >= 500))
    {
        g_hasPendingSave = false;
        CommitSettingsToDisk();
    }

    // 6. Rendu de l'overlay dans le casque VR et sur le miroir bureau
    if (g_hudVisible && pCmdList)
    {
        BlitHUDToOutput((ID3D12GraphicsCommandList*)pCmdList, pParameters);
    }

    // 7. Télémétrie de synchronisation continue
    if ((g_evalFrameCounter % 200) == 1 || g_evalFrameCounter <= 20)
    {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-Telemetry] Continuous VR frame #%llu: gameHandle=%p, eval_ret=0x%08X", 
            g_evalFrameCounter, pHandle, result);
        LogMsg(buf);
    }

    return result;
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

}

