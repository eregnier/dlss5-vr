#include <windows.h>
#include <stdio.h>

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

    // 1. Charger RealVR64.dll (LukeRoss VR mod) d'abord
    g_hRealVR = LoadLibraryA("RealVR64.dll");
    if (g_hRealVR)
    {
        LogMsg("[Proxy] Successfully loaded RealVR64.dll");
        g_pfnCreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g_hRealVR, "CreateDXGIFactory");
        g_pfnCreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g_hRealVR, "CreateDXGIFactory1");
        g_pfnCreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g_hRealVR, "CreateDXGIFactory2");
        g_pfnDXGIDeclareAdapterRemovalSupport = (PFN_DXGIDeclareAdapterRemovalSupport)GetProcAddress(g_hRealVR, "DXGIDeclareAdapterRemovalSupport");
        g_pfnDXGIGetDebugInterface1 = (PFN_DXGIGetDebugInterface1)GetProcAddress(g_hRealVR, "DXGIGetDebugInterface1");

        // Detour inconditionnel de NVSDK_NGX_D3D12_EvaluateFeature dans RealVR64
        void* pRealVREval = (void*)GetProcAddress(g_hRealVR, "NVSDK_NGX_D3D12_EvaluateFeature");
        if (pRealVREval)
        {
            LogMsg("[Proxy] Found RealVR64:NVSDK_NGX_D3D12_EvaluateFeature, installing permanent detour...");
            DWORD oldProtect;
            if (VirtualProtect(pRealVREval, 14, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                // Sauvegarder les 14 octets originaux pour le trampoline
                static BYTE s_trampoline[32];
                memcpy(s_trampoline, pRealVREval, 14);
                
                // Saut du trampoline vers pRealVREval + 14
                s_trampoline[14] = 0xFF;
                s_trampoline[15] = 0x25;
                *(DWORD*)(&s_trampoline[16]) = 0;
                *(ULONG_PTR*)(&s_trampoline[20]) = ((ULONG_PTR)pRealVREval) + 14;
                DWORD trampProtect;
                VirtualProtect(s_trampoline, sizeof(s_trampoline), PAGE_EXECUTE_READWRITE, &trampProtect);
                g_pfnNGXEvaluateFeature = (PFN_NVSDK_NGX_D3D12_EvaluateFeature)(void*)s_trampoline;

                // Installer jmp qword ptr [rip+0] vers Proxy_NVSDK_NGX_D3D12_EvaluateFeature
                BYTE patch[14];
                patch[0] = 0xFF;
                patch[1] = 0x25;
                *(DWORD*)(&patch[2]) = 0;
                extern int WINAPI Proxy_NVSDK_NGX_D3D12_EvaluateFeature(void*, void*, void*, void*);
                *(ULONG_PTR*)(&patch[6]) = (ULONG_PTR)Proxy_NVSDK_NGX_D3D12_EvaluateFeature;
                memcpy(pRealVREval, patch, 14);

                VirtualProtect(pRealVREval, 14, oldProtect, &oldProtect);
                LogMsg("[Proxy] SUCCESS: Permanent direct detour installed on RealVR64:EvaluateFeature!");
            }
            else
            {
                LogMsg("[Proxy] WARNING: VirtualProtect failed on RealVR64:EvaluateFeature");
            }
        }
    }
    else
    {
        LogMsg("[Proxy] WARNING: RealVR64.dll not found, falling back to system dxgi.dll");
    }


    // 2. Charger ReShade 6.8 (DLSS 5 host) ensuite
    g_hReShade = LoadLibraryA("ReShade64_dlss5.dll");
    if (g_hReShade)
    {
        LogMsg("[Proxy] Successfully loaded ReShade64_dlss5.dll");
    }
    else
    {
        LogMsg("[Proxy] WARNING: Could not load ReShade64_dlss5.dll");
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

static HMODULE g_hRenoDX = NULL;
static PFN_NVSDK_NGX_D3D12_EvaluateFeature g_pfnRenoDX_Evaluate = NULL;

static thread_local bool g_inRenoEvaluation = false;

int WINAPI Proxy_NVSDK_NGX_D3D12_EvaluateFeature(void* pCmdList, void* pHandle, void* pParameters, void* pCallback)
{
    InitProxy();

    // 0. Protection anti-récursion essentielle :
    // Quand RenoDX exécute son évaluation neuronale, il appelle en interne son pointeur
    // de fonction original (qui pointe vers notre hook proxy). Sans ce verrou, récursion infinie et crash immédiat.
    if (g_inRenoEvaluation)
    {
        if (g_pfnNGXEvaluateFeature)
            return g_pfnNGXEvaluateFeature(pCmdList, pHandle, pParameters, pCallback);
        return 0;
    }

    // 1. Liaison dynamique au moteur neuronal RenoDX DLSS 5 (RVA 0x376C0)
    if (!g_pfnRenoDX_Evaluate)
    {
        if (!g_hRenoDX) g_hRenoDX = GetModuleHandleA("renodx-dlss5.addon64");
        if (g_hRenoDX)
        {
            g_pfnRenoDX_Evaluate = (PFN_NVSDK_NGX_D3D12_EvaluateFeature)((uintptr_t)g_hRenoDX + 0x376C0);
            LogMsg("[VR-DLSS5] SUCCESS: Bound directly to RenoDX DLSS 5 Neural Reconstruction engine (RVA 0x376C0)!");
        }
    }

    int result = 0;
    int renoResult = -1;

    if (g_pfnRenoDX_Evaluate && pCmdList && pParameters)
    {
        g_inRenoEvaluation = true;
        // Dispatch continu vers RenoDX DLSS 5 (Feature 18 avec signed snippet et guides)
        renoResult = g_pfnRenoDX_Evaluate(pCmdList, pHandle, pParameters, pCallback);
        g_inRenoEvaluation = false;
        result = renoResult;
    }
    else if (g_pfnNGXEvaluateFeature)
    {
        // Repli standard si RenoDX n'est pas encore prêt
        result = g_pfnNGXEvaluateFeature(pCmdList, pHandle, pParameters, pCallback);
    }

    g_evalFrameCounter++;
    if ((g_evalFrameCounter % 100) == 1 || g_evalFrameCounter <= 10)
    {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "[VR-DLSS5-Telemetry] Frame #%llu: gameHandle=%p, renoEval=0x%08X (RenoDX Engine=%s)", 
            g_evalFrameCounter, pHandle, renoResult, g_pfnRenoDX_Evaluate ? "ACTIVE" : "PENDING");
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

