#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <functional>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wininet.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Version Constants
#define CURRENT_VERSION_STR L"1.0.0"
#define UPDATE_CHECK_URL    L"https://raw.githubusercontent.com/eregnier/dlss5-vr/main/VERSION"
#define GITHUB_RELEASES_URL L"https://github.com/eregnier/dlss5-vr/releases"

// Control IDs
#define IDC_EDIT_PATH       1001
#define IDC_BTN_BROWSE      1002
#define IDC_BTN_INSTALL     1003
#define IDC_BTN_RESTORE     1004
#define IDC_BTN_REFRESH     1005
#define IDC_STATIC_STATUS   1006
#define IDC_EDIT_LOG        1007
#define IDC_PROGRESS_BAR    1008
#define IDC_BTN_UPDATE      1009

HWND g_hMainWnd = NULL;
HWND g_hEditPath = NULL;
HWND g_hBtnBrowse = NULL;
HWND g_hBtnInstall = NULL;
HWND g_hBtnRestore = NULL;
HWND g_hBtnRefresh = NULL;
HWND g_hBtnUpdate = NULL;
HWND g_hStaticStatus = NULL;
HWND g_hProgressBar = NULL;
HWND g_hEditLog = NULL;
HFONT g_hFontTitle = NULL;
HFONT g_hFontNormal = NULL;
HFONT g_hFontMono = NULL;

std::wstring g_selectedExe = L"";
std::wstring g_targetDir = L"";

bool g_hasLukeRoss = false;
bool g_hasRealVR64 = false;
bool g_hasOurProxy = false;
bool g_hasDLSS = false;
std::wstring g_dlssVersionStr = L"";
bool g_isGameRunning = false;
std::wstring g_runningProcessName = L"";

void ProcessWindowMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void AppendLog(const std::wstring& text)
{
    if (!g_hEditLog) return;
    int len = GetWindowTextLengthW(g_hEditLog);
    SendMessageW(g_hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hEditLog, EM_REPLACESEL, 0, (LPARAM)text.c_str());
    SendMessageW(g_hEditLog, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
    SendMessageW(g_hEditLog, EM_SCROLLCARET, 0, 0);

    ProcessWindowMessages();
}

void SetProgressVisible(bool visible)
{
    if (g_hProgressBar) {
        ShowWindow(g_hProgressBar, visible ? SW_SHOW : SW_HIDE);
        if (visible) {
            SendMessageW(g_hProgressBar, PBM_SETRANGE32, 0, 100);
            SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
        }
    }
}

void SetProgressPercent(int pct)
{
    if (g_hProgressBar) {
        SendMessageW(g_hProgressBar, PBM_SETPOS, (WPARAM)pct, 0);
        ProcessWindowMessages();
    }
}

bool FileContainsBytes(const std::wstring& path, const char* pattern, size_t patternLen)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    DWORD toRead = (fileSize > 2 * 1024 * 1024) ? (2 * 1024 * 1024) : fileSize;
    std::vector<char> buffer(toRead);
    DWORD read = 0;
    bool found = false;

    if (ReadFile(hFile, buffer.data(), toRead, &read, NULL)) {
        for (DWORD i = 0; i + patternLen <= read; i++) {
            if (memcmp(&buffer[i], pattern, patternLen) == 0) {
                found = true;
                break;
            }
        }
    }
    CloseHandle(hFile);
    return found;
}

bool CheckProcessRunning(const std::wstring& exeName)
{
    if (exeName.empty()) return false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool running = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return running;
}

bool KillRunningGame(const std::wstring& exeName)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool killed = false;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                    killed = true;
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    Sleep(1000);
    return killed;
}

std::wstring FetchRemoteText(const std::wstring& url)
{
    HINTERNET hInternet = InternetOpenW(L"VR-DLSS5-Installer/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return L"";

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return L"";
    }

    std::string result = "";
    char buffer[1024];
    DWORD bytesRead = 0;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    size_t start = result.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return L"";
    size_t end = result.find_last_not_of(" \t\r\n");
    std::string trimmed = result.substr(start, end - start + 1);

    return std::wstring(trimmed.begin(), trimmed.end());
}

