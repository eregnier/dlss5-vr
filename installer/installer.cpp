#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <wincrypt.h>
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
#define CURRENT_VERSION_STR L"1.1.0"
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
#define IDC_EDIT_MODEL      1010
#define IDC_BTN_MODEL       1011

HWND g_hMainWnd = NULL;
HWND g_hEditPath = NULL;
HWND g_hBtnBrowse = NULL;
HWND g_hEditModel = NULL;
HWND g_hBtnModel = NULL;
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
std::wstring g_selectedModel = L"";   // user-provided nvngx_dlssnr.dll ("found elsewhere")
std::wstring g_targetDir = L"";

// NVIDIA Neural Rendering runtime hashes from the upstream install guide.
static const wchar_t* kModelDllName = L"nvngx_dlssnr.dll";
static const wchar_t* kModelHashRtx50 =
    L"E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E";
static const wchar_t* kModelHashShortFuse =
    L"E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A";

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

// ---------------------------------------------------------------------------
// User-provided NVIDIA runtime (nvngx_dlssnr.dll)
//
// The runtime is NVIDIA proprietary: this installer neither bundles it nor
// downloads it from unofficial mirrors. The user points at the file they
// obtained themselves; we validate it, remember the path and copy it next to
// the game executable during install.
// ---------------------------------------------------------------------------
void InspectTarget();

std::wstring ComputeFileSha256(const std::wstring& path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return L"";

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::wstring result;

    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        BYTE buffer[1 << 16];
        DWORD read = 0;
        while (ReadFile(hFile, buffer, sizeof(buffer), &read, NULL) && read > 0)
            CryptHashData(hHash, buffer, read, 0);

        BYTE digest[32] = {0};
        DWORD digestLen = sizeof(digest);
        if (CryptGetHashParam(hHash, HP_HASHVAL, digest, &digestLen, 0))
        {
            static const wchar_t* hex = L"0123456789ABCDEF";
            result.reserve(64);
            for (DWORD i = 0; i < digestLen; i++)
            {
                result.push_back(hex[digest[i] >> 4]);
                result.push_back(hex[digest[i] & 0x0F]);
            }
        }
    }

    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

std::wstring GetInstallerSettingsPath()
{
    wchar_t appData[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData)))
        return L"";

    std::wstring dir = std::wstring(appData) + L"\\DLSS5-VR";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\installer.ini";
}

void SaveInstallerSettings()
{
    std::wstring ini = GetInstallerSettingsPath();
    if (ini.empty()) return;
    if (!g_selectedModel.empty())
        WritePrivateProfileStringW(L"Paths", L"ModelFile", g_selectedModel.c_str(), ini.c_str());
    if (!g_selectedExe.empty())
        WritePrivateProfileStringW(L"Paths", L"GameExe", g_selectedExe.c_str(), ini.c_str());
}

void LoadInstallerSettings()
{
    std::wstring ini = GetInstallerSettingsPath();
    if (ini.empty()) return;

    wchar_t buffer[MAX_PATH] = L"";
    if (GetPrivateProfileStringW(L"Paths", L"ModelFile", L"", buffer, MAX_PATH, ini.c_str()) > 0)
    {
        if (PathFileExistsW(buffer)) {
            g_selectedModel = buffer;
            if (g_hEditModel) SetWindowTextW(g_hEditModel, g_selectedModel.c_str());
            AppendLog(L"[MODEL] Restored previous selection: " + g_selectedModel);
        } else {
            // Never keep a stale path: it would silently skip the file at install.
            WritePrivateProfileStringW(L"Paths", L"ModelFile", NULL, ini.c_str());
            AppendLog(L"[MODEL] Saved selection no longer exists, cleared: " + std::wstring(buffer));
        }
    }
    if (GetPrivateProfileStringW(L"Paths", L"GameExe", L"", buffer, MAX_PATH, ini.c_str()) > 0
        && PathFileExistsW(buffer))
    {
        g_selectedExe = buffer;
        if (g_hEditPath) SetWindowTextW(g_hEditPath, g_selectedExe.c_str());
        AppendLog(L"[TARGET] Restored previous target: " + g_selectedExe);
    }
}

