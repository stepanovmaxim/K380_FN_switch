#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <dbt.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define IDM_TOGGLE 1001
#define IDM_STARTUP 1002
#define IDM_EXIT 1003

NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
bool fKeysOn = true;

bool SetK380FKeys(bool fkeys) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA devInfoData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    bool found = false;

    for (int i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &hidGuid, i, &devInfoData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devInfoData, NULL, 0, &reqSize, NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A devDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(reqSize);
        devDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devInfoData, devDetail, reqSize, NULL, NULL)) {
            HANDLE hFile = CreateFileA(devDetail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attr = { sizeof(HIDD_ATTRIBUTES) };
                if (HidD_GetAttributes(hFile, &attr) && attr.VendorID == 0x046d && attr.ProductID == 0xb342) {
                    unsigned char buf[7] = { 0x10, 0xFF, 0x0B, 0x1E, fkeys ? (unsigned char)0x00 : (unsigned char)0x01, 0x00, 0x00 };
                    HidD_SetOutputReport(hFile, buf, sizeof(buf));
                    found = true;
                }
                CloseHandle(hFile);
            }
        }
        free(devDetail);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

bool IsStartupEnabled() {
    HKEY hKey;
    bool enabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        enabled = (RegQueryValueExW(hKey, L"K380_FnLock", NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
        RegCloseKey(hKey);
    }
    return enabled;
}

void SetStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, L"K380_FnLock", 0, REG_SZ, (BYTE*)path, (lstrlenW(path) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"K380_FnLock");
        }
        RegCloseKey(hKey);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            nid.hWnd = hwnd;
            nid.uID = 1;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            
            nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101));
            if (!nid.hIcon) nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION); 

            lstrcpyW(nid.szTip, L"K380 F-Keys");
            Shell_NotifyIconW(NIM_ADD, &nid);
            
            SetK380FKeys(fKeysOn);

            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);
            DEV_BROADCAST_DEVICEINTERFACE_A filter = { sizeof(filter), DBT_DEVTYP_DEVICEINTERFACE, 0, hidGuid };
            RegisterDeviceNotificationA(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
            break;
        }
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVICEARRIVAL) SetTimer(hwnd, 1, 2000, NULL);
            break;
        case WM_TIMER:
            if (wParam == 1) { KillTimer(hwnd, 1); SetK380FKeys(fKeysOn); }
            break;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                
                AppendMenuW(hMenu, MF_STRING | (fKeysOn ? MF_CHECKED : MF_UNCHECKED), IDM_TOGGLE, L"Toggle F-Keys (On/Off)");
                AppendMenuW(hMenu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_STARTUP, L"Run at Startup");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);

                if (cmd == IDM_TOGGLE) { fKeysOn = !fKeysOn; SetK380FKeys(fKeysOn); }
                else if (cmd == IDM_STARTUP) SetStartup(!IsStartupEnabled());
                else if (cmd == IDM_EXIT) PostQuitMessage(0);
            }
            break;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        default: return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

extern "C" int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CreateMutexW(0, FALSE, L"Local\\K380FnLockMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101)); 
    wc.lpszClassName = L"K380FnTray";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"K380", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
    if (!hwnd) return 1;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}