bool ParseSemVer(const std::wstring& ver, int& major, int& minor, int& patch)
{
    major = minor = patch = 0;
    const wchar_t* p = ver.c_str();
    if (*p == L'v' || *p == L'V') p++;
    if (swscanf_s(p, L"%d.%d.%d", &major, &minor, &patch) >= 1) {
        return true;
    }
    return false;
}

int CompareSemVer(const std::wstring& v1, const std::wstring& v2)
{
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    ParseSemVer(v1, maj1, min1, pat1);
    ParseSemVer(v2, maj2, min2, pat2);

    if (maj1 != maj2) return (maj1 > maj2) ? 1 : -1;
    if (min1 != min2) return (min1 > min2) ? 1 : -1;
    if (pat1 != pat2) return (pat1 > pat2) ? 1 : -1;
    return 0;
}

void DoCheckUpdate()
{
    AppendLog(L"----------------------------------------------------------------------");
    AppendLog(L"[UPDATE] Checking for updates online...");
    AppendLog(std::wstring(L"[UPDATE] Current version: v") + CURRENT_VERSION_STR);
    AppendLog(std::wstring(L"[UPDATE] Checking: ") + UPDATE_CHECK_URL);

    std::wstring remoteVer = FetchRemoteText(UPDATE_CHECK_URL);
    if (remoteVer.empty()) {
        AppendLog(L"[UPDATE] Unable to reach update server or repository not yet published on GitHub.");
        MessageBoxW(g_hMainWnd,
            L"Could not check for updates online.\n(The GitHub repository https://github.com/eregnier/dlss5-vr might not be published yet or you are offline).",
            L"Update Check", MB_OK | MB_ICONINFORMATION);
        return;
    }

    AppendLog(L"[UPDATE] Latest remote version: v" + remoteVer);

    if (CompareSemVer(remoteVer, CURRENT_VERSION_STR) > 0) {
        AppendLog(L"[UPDATE] An update is available: v" + remoteVer);
        std::wstring msg = L"A new version of DLSS 5 <> VR is available!\n\n"
                           L"Current version : v" + std::wstring(CURRENT_VERSION_STR) + L"\n"
                           L"Latest version  : v" + remoteVer + L"\n\n"
                           L"Would you like to open the GitHub Releases page to download it?";

        int res = MessageBoxW(g_hMainWnd, msg.c_str(), L"New Version Available", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            AppendLog(L"[UPDATE] Opening GitHub Releases page in browser: " + std::wstring(GITHUB_RELEASES_URL));
            ShellExecuteW(NULL, L"open", GITHUB_RELEASES_URL, NULL, NULL, SW_SHOWNORMAL);
        } else {
            AppendLog(L"[UPDATE] User declined opening release page.");
        }
    } else {
        AppendLog(L"[UPDATE] You are running the latest version (v" + std::wstring(CURRENT_VERSION_STR) + L").");
        MessageBoxW(g_hMainWnd,
            (L"You are using the latest version of DLSS 5 <> VR (v" + std::wstring(CURRENT_VERSION_STR) + L").").c_str(),
            L"Up to Date", MB_OK | MB_ICONINFORMATION);
    }
}