void SelectModelFile(HWND hWnd, const std::wstring& path)
{
    if (path.empty() || !PathFileExistsW(path.c_str())) {
        MessageBoxW(hWnd, L"File not found.", L"Model file", MB_OK | MB_ICONWARNING);
        return;
    }

    const wchar_t* baseName = PathFindFileNameW(path.c_str());
    if (_wcsicmp(baseName, kModelDllName) != 0) {
        int res = MessageBoxW(hWnd,
            L"The selected file is not named nvngx_dlssnr.dll.\n\n"
            L"It will be copied to the game folder under that exact name. Continue?",
            L"Unexpected file name", MB_YESNO | MB_ICONWARNING);
        if (res != IDYES) return;
    }

    g_selectedModel = path;
    if (g_hEditModel) SetWindowTextW(g_hEditModel, g_selectedModel.c_str());
    AppendLog(L"[MODEL] User-provided runtime: " + g_selectedModel);

    std::wstring hash = ComputeFileSha256(path);
    if (!hash.empty()) {
        AppendLog(L"[MODEL] SHA-256: " + hash);
        if (hash == kModelHashRtx50)
            AppendLog(L"[MODEL] Recognized: NVIDIA-signed 310.8 (RTX 50)");
        else if (hash == kModelHashShortFuse)
            AppendLog(L"[MODEL] Recognized: ShortFuse cross-generation 310.8 (RTX 20/30/40)");
        else
            AppendLog(L"[MODEL] Unrecognized hash - verify the file yourself before installing.");
    }

    SaveInstallerSettings();
    InspectTarget();
}

// True when a usable NVIDIA runtime is currently selected. If it was moved or
// deleted since it was picked (or since the last run), the stale path is
// dropped, the settings are cleaned and the user is told to pick it again.
bool VerifySelectedModel(bool clearIfMissing)
{
    if (g_selectedModel.empty())
        return false;

    if (PathFileExistsW(g_selectedModel.c_str()))
        return true;

    AppendLog(L"[MODEL] Selected runtime file is no longer on disk: " + g_selectedModel);
    AppendLog(L"[MODEL] Please pick it again with Browse (or place it next to the game exe).");

    if (clearIfMissing) {
        g_selectedModel.clear();
        if (g_hEditModel) SetWindowTextW(g_hEditModel, L"");
        std::wstring ini = GetInstallerSettingsPath();
        if (!ini.empty())
            WritePrivateProfileStringW(L"Paths", L"ModelFile", NULL, ini.c_str());
    }
    return false;
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
        std::wstring root = std::wstring(localApp) + L"\\DLSS5-VR";
        std::wstring cachePath = root + L"\\cache";

        // Reuse the cache left by the retired Go CLI (vr-dlss5-patch) so users
        // do not re-download the engine/model packages.
        std::wstring legacyPath = std::wstring(localApp) + L"\\vr-dlss5-patch\\cache";
        if (!PathFileExistsW(cachePath.c_str()) && PathFileExistsW(legacyPath.c_str()))
            return legacyPath;

        CreateDirectoryW(root.c_str(), NULL);
        CreateDirectoryW(cachePath.c_str(), NULL);
        return cachePath;
    }
    return L".";
}

bool CopyDirectoryRecursiveW(const std::wstring& srcDir, const std::wstring& dstDir)
{
    CreateDirectoryW(dstDir.c_str(), NULL);
    std::wstring searchPath = srcDir + L"\\*.*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
            continue;

        std::wstring srcItem = srcDir + L"\\" + ffd.cFileName;
        std::wstring dstItem = dstDir + L"\\" + ffd.cFileName;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirectoryRecursiveW(srcItem, dstItem);
        } else {
            CopyFileW(srcItem.c_str(), dstItem.c_str(), FALSE);
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
    return true;
}

bool DeleteDirectoryRecursiveW(const std::wstring& path)
{
    std::wstring searchPath = path + L"\\*.*";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return RemoveDirectoryW(path.c_str()) != FALSE;

    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
            continue;

        std::wstring item = path + L"\\" + ffd.cFileName;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteDirectoryRecursiveW(item);
        } else {
            DeleteFileW(item.c_str());
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

std::wstring FindOptiScalerBackendDir()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    std::vector<std::wstring> bases = {
        std::wstring(exePath) + L"\\OptiScaler",
        std::wstring(exePath) + L"\\..\\OptiScaler",
        std::wstring(exePath) + L"\\deps\\OptiScaler",
        std::wstring(exePath) + L"\\..\\deps\\OptiScaler",
        GetCacheDirectory() + L"\\OptiScaler"
    };

    for (const auto& b : bases) {
        if (PathFileExistsW(b.c_str()) && PathIsDirectoryW(b.c_str())) {
            return b;
        }
    }
    return L"";
}