bool DownloadHttpFile(const std::wstring& url, const std::wstring& destFile, const std::wstring& label)
{
    AppendLog(L"[DOWNLOAD] Initiating download: " + label);
    SetProgressVisible(true);
    SetProgressPercent(0);

    HINTERNET hInternet = InternetOpenW(L"VR-DLSS5-Installer/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        AppendLog(L"[ERROR] Failed to open Internet handle.");
        SetProgressVisible(false);
        return false;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        AppendLog(L"[ERROR] Failed to connect to URL: " + url);
        SetProgressVisible(false);
        return false;
    }

    DWORD contentLength = 0;
    DWORD bufferSize = sizeof(contentLength);
    DWORD index = 0;
    HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &bufferSize, &index);

    HANDLE hFile = CreateFileW(destFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        AppendLog(L"[ERROR] Failed to create destination file: " + destFile);
        SetProgressVisible(false);
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    DWORD bytesRead = 0;
    DWORD totalDownloaded = 0;
    DWORD lastReportPct = 0;
    DWORD lastReportTick = GetTickCount();

    while (InternetReadFile(hUrl, buffer.data(), (DWORD)buffer.size(), &bytesRead) && bytesRead > 0) {
        DWORD written = 0;
        WriteFile(hFile, buffer.data(), bytesRead, &written, NULL);
        totalDownloaded += bytesRead;

        DWORD now = GetTickCount();
        if (contentLength > 0) {
            DWORD pct = (DWORD)(((__int64)totalDownloaded * 100) / contentLength);
            SetProgressPercent((int)pct);
            if (pct >= lastReportPct + 5 || (now - lastReportTick) > 1000) {
                lastReportPct = pct;
                lastReportTick = now;
                wchar_t logBuf[256];
                swprintf_s(logBuf, L"[DOWNLOAD] %s: %lu%% (%.1f MB / %.1f MB)",
                    label.c_str(), pct, (float)totalDownloaded / (1024.0f * 1024.0f), (float)contentLength / (1024.0f * 1024.0f));
                AppendLog(logBuf);
            }
        } else {
            if ((now - lastReportTick) > 1500) {
                lastReportTick = now;
                wchar_t logBuf[256];
                swprintf_s(logBuf, L"[DOWNLOAD] %s: %.1f MB downloaded...", label.c_str(), (float)totalDownloaded / (1024.0f * 1024.0f));
                AppendLog(logBuf);
            }
        }
    }

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    SetProgressPercent(100);
    AppendLog(L"[DOWNLOAD] Completed successfully: " + label);
    SetProgressVisible(false);
    return true;
}

bool ExtractZip(const std::wstring& zipPath, const std::wstring& outDir)
{
    AppendLog(std::wstring(L"[EXTRACT] Extracting ") + PathFindFileNameW(zipPath.c_str()) + L" via system tar...");
    wchar_t cmd[1024];
    swprintf_s(cmd, L"tar.exe -xf \"%s\" -C \"%s\"", zipPath.c_str(), outDir.c_str());

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };

    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (exitCode == 0);
    }
    return false;
}

std::wstring GetCacheDirectory()
{
    wchar_t localApp[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH)) {
        wchar_t cachePath[MAX_PATH];
        PathCombineW(cachePath, localApp, L"vr-dlss5-patch\\cache");
        CreateDirectoryW(cachePath, NULL);
        return std::wstring(cachePath);
    }
    return L".";
}

std::wstring FindOrDownloadComponent(const std::wstring& fileName)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    std::vector<std::wstring> searchBases = {
        exePath,
        std::wstring(exePath) + L"\\..",
        std::wstring(exePath) + L"\\proxy",
        std::wstring(exePath) + L"\\..\\proxy",
        std::wstring(exePath) + L"\\deps",
        std::wstring(exePath) + L"\\..\\deps",
        GetCacheDirectory()
    };

    for (const auto& base : searchBases) {
        wchar_t full[MAX_PATH];
        PathCombineW(full, base.c_str(), fileName.c_str());
        if (PathFileExistsW(full)) {
            return std::wstring(full);
        }
    }

    std::wstring cacheDir = GetCacheDirectory();

    // Auto-download nvngx_dlssnr.dll (~160 MB ShortFuse model) from RankFTW rhi-repo
    if (fileName == L"nvngx_dlssnr.dll") {
        wchar_t cachedModel[MAX_PATH];
        PathCombineW(cachedModel, cacheDir.c_str(), L"nvngx_dlssnr.dll");
        if (PathFileExistsW(cachedModel)) return std::wstring(cachedModel);

        wchar_t zipDest[MAX_PATH];
        PathCombineW(zipDest, cacheDir.c_str(), L"nvngx_dlssnr_310.8.SF-v2.zip");

        std::wstring url = L"https://github.com/RankFTW/rhi-repo/releases/download/dlssnr-310.8.SF-v2/nvngx_dlssnr_310.8.SF-v2.zip";
        if (DownloadHttpFile(url, zipDest, L"DLSS 5 Neural Model (nvngx_dlssnr.dll, ~160 MB)")) {
            ExtractZip(zipDest, cacheDir);
            DeleteFileW(zipDest);
            if (PathFileExistsW(cachedModel)) return std::wstring(cachedModel);
        }
    }

    // Auto-download renodx-dlss5.addon64 if missing
    if (fileName == L"renodx-dlss5.addon64") {
        wchar_t cachedAddon[MAX_PATH];
        PathCombineW(cachedAddon, cacheDir.c_str(), L"renodx-dlss5.addon64");
        if (PathFileExistsW(cachedAddon)) return std::wstring(cachedAddon);

        wchar_t zipDest[MAX_PATH];
        PathCombineW(zipDest, cacheDir.c_str(), L"renodx-dlss5_4.70.zip");

        std::wstring url = L"https://github.com/RankFTW/rhi-repo/releases/download/renodx-dlss5-4.70/renodx-dlss5_4.70.zip";
        if (DownloadHttpFile(url, zipDest, L"RenoDX DLSS 5 Add-on")) {
            ExtractZip(zipDest, cacheDir);
            DeleteFileW(zipDest);
            if (PathFileExistsW(cachedAddon)) return std::wstring(cachedAddon);
        }
    }

    return L"";
}