std::wstring FindOrDownloadComponent(const std::wstring& fileName)
{
    // User-selected NVIDIA runtime takes priority and is never downloaded.
    if (fileName == kModelDllName && !g_selectedModel.empty() &&
        PathFileExistsW(g_selectedModel.c_str()))
    {
        return g_selectedModel;
    }

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

    // The NVIDIA Neural Rendering runtime (nvngx_dlssnr.dll, ~160 MB) is NVIDIA
    // proprietary. It is deliberately neither bundled nor downloaded by this
    // installer: the user supplies the 310.8 runtime matching their GPU, exactly
    // like the upstream OptiScaler-DLSSNR project requires.
    if (fileName == L"nvngx_dlssnr.dll") {
        static bool s_modelNoticeShown = false;
        AppendLog(L"[LICENSE] nvngx_dlssnr.dll is not distributed with this package (NVIDIA proprietary).");
        AppendLog(L"[ACTION] Copy the 310.8 runtime for your GPU next to the game executable, then re-run Install.");

        if (!s_modelNoticeShown) {
            s_modelNoticeShown = true;
            int res = MessageBoxW(g_hMainWnd,
                L"The NVIDIA Neural Rendering runtime (nvngx_dlssnr.dll) is not bundled or downloaded by this installer "
                L"for licensing reasons.\n\n"
                L"Download the 310.8 runtime matching your GPU:\n"
                L"  - RTX 50: NVIDIA-signed original\n"
                L"  - RTX 20 / 30 / 40: ShortFuse cross-generation runtime\n\n"
                L"Place it next to the game executable, then re-run Install / Update.\n\n"
                L"Open the upstream install guide (hashes, sources)?",
                L"NVIDIA runtime required (not redistributed)", MB_YESNO | MB_ICONINFORMATION);

            if (res == IDYES)
                ShellExecuteW(NULL, L"open",
                              L"https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/main/INSTALL-DLSSNR.md",
                              NULL, NULL, SW_SHOWNORMAL);
        }
        return L"";
    }

    // Auto-download OptiScaler Pre-SR package (OptiScaler.dll, nvngx.dll_dlssnr.dll, OptiScaler/ backend) if missing
    if (fileName == L"OptiScaler.dll" || fileName == L"nvngx.dll_dlssnr.dll" || fileName == L"OptiScaler.ini") {
        wchar_t cachedOpti[MAX_PATH];
        PathCombineW(cachedOpti, cacheDir.c_str(), fileName.c_str());
        if (PathFileExistsW(cachedOpti)) return std::wstring(cachedOpti);

        wchar_t zipDest[MAX_PATH];
        PathCombineW(zipDest, cacheDir.c_str(), L"OptiScaler-DLSSNR-v0.7.6.zip");

        std::wstring url = L"https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/download/v0.7.6/OptiScaler-DLSSNR-v0.7.6.zip";
        if (DownloadHttpFile(url, zipDest, L"OptiScaler Pre-SR Multipass Package (v0.7.6)")) {
            ExtractZip(zipDest, cacheDir);
            DeleteFileW(zipDest);
            if (PathFileExistsW(cachedOpti)) return std::wstring(cachedOpti);
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

    // Drop a stale NVIDIA runtime selection before reporting status.
    VerifySelectedModel(true);

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

    bool hasOptiScaler = false;
    wchar_t optiAsi[MAX_PATH], optiDll[MAX_PATH];
    PathCombineW(optiAsi, g_targetDir.c_str(), L"OptiScaler.asi");
    PathCombineW(optiDll, g_targetDir.c_str(), L"OptiScaler.dll");
    if (PathFileExistsW(optiAsi) || PathFileExistsW(optiDll)) {
        hasOptiScaler = true;
    }

    if (g_hasDLSS) {
        statusText += L"DLSS Engine  : [OK] " + g_dlssVersionStr + L"\r\n";
    } else {
        statusText += L"DLSS Engine  : [WARNING] No nvngx_dlss.dll found in directory\r\n";
    }

    if (hasOptiScaler) {
        statusText += L"OptiScaler   : [OK] Pre-SR Engine detected (OptiScaler.asi/dll)\r\n";
    } else {
        statusText += L"OptiScaler   : [READY] Will be deployed on install\r\n";
    }

    // NVIDIA Neural Rendering runtime (user-provided, never redistributed).
    {
        std::wstring modelShown;
        if (!g_selectedModel.empty() && PathFileExistsW(g_selectedModel.c_str())) {
            modelShown = g_selectedModel;
        } else {
            wchar_t modelInTarget[MAX_PATH];
            PathCombineW(modelInTarget, g_targetDir.c_str(), kModelDllName);
            if (PathFileExistsW(modelInTarget)) modelShown = modelInTarget;
        }

        if (!modelShown.empty())
            statusText += L"NVIDIA Runtime: [OK] " + modelShown + L"\r\n";
        else
            statusText += L"NVIDIA Runtime: [MISSING] Select your " + std::wstring(kModelDllName) +
                          L" below (not redistributed)\r\n";
    }

    if (g_isGameRunning) {
        statusText += L"Process      : [RUNNING] " + g_runningProcessName + L" is currently active!\r\n";
    } else {
        statusText += L"Process      : [CLOSED] Ready for file operations\r\n";
    }

    if (g_hasOurProxy && hasOptiScaler) {
        statusText += L"State        : DLSS 5 <> VR (OptiScaler Pre-SR) is currently INSTALLED and ACTIVE.";
    } else if (g_hasOurProxy) {
        statusText += L"State        : DLSS 5 Proxy active (Update recommended to deploy OptiScaler Pre-SR).";
    } else if (g_hasLukeRoss) {
        statusText += L"State        : Ready to Install DLSS 5 (OptiScaler Pre-SR Multipass).";
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
    AppendLog(L"[START] Starting DLSS 5 <> VR Installation (OptiScaler Pre-SR Engine)...");

    // The user's runtime may have been moved/deleted since it was picked.
    if (VerifySelectedModel(true))
        AppendLog(L"[MODEL] Using user-provided runtime: " + g_selectedModel);

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
        { L"OptiScaler.dll", L"OptiScaler.asi", true },              // LukeRoss native stéréoscopic hook target
        { L"OptiScaler.dll", L"OptiScaler.dll", true },              // Direct DLL reference
        { L"nvngx.dll_dlssnr.dll", L"nvngx.dll_dlssnr.dll", true },  // Signature forwarder
        { L"OptiScaler.ini", L"OptiScaler.ini", false },             // Base configuration
        { L"cudart64_12.dll", L"cudart64_12.dll", false },
        { L"nvngx_dlssnr.dll", L"nvngx_dlssnr.dll", false }          // DLSS 5 Neural Model
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

    // Step 2b: Copy OptiScaler/ backend folder if present
    std::wstring backendDir = FindOptiScalerBackendDir();
    if (!backendDir.empty()) {
        wchar_t dstBackend[MAX_PATH];
        PathCombineW(dstBackend, g_targetDir.c_str(), L"OptiScaler");
        if (CopyDirectoryRecursiveW(backendDir, dstBackend)) {
            AppendLog(L"[COPY] Successfully deployed OptiScaler/ backend shaders & modules");
        } else {
            AppendLog(L"[WARN] Could not copy OptiScaler/ backend directory");
        }
    }

    // Step 2c: Clean up legacy ReShade / RenoDX files to prevent conflicts
    std::vector<std::wstring> legacyFiles = {
        L"ReShade64_dlss5.dll",
        L"renodx-dlss5.addon64"
    };
    for (const auto& lf : legacyFiles) {
        wchar_t lp[MAX_PATH];
        PathCombineW(lp, g_targetDir.c_str(), lf.c_str());
        if (PathFileExistsW(lp)) {
            DeleteFileW(lp);
            AppendLog(L"[CLEAN] Removed legacy component: " + lf);
        }
    }

    // Step 2d: Quarantine a legacy OptiScaler engine left as a local proxy DLL
    // (typically WINMM.dll from the pre-1.1 installs). Two engines in the same
    // process fight over NGX and the HUD control channel, and the old one runs
    // the neural pass instead of the bundled build.
    {
        const wchar_t* legacyEngineProxies[] = { L"WINMM.dll" };
        for (const wchar_t* proxyName : legacyEngineProxies) {
            wchar_t proxyPath[MAX_PATH];
            PathCombineW(proxyPath, g_targetDir.c_str(), proxyName);
            if (!PathFileExistsW(proxyPath))
                continue;

            HANDLE h = CreateFileW(proxyPath, GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE)
                continue;

            LARGE_INTEGER fileSize = {};
            GetFileSizeEx(h, &fileSize);
            CloseHandle(h);

            // The real system winmm.dll is ~100 KB; a multi-MB local copy is a proxy engine.
            if (fileSize.QuadPart > 8 * 1024 * 1024) {
                std::wstring disabled = std::wstring(proxyPath) + L".disabled";
                DeleteFileW(disabled.c_str());
                if (MoveFileW(proxyPath, disabled.c_str())) {
                    AppendLog(L"[CLEAN] Quarantined legacy engine proxy: " + std::wstring(proxyName) +
                              L" -> " + std::wstring(proxyName) + L".disabled (restart the game launcher)");
                } else {
                    AppendLog(L"[WARN] Could not quarantine legacy engine proxy: " + std::wstring(proxyName));
                }
            }
        }
    }

    // Step 3: Configure OptiScaler.ini with optimal VR Pre-SR parameters
    wchar_t iniPath[MAX_PATH];
    PathCombineW(iniPath, g_targetDir.c_str(), L"OptiScaler.ini");

    WritePrivateProfileStringW(L"DlssNr", L"Enabled", L"true", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"RunBeforeSR", L"true", iniPath);         // Critical Pre-SR mode (4x smaller work area)
    WritePrivateProfileStringW(L"DlssNr", L"WorkingScale", L"0.75", iniPath);        // 72 FPS solid on RTX 5090 / 4090
    WritePrivateProfileStringW(L"DlssNr", L"Passes", L"1", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"ResidualAcrossRR", L"true", iniPath);    // RT Overdrive Ray Recon support
    WritePrivateProfileStringW(L"DlssNr", L"ResidualAcrossRRBlend", L"0.08", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"DeferredDLSS", L"false", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"ResidualFG", L"false", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"AutoCapture", L"false", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"DebugView", L"0", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"Preset", L"2", iniPath);                 // Preset 2: Performance
    WritePrivateProfileStringW(L"DlssNr", L"Intensity", L"1.0", iniPath);
    WritePrivateProfileStringW(L"DlssNr", L"AutoMask", L"true", iniPath);

    // VR HUD configuration
    WritePrivateProfileStringW(L"VRHUD", L"HUDScale", L"1", iniPath);
    WritePrivateProfileStringW(L"VRHUD", L"HUDPosition", L"0", iniPath);
    WritePrivateProfileStringW(L"VRHUD", L"FrameGuard", L"1", iniPath);
    WritePrivateProfileStringW(L"VRHUD", L"PriorityBoost", L"1", iniPath);

    AppendLog(L"[CONFIG] Configured OptiScaler.ini: [DlssNr] RunBeforeSR=true, WorkingScale=0.75, Preset=2, ResidualAcrossRR=true");

    AppendLog(L"[SUCCESS] Installation finished successfully!");
    AppendLog(L"----------------------------------------------------------------------");

    InspectTarget();
    MessageBoxW(g_hMainWnd, L"DLSS 5 <> VR (OptiScaler Pre-SR) installed successfully!\nYou can now launch the game in VR.", L"Success", MB_OK | MB_ICONINFORMATION);
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
        L"OptiScaler.asi",
        L"OptiScaler.dll",
        L"nvngx.dll_dlssnr.dll",
        L"OptiScaler.ini",
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

    wchar_t backendDir[MAX_PATH];
    PathCombineW(backendDir, g_targetDir.c_str(), L"OptiScaler");
    if (PathFileExistsW(backendDir)) {
        DeleteDirectoryRecursiveW(backendDir);
        AppendLog(L"[CLEAN] Removed OptiScaler/ backend folder");
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

        // User-provided NVIDIA Neural Rendering runtime (never redistributed).
        HWND hLblModel = CreateWindowW(L"STATIC", L"NVIDIA Runtime (nvngx_dlssnr.dll, user-supplied):",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 125, 460, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hLblModel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hEditModel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 20, 143, 460, 25, hWnd, (HMENU)IDC_EDIT_MODEL, NULL, NULL);
        SendMessageW(g_hEditModel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnModel = CreateWindowW(L"BUTTON", L"Select file...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 490, 142, 90, 27, hWnd, (HMENU)IDC_BTN_MODEL, NULL, NULL);
        SendMessageW(g_hBtnModel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hGrp = CreateWindowW(L"BUTTON", L"Diagnostics & Detection",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 20, 174, 560, 176, hWnd, NULL, NULL, NULL);
        SendMessageW(hGrp, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hStaticStatus = CreateWindowW(L"STATIC", L"Status: Please select a game executable (*.exe)...",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 35, 192, 530, 150, hWnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);
        SendMessageW(g_hStaticStatus, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnInstall = CreateWindowW(L"BUTTON", L"Install / Update DLSS 5",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_DISABLED, 20, 358, 200, 36, hWnd, (HMENU)IDC_BTN_INSTALL, NULL, NULL);
        SendMessageW(g_hBtnInstall, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnRestore = CreateWindowW(L"BUTTON", L"Restore LukeRoss Vanilla",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, 230, 358, 200, 36, hWnd, (HMENU)IDC_BTN_RESTORE, NULL, NULL);
        SendMessageW(g_hBtnRestore, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Refresh",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 480, 358, 100, 36, hWnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);
        SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Native Windows Progress Bar (Smooth animated)
        g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | PBS_SMOOTH, 20, 400, 560, 16, hWnd, (HMENU)IDC_PROGRESS_BAR, NULL, NULL);
        ShowWindow(g_hProgressBar, SW_HIDE);

        HWND hLblLog = CreateWindowW(L"STATIC", L"Activity Log:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 20, 424, 200, 18, hWnd, NULL, NULL, NULL);
        SendMessageW(hLblLog, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        g_hEditLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            20, 444, 560, 124, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessageW(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        AppendLog(std::wstring(L"DLSS 5 <> VR Universal Installer v") + CURRENT_VERSION_STR + L" ready.");
        AppendLog(L"Drag & drop a game executable here or click Browse.");
        AppendLog(L"Select your own nvngx_dlssnr.dll (NVIDIA runtime) - it is not redistributed by this tool.");

        LoadInstallerSettings();
        if (!g_selectedExe.empty())
            InspectTarget();
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

            // Dropping the NVIDIA runtime anywhere in the window selects it.
            if (_wcsicmp(PathFindFileNameW(dropped), kModelDllName) == 0) {
                SelectModelFile(hWnd, dropped);
            } else {
                g_selectedExe = dropped;
                SetWindowTextW(g_hEditPath, g_selectedExe.c_str());
                AppendLog(L"[TARGET] Selected: " + g_selectedExe);
                SaveInstallerSettings();
                InspectTarget();
            }
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
                SaveInstallerSettings();
                InspectTarget();
            }
        }
        else if (wmId == IDC_EDIT_PATH && wmEvent == EN_CHANGE) {
            wchar_t buf[MAX_PATH];
            GetWindowTextW(g_hEditPath, buf, MAX_PATH);
            g_selectedExe = buf;
            InspectTarget();
        }
        else if (wmId == IDC_BTN_MODEL) {
            wchar_t szModel[MAX_PATH] = L"";
            OPENFILENAMEW ofnModel;
            ZeroMemory(&ofnModel, sizeof(ofnModel));
            ofnModel.lStructSize = sizeof(ofnModel);
            ofnModel.hwndOwner = hWnd;
            ofnModel.lpstrFilter = L"NVIDIA Neural Rendering runtime (nvngx_dlssnr.dll)\0nvngx_dlssnr.dll\0DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
            ofnModel.lpstrFile = szModel;
            ofnModel.nMaxFile = MAX_PATH;
            ofnModel.lpstrTitle = L"Select your own nvngx_dlssnr.dll (not redistributed by this tool)";
            ofnModel.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameW(&ofnModel))
                SelectModelFile(hWnd, szModel);
        }
        else if (wmId == IDC_EDIT_MODEL && wmEvent == EN_CHANGE) {
            wchar_t buf[MAX_PATH];
            GetWindowTextW(g_hEditModel, buf, MAX_PATH);
            if (buf[0] != L'\0') g_selectedModel = buf;
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
    int h = 650;
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