void InspectTarget()
{
    g_hasLukeRoss = false;
    g_hasRealVR64 = false;
    g_hasOurProxy = false;
    g_hasDLSS = false;
    g_dlssVersionStr = L"";
    g_isGameRunning = false;
    g_runningProcessName = L"";

    if (g_selectedExe.empty() || !PathFileExistsW(g_selectedExe.c_str())) {
        SetWindowTextW(g_hStaticStatus, L"Status: Please select a game executable (*.exe) to analyze.");
        EnableWindow(g_hBtnInstall, FALSE);
        EnableWindow(g_hBtnRestore, FALSE);
        return;
    }

    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, g_selectedExe.c_str());
    PathRemoveFileSpecW(dir);
    g_targetDir = dir;

    wchar_t exeName[MAX_PATH];
    wcscpy_s(exeName, PathFindFileNameW(g_selectedExe.c_str()));
    g_runningProcessName = exeName;
    g_isGameRunning = CheckProcessRunning(exeName);

    // Unreal Engine deep-check: check if game has Binaries/Win64
    wchar_t ueCheck[MAX_PATH];
    PathCombineW(ueCheck, dir, L"Binaries\\Win64");
    if (PathFileExistsW(ueCheck)) {
        wchar_t rvrCheck[MAX_PATH];
        PathCombineW(rvrCheck, ueCheck, L"RealVR.ini");
        if (PathFileExistsW(rvrCheck)) {
            g_targetDir = ueCheck;
            AppendLog(L"[INFO] Unreal Engine detected: Automatically switching target dir to Binaries\\Win64");
        }
    }

    // 1. Check LukeRoss VR mod
    wchar_t realVR64Path[MAX_PATH];
    PathCombineW(realVR64Path, g_targetDir.c_str(), L"RealVR64.dll");
    if (PathFileExistsW(realVR64Path)) {
        g_hasRealVR64 = true;
        g_hasLukeRoss = true;
    }

    wchar_t realIniPath[MAX_PATH];
    PathCombineW(realIniPath, g_targetDir.c_str(), L"RealVR.ini");
    if (PathFileExistsW(realIniPath)) {
        g_hasLukeRoss = true;
    }

    wchar_t dxgiPath[MAX_PATH];
    PathCombineW(dxgiPath, g_targetDir.c_str(), L"dxgi.dll");
    if (PathFileExistsW(dxgiPath)) {
        const char ourSig[] = "DLSS 5 <> VR";
        const char lrSig1[] = "RealVR64";
        const char lrSig2[] = "g_rvrShared";
        const char lrSig3[] = "LukeRoss";

        if (FileContainsBytes(dxgiPath, ourSig, strlen(ourSig))) {
            g_hasOurProxy = true;
            g_hasLukeRoss = true;
        } else if (FileContainsBytes(dxgiPath, lrSig1, strlen(lrSig1)) ||
                   FileContainsBytes(dxgiPath, lrSig2, strlen(lrSig2)) ||
                   FileContainsBytes(dxgiPath, lrSig3, strlen(lrSig3))) {
            g_hasLukeRoss = true;
        }
    }

    // 2. Check Native DLSS
    wchar_t dlssPath[MAX_PATH];
    PathCombineW(dlssPath, g_targetDir.c_str(), L"nvngx_dlss.dll");
    if (PathFileExistsW(dlssPath)) {
        g_hasDLSS = true;
        g_dlssVersionStr = L"nvngx_dlss.dll";
    } else {
        PathCombineW(dlssPath, g_targetDir.c_str(), L"sl.dlss.dll");
        if (PathFileExistsW(dlssPath)) {
            g_hasDLSS = true;
            g_dlssVersionStr = L"Streamline (sl.dlss.dll)";
        }
    }

    // Update Status string
    std::wstring statusText = L"Game Path : " + g_selectedExe + L"\r\n";
    statusText += L"Target Dir: " + g_targetDir + L"\r\n";

    if (g_hasOurProxy) {
        statusText += L"LukeRoss Mod : [OK] Detected (Proxy active, RealVR64.dll preserved)\r\n";
    } else if (g_hasRealVR64) {
        statusText += L"LukeRoss Mod : [OK] Detected (RealVR64.dll present)\r\n";
    } else if (g_hasLukeRoss) {
        statusText += L"LukeRoss Mod : [OK] Detected (dxgi.dll original)\r\n";
    } else {
        statusText += L"LukeRoss Mod : [NOT DETECTED] Please install LukeRoss REAL VR mod first!\r\n";
    }

    if (g_hasDLSS) {
        statusText += L"DLSS Engine  : [OK] " + g_dlssVersionStr + L"\r\n";
    } else {
        statusText += L"DLSS Engine  : [WARNING] No nvngx_dlss.dll found in directory\r\n";
    }

    if (g_isGameRunning) {
        statusText += L"Process      : [RUNNING] " + g_runningProcessName + L" is currently active!\r\n";
    } else {
        statusText += L"Process      : [CLOSED] Ready for file operations\r\n";
    }

    if (g_hasOurProxy) {
        statusText += L"State        : DLSS 5 <> VR is currently INSTALLED and ACTIVE.";
    } else if (g_hasLukeRoss) {
        statusText += L"State        : Ready to Install DLSS 5 Neural Reconstruction.";
    } else {
        statusText += L"State        : RealVR mod missing. Run RealConfig.bat on game first.";
    }

    SetWindowTextW(g_hStaticStatus, statusText.c_str());

    EnableWindow(g_hBtnInstall, g_hasLukeRoss ? TRUE : FALSE);
    EnableWindow(g_hBtnRestore, (g_hasOurProxy || g_hasRealVR64) ? TRUE : FALSE);
}

void DoInstall()
{
    AppendLog(L"----------------------------------------------------------------------");
    AppendLog(L"[START] Starting DLSS 5 <> VR Installation...");

    if (g_isGameRunning) {
        int res = MessageBoxW(g_hMainWnd,
            (g_runningProcessName + L" is currently running!\nWould you like to close it automatically to unlock game files?").c_str(),
            L"Game Process Running", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            AppendLog(L"[INFO] Terminating running game process: " + g_runningProcessName);
            KillRunningGame(g_runningProcessName);
        } else {
            AppendLog(L"[ABORT] Installation cancelled by user because game is running.");
            return;
        }
    }

    // Step 1: The SWAP (Safeguard original LukeRoss dxgi.dll -> RealVR64.dll)
    wchar_t dxgiDst[MAX_PATH];
    PathCombineW(dxgiDst, g_targetDir.c_str(), L"dxgi.dll");

    wchar_t realVR64Dst[MAX_PATH];
    PathCombineW(realVR64Dst, g_targetDir.c_str(), L"RealVR64.dll");

    if (!PathFileExistsW(realVR64Dst)) {
        if (PathFileExistsW(dxgiDst)) {
            const char ourSig[] = "DLSS 5 <> VR";
            if (!FileContainsBytes(dxgiDst, ourSig, strlen(ourSig))) {
                if (MoveFileW(dxgiDst, realVR64Dst)) {
                    AppendLog(L"[SWAP] Successfully preserved LukeRoss dxgi.dll -> RealVR64.dll");
                } else {
                    AppendLog(L"[ERROR] Failed to rename dxgi.dll to RealVR64.dll!");
                    MessageBoxW(g_hMainWnd, L"Failed to rename dxgi.dll to RealVR64.dll. File might be locked.", L"Error", MB_ICONERROR);
                    return;
                }
            }
        }
    } else {
        AppendLog(L"[INFO] RealVR64.dll already exists. Preserving LukeRoss core.");
    }

    // Step 2: Copy / Download Components
    struct CopyPair {
        std::wstring srcName;
        std::wstring dstName;
        bool required;
    };

    std::vector<CopyPair> files = {
        { L"dxgi.dll", L"dxgi.dll", true },
        { L"openvr_api.dll", L"openvr_api.dll", true },
        { L"ReShade64_dlss5.dll", L"ReShade64_dlss5.dll", true },
        { L"renodx-dlss5.addon64", L"renodx-dlss5.addon64", true },
        { L"cudart64_12.dll", L"cudart64_12.dll", false },
        { L"nvngx_dlssnr.dll", L"nvngx_dlssnr.dll", false }
    };

    for (const auto& item : files) {
        std::wstring src = FindOrDownloadComponent(item.srcName);
        if (src.empty()) {
            if (item.required) {
                AppendLog(L"[ERROR] Missing required component: " + item.srcName);
                MessageBoxW(g_hMainWnd, (L"Missing required component: " + item.srcName).c_str(), L"Component Missing", MB_ICONERROR);
                return;
            } else {
                AppendLog(L"[WARN] Optional component not found or download failed: " + item.srcName);
                continue;
            }
        }

        wchar_t dest[MAX_PATH];
        PathCombineW(dest, g_targetDir.c_str(), item.dstName.c_str());

        if (CopyFileW(src.c_str(), dest, FALSE)) {
            AppendLog(L"[COPY] Installed: " + item.dstName);
        } else {
            AppendLog(L"[ERROR] Failed to copy " + item.dstName + L" (Error " + std::to_wstring(GetLastError()) + L")");
        }
    }

    // Step 3: Configure ReShade.ini with optimal VR parameters
    wchar_t iniPath[MAX_PATH];
    PathCombineW(iniPath, g_targetDir.c_str(), L"ReShade.ini");
    WritePrivateProfileStringW(L"RenoDX.DLSS5", L"NeuralUplift", L"1", iniPath);
    WritePrivateProfileStringW(L"RenoDX.DLSS5", L"EnableHooks", L"1", iniPath);       // Standard hooks (EnableHooks=2 causes visual corruption)
    WritePrivateProfileStringW(L"RenoDX.DLSS5", L"NRPreset", L"2", iniPath);          // Preset 2: Performance
    WritePrivateProfileStringW(L"RenoDX.DLSS5", L"NRStyle", L"0", iniPath);           // Neutral style (prevents filmic tone collision)
    AppendLog(L"[CONFIG] Configured ReShade.ini: [RenoDX.DLSS5] NeuralUplift=1, EnableHooks=1, NRPreset=2, NRStyle=0");

    AppendLog(L"[SUCCESS] Installation finished successfully!");
    AppendLog(L"----------------------------------------------------------------------");

    InspectTarget();
    MessageBoxW(g_hMainWnd, L"DLSS 5 <> VR installed successfully!\nYou can now launch the game in VR.", L"Success", MB_OK | MB_ICONINFORMATION);
}

void DoRestore()
{
    AppendLog(L"----------------------------------------------------------------------");
    AppendLog(L"[START] Restoring vanilla LukeRoss REAL VR mod...");

    if (g_isGameRunning) {
        int res = MessageBoxW(g_hMainWnd,
            (g_runningProcessName + L" is currently running!\nWould you like to close it automatically to unlock game files?").c_str(),
            L"Game Process Running", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            KillRunningGame(g_runningProcessName);
        } else {
            return;
        }
    }

    wchar_t dxgiDst[MAX_PATH];
    PathCombineW(dxgiDst, g_targetDir.c_str(), L"dxgi.dll");

    wchar_t realVR64Dst[MAX_PATH];
    PathCombineW(realVR64Dst, g_targetDir.c_str(), L"RealVR64.dll");

    if (PathFileExistsW(realVR64Dst)) {
        DeleteFileW(dxgiDst);
        if (MoveFileW(realVR64Dst, dxgiDst)) {
            AppendLog(L"[RESTORE] Restored RealVR64.dll -> dxgi.dll");
        } else {
            AppendLog(L"[ERROR] Could not restore RealVR64.dll to dxgi.dll!");
        }
    }

    std::vector<std::wstring> toRemove = {
        L"ReShade64_dlss5.dll",
        L"renodx-dlss5.addon64",
        L"vr_dlss5_proxy.log"
    };

    for (const auto& f : toRemove) {
        wchar_t p[MAX_PATH];
        PathCombineW(p, g_targetDir.c_str(), f.c_str());
        if (PathFileExistsW(p)) {
            DeleteFileW(p);
            AppendLog(L"[CLEAN] Removed: " + f);
        }
    }

    AppendLog(L"[SUCCESS] Vanilla LukeRoss REAL VR restored successfully!");
    AppendLog(L"----------------------------------------------------------------------");

    InspectTarget();
    MessageBoxW(g_hMainWnd, L"LukeRoss REAL VR restored to original state.", L"Restored", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        DragAcceptFiles(hWnd, TRUE);

        HWND hTitle = CreateWindowW(L"STATIC", L"DLSS 5 <> VR - Universal Installer",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 15, 420, 26, hWnd, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        g_hBtnUpdate = CreateWindowW(L"BUTTON", L"Check Update",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 460, 15, 120, 28, hWnd, (HMENU)IDC_BTN_UPDATE, NULL, NULL);
        SendMessageW(g_hBtnUpdate, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hSub = CreateWindowW(L"STATIC", L"Enable DLSS 5 Neural Reconstruction on LukeRoss R.E.A.L. VR Games",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 42, 560, 20, hWnd, NULL, NULL, NULL);
        SendMessageW(hSub, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hLblExe = CreateWindowW(L"STATIC", L"Target Game Executable (*.exe):",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 75, 400, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hLblExe, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 20, 95, 460, 25, hWnd, (HMENU)IDC_EDIT_PATH, NULL, NULL);
        SendMessageW(g_hEditPath, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnBrowse = CreateWindowW(L"BUTTON", L"Browse...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 490, 94, 90, 27, hWnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
        SendMessageW(g_hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hGrp = CreateWindowW(L"BUTTON", L"Diagnostics & Detection",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 20, 130, 560, 160, hWnd, NULL, NULL, NULL);
        SendMessageW(hGrp, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hStaticStatus = CreateWindowW(L"STATIC", L"Status: Please select a game executable (*.exe)...",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 35, 150, 530, 132, hWnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);
        SendMessageW(g_hStaticStatus, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnInstall = CreateWindowW(L"BUTTON", L"Install / Update DLSS 5",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_DISABLED, 20, 302, 200, 36, hWnd, (HMENU)IDC_BTN_INSTALL, NULL, NULL);
        SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnRestore = CreateWindowW(L"BUTTON", L"Restore LukeRoss Vanilla",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, 230, 302, 200, 36, hWnd, (HMENU)IDC_BTN_RESTORE, NULL, NULL);
        SendMessageW(g_hBtnRestore, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Refresh",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 480, 302, 100, 36, hWnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);
        SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Native Windows Progress Bar (Smooth animated)
        g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | PBS_SMOOTH, 20, 347, 560, 16, hWnd, (HMENU)IDC_PROGRESS_BAR, NULL, NULL);
        ShowWindow(g_hProgressBar, SW_HIDE);

        HWND hLblLog = CreateWindowW(L"STATIC", L"Activity Log:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 370, 200, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hLblLog, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hEditLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            20, 390, 560, 150, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessageW(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        AppendLog(std::wstring(L"DLSS 5 <> VR Universal Installer v") + CURRENT_VERSION_STR + L" ready.");
        AppendLog(L"Drag & drop a game executable here or click Browse.");
        break;
    }

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t dropped[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, dropped, MAX_PATH)) {
            if (PathIsDirectoryW(dropped)) {
                wchar_t searchPattern[MAX_PATH];
                PathCombineW(searchPattern, dropped, L"*.exe");
                WIN32_FIND_DATAW ffd;
                HANDLE hFind = FindFirstFileW(searchPattern, &ffd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    PathCombineW(dropped, dropped, ffd.cFileName);
                    FindClose(hFind);
                }
            }
            g_selectedExe = dropped;
            SetWindowTextW(g_hEditPath, g_selectedExe.c_str());
            AppendLog(L"[TARGET] Selected: " + g_selectedExe);
            InspectTarget();
        }
        DragFinish(hDrop);
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDC_BTN_BROWSE) {
            wchar_t szFile[MAX_PATH] = L"";
            OPENFILENAMEW ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"Game Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameW(&ofn)) {
                g_selectedExe = szFile;
                SetWindowTextW(g_hEditPath, g_selectedExe.c_str());
                AppendLog(L"[TARGET] Selected: " + g_selectedExe);
                InspectTarget();
            }
        }
        else if (wmId == IDC_EDIT_PATH && wmEvent == EN_CHANGE) {
            wchar_t buf[MAX_PATH];
            GetWindowTextW(g_hEditPath, buf, MAX_PATH);
            g_selectedExe = buf;
            InspectTarget();
        }
        else if (wmId == IDC_BTN_INSTALL) {
            DoInstall();
        }
        else if (wmId == IDC_BTN_RESTORE) {
            int res = MessageBoxW(hWnd, L"Are you sure you want to restore the original LukeRoss REAL VR mod?\nThis will remove DLSS 5 proxy and restore RealVR64.dll back to dxgi.dll.", L"Confirm Restore", MB_YESNO | MB_ICONQUESTION);
            if (res == IDYES) {
                DoRestore();
            }
        }
        else if (wmId == IDC_BTN_REFRESH) {
            InspectTarget();
            AppendLog(L"[REFRESH] Diagnostics updated.");
        }
        else if (wmId == IDC_BTN_UPDATE) {
            DoCheckUpdate();
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    g_hFontTitle = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_hFontMono = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VRDLSS5InstallerClass";
    RegisterClassExW(&wc);

    int w = 620;
    int h = 600;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    g_hMainWnd = CreateWindowExW(
        0,
        L"VRDLSS5InstallerClass",
        L"DLSS 5 <> VR - Universal Installer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontMono) DeleteObject(g_hFontMono);

    return (int)msg.wParam;
}
