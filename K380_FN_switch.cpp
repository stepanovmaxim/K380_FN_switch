// K380 FN Switch - tray utility that locks F1-F12 as primary keys on
// Logitech K380 / K380s keyboards by sending a HID++ short report.
//
// Build: see build.cmd (MSVC, no CRT, ~15 KB executable).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <dbt.h>
#include <wtsapi32.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

// ---------------------------------------------------------------- constants

#define WM_TRAYICON     (WM_USER + 1)

#define IDM_TOGGLE      1001
#define IDM_STARTUP     1002
#define IDM_EXIT        1003
#define IDM_REAPPLY     1004
#define IDM_HOTKEY      1005
#define IDM_FKEYS_ON    1006
#define IDM_FKEYS_OFF   1007

#define IDI_APPICON     101
#define TIMER_APPLY     1
#define HOTKEY_ID       1

#define LOGITECH_VID    0x046D
#define K380_PID        0xB342      // K380 (Bluetooth)
#define MAX_PIDS        16

#define APPLY_RETRIES   12          // ~18 s of retries after a reconnect
#define APPLY_INTERVAL  1500

static const WCHAR CLASS_NAME[]  = L"K380FnTray";
static const WCHAR MUTEX_NAME[]  = L"Local\\K380FnLockMutex";
static const WCHAR REG_APP[]     = L"Software\\K380FnSwitch";
static const WCHAR REG_RUN[]     = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR RUN_VALUE[]   = L"K380_FnLock";

// ------------------------------------------------------------------ globals

static NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };
static HINSTANCE g_hInst;
static HICON     g_iconBase;   // as authored, owned for the process lifetime
static HICON     g_iconOn;     // g_iconBase, or a lightened copy on dark themes
static HICON     g_iconOff;
static HDEVNOTIFY g_hDevNotify;
static UINT      g_msgTaskbarCreated;
static BOOL      g_fKeysOn      = TRUE;
static BOOL      g_hotkeyOn     = FALSE;
static BOOL      g_deviceFound  = FALSE;
static int       g_retriesLeft;
static USHORT    g_pids[MAX_PIDS] = { K380_PID };
static int       g_pidCount     = 1;

// ------------------------------------------------- freestanding CRT helpers
// build.cmd links with /NODEFAULTLIB, but the compiler still emits calls to
// memset/memcpy for aggregate initialisation, so supply them. Defining
// K380_NO_CRT is what build.cmd does; a plain CRT-linked build skips this and
// uses the runtime's own copies.

#ifdef K380_NO_CRT
#pragma function(memset)
extern "C" void *memset(void *dst, int value, size_t count) {
    unsigned char *p = (unsigned char *)dst;
    while (count--) *p++ = (unsigned char)value;
    return dst;
}

#pragma function(memcpy)
extern "C" void *memcpy(void *dst, const void *src, size_t count) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (count--) *d++ = *s++;
    return dst;
}
#else
#include <string.h>
#endif

// ------------------------------------------------------------ small helpers

static BOOL ParseHexU16(const WCHAR *s, USHORT *out) {
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) s += 2;
    UINT value = 0;
    int digits = 0;
    for (; *s; ++s) {
        UINT d;
        if (*s >= L'0' && *s <= L'9')      d = (UINT)(*s - L'0');
        else if (*s >= L'a' && *s <= L'f') d = (UINT)(*s - L'a') + 10;
        else if (*s >= L'A' && *s <= L'F') d = (UINT)(*s - L'A') + 10;
        else return FALSE;
        value = (value << 4) | d;
        if (++digits > 4) return FALSE;
    }
    if (!digits) return FALSE;
    *out = (USHORT)value;
    return TRUE;
}

static void AddPid(USHORT pid) {
    if (!pid || g_pidCount >= MAX_PIDS) return;
    for (int i = 0; i < g_pidCount; ++i)
        if (g_pids[i] == pid) return;
    g_pids[g_pidCount++] = pid;
}

static BOOL PidMatches(USHORT pid) {
    for (int i = 0; i < g_pidCount; ++i)
        if (g_pids[i] == pid) return TRUE;
    return FALSE;
}

// Case-insensitive substring test used to skip device-change events that are
// not about a Logitech device.
static BOOL ContainsLogitechVid(const WCHAR *path) {
    static const WCHAR needle[] = L"vid_046d";
    for (; *path; ++path) {
        int i = 0;
        while (needle[i]) {
            WCHAR c = path[i];
            if (c >= L'A' && c <= L'Z') c = (WCHAR)(c + 32);
            if (c != needle[i]) break;
            ++i;
        }
        if (!needle[i]) return TRUE;
    }
    return FALSE;
}

// ----------------------------------------------------------------- registry

static DWORD RegReadDword(const WCHAR *name, DWORD fallback) {
    HKEY key;
    DWORD value = fallback, size = sizeof(value), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_APP, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
            type != REG_DWORD)
            value = fallback;
        RegCloseKey(key);
    }
    return value;
}

static void RegWriteDword(const WCHAR *name, DWORD value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_APP, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL)
        == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
    }
}

static BOOL RegReadString(const WCHAR *name, WCHAR *buffer, DWORD cch) {
    HKEY key;
    BOOL ok = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_APP, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD size = cch * sizeof(WCHAR), type = 0;
        ok = (RegQueryValueExW(key, name, NULL, &type, (BYTE *)buffer, &size) == ERROR_SUCCESS &&
              type == REG_SZ);
        RegCloseKey(key);
    }
    if (ok) buffer[cch - 1] = 0;
    return ok;
}

static BOOL IsStartupEnabled(void) {
    HKEY key;
    BOOL enabled = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        enabled = (RegQueryValueExW(key, RUN_VALUE, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
        RegCloseKey(key);
    }
    return enabled;
}

static void SetStartup(BOOL enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return;
    if (enable) {
        WCHAR path[MAX_PATH + 4];
        path[0] = L'"';
        DWORD len = GetModuleFileNameW(NULL, path + 1, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            path[len + 1] = L'"';
            path[len + 2] = 0;
            RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, (const BYTE *)path,
                           (len + 3) * sizeof(WCHAR));
        }
    } else {
        RegDeleteValueW(key, RUN_VALUE);
    }
    RegCloseKey(key);
}

// --------------------------------------------------------------- HID access

enum ApplyResult { APPLY_OK, APPLY_NO_DEVICE, APPLY_WRITE_FAILED };

// Keyboards are opened exclusively by the input stack, so fall back to weaker
// access rights instead of giving up on the first failure.
static HANDLE OpenHidDevice(const WCHAR *path) {
    static const DWORD access[] = { GENERIC_READ | GENERIC_WRITE, GENERIC_WRITE, 0 };
    for (int i = 0; i < 3; ++i) {
        HANDLE h = CreateFileW(path, access[i], FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

// The HID++ short report only lives on the vendor-defined collection whose
// output reports are exactly 7 bytes long.
static BOOL IsHidppShortCollection(HANDLE device) {
    PHIDP_PREPARSED_DATA preparsed = NULL;
    BOOL match = FALSE;
    if (HidD_GetPreparsedData(device, &preparsed)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
            match = (caps.OutputReportByteLength == 7 && caps.UsagePage >= 0xFF00);
        HidD_FreePreparsedData(preparsed);
    }
    return match;
}

// One enumeration pass. `strict` restricts writes to the HID++ collection;
// `present` reports whether a matching VID/PID showed up at all.
static int ApplyPass(BOOL fkeys, BOOL strict, BOOL *present) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return 0;

    SP_DEVICE_INTERFACE_DATA iface = { sizeof(SP_DEVICE_INTERFACE_DATA) };
    union {
        SP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        BYTE raw[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 512 * sizeof(WCHAR)];
    } buffer;

    int written = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &iface); ++i) {
        buffer.detail.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &iface, &buffer.detail,
                                              sizeof(buffer), NULL, NULL))
            continue;

        HANDLE device = OpenHidDevice(buffer.detail.DevicePath);
        if (device == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr = { sizeof(HIDD_ATTRIBUTES) };
        if (HidD_GetAttributes(device, &attr) &&
            attr.VendorID == LOGITECH_VID && PidMatches(attr.ProductID)) {
            if (present) *present = TRUE;
            if (!strict || IsHidppShortCollection(device)) {
                // HID++ 1.0 short report: set register 0x0B ("Fn inversion"),
                // 0x00 = F-keys primary, 0x01 = media keys primary.
                BYTE report[7] = { 0x10, 0xFF, 0x0B, 0x1E,
                                   (BYTE)(fkeys ? 0x00 : 0x01), 0x00, 0x00 };
                if (HidD_SetOutputReport(device, report, sizeof(report))) ++written;
            }
        }
        CloseHandle(device);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return written;
}

static ApplyResult ApplyFKeys(BOOL fkeys) {
    BOOL present = FALSE;
    int written = ApplyPass(fkeys, TRUE, &present);
    if (!written && present)
        written = ApplyPass(fkeys, FALSE, NULL);   // pre-1.1 behaviour, just in case
    if (written) return APPLY_OK;
    return present ? APPLY_WRITE_FAILED : APPLY_NO_DEVICE;
}

// -------------------------------------------------------------- tray visuals

static HICON LoadAppIcon(int cx, int cy) {
    HICON icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   cx, cy, LR_DEFAULTCOLOR);
    // cast, not IDI_APPLICATION: that macro is ANSI unless UNICODE is defined
    if (!icon) icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    return icon;
}

static BOOL IsDarkTheme(void) {
    HKEY key;
    DWORD value = 1, size = sizeof(value), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"SystemUsesLightTheme", NULL, &type,
                             (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
            value = 1;
        RegCloseKey(key);
    }
    return value == 0;
}

enum IconTone {
    TONE_MUTED,   // grey + translucent: Fn lock off
    TONE_LIGHT    // brightened: the artwork is dark, dark taskbars swallow it
};

// Recoloured copy of an icon. Returns NULL for icons without an alpha channel
// (nothing sensible to recolour), and callers fall back to the original.
static HICON MakeIconVariant(HICON source, IconTone tone) {
    ICONINFO info;
    if (!source || !GetIconInfo(source, &info)) return NULL;

    HICON result = NULL;
    BITMAP bm;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) &&
        bm.bmWidth > 0 && bm.bmHeight > 0) {
        const int width = bm.bmWidth, height = bm.bmHeight;

        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = width;
        bi.bmiHeader.biHeight      = -height;      // top-down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(NULL);
        void *bits = NULL;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (dib && bits &&
            GetDIBits(screen, info.hbmColor, 0, (UINT)height, bits, &bi, DIB_RGB_COLORS)) {
            DWORD *pixels = (DWORD *)bits;
            const int count = width * height;

            BOOL hasAlpha = FALSE;
            for (int i = 0; i < count; ++i)
                if (pixels[i] & 0xFF000000u) { hasAlpha = TRUE; break; }

            if (hasAlpha) {
                // TONE_MUTED: greyscale pulled towards mid-grey and made
                // translucent, so "off" stays legible on light and dark bars.
                // TONE_LIGHT: keep the hue, lift every channel towards white.
                const int MID_GREY = 0x96, PULL = 60, OPACITY = 75, LIFT = 72;
                for (int i = 0; i < count; ++i) {
                    DWORD p = pixels[i];
                    int a = (int)((p >> 24) & 0xFF);
                    if (!a) { pixels[i] = 0; continue; }
                    int r = (int)((p >> 16) & 0xFF);
                    int g = (int)((p >> 8) & 0xFF);
                    int b = (int)(p & 0xFF);
                    if (tone == TONE_MUTED) {
                        int v = (r * 77 + g * 151 + b * 28) >> 8;
                        v += (MID_GREY - v) * PULL / 100;
                        r = g = b = v;
                        a = a * OPACITY / 100;
                    } else {
                        r += (255 - r) * LIFT / 100;
                        g += (255 - g) * LIFT / 100;
                        b += (255 - b) * LIFT / 100;
                    }
                    pixels[i] = ((UINT)a << 24) | ((UINT)r << 16) | ((UINT)g << 8) | (UINT)b;
                }
                ICONINFO variant;
                memset(&variant, 0, sizeof(variant));
                variant.fIcon    = TRUE;
                variant.hbmColor = dib;
                variant.hbmMask  = info.hbmMask;
                result = CreateIconIndirect(&variant);   // copies both bitmaps
            }
        }
        if (dib) DeleteObject(dib);
        ReleaseDC(NULL, screen);
    }

    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask)  DeleteObject(info.hbmMask);
    return result;
}

// Picks the on/off artwork for the current theme. Safe to call repeatedly.
static void RebuildIcons(void) {
    if (!g_iconBase) {
        g_iconBase = LoadAppIcon(GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON));
        if (!g_iconBase) return;
    }

    HICON oldOn = g_iconOn, oldOff = g_iconOff;

    HICON lightened = IsDarkTheme() ? MakeIconVariant(g_iconBase, TONE_LIGHT) : NULL;
    g_iconOn = lightened ? lightened : g_iconBase;

    HICON muted = MakeIconVariant(g_iconBase, TONE_MUTED);
    g_iconOff = muted ? muted : g_iconOn;

    if (oldOn && oldOn != g_iconBase) DestroyIcon(oldOn);
    if (oldOff && oldOff != g_iconBase && oldOff != oldOn) DestroyIcon(oldOff);
}

static void UpdateTray(BOOL add) {
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = (g_fKeysOn || !g_iconOff) ? g_iconOn : g_iconOff;

    const WCHAR *state = g_fKeysOn ? L"F1-F12 (Fn lock on)" : L"Media keys (Fn lock off)";
    if (g_deviceFound)
        wsprintfW(g_nid.szTip, L"K380: %s", state);
    else
        wsprintfW(g_nid.szTip, L"K380 not connected\n%s - will apply on reconnect", state);

    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &g_nid);
    if (add) {
        g_nid.uVersion = NOTIFYICON_VERSION;   // 128-char tooltips, classic callbacks
        Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    }
}

// ------------------------------------------------------------- app behaviour

static void ScheduleApply(HWND hwnd, int retries) {
    g_retriesLeft = retries;
    SetTimer(hwnd, TIMER_APPLY, APPLY_INTERVAL, NULL);
}

static void ApplyNow(HWND hwnd, BOOL retryOnFailure) {
    ApplyResult result = ApplyFKeys(g_fKeysOn);
    g_deviceFound = (result == APPLY_OK);
    UpdateTray(FALSE);
    if (result != APPLY_OK && retryOnFailure) ScheduleApply(hwnd, APPLY_RETRIES);
}

static void SetFKeys(HWND hwnd, BOOL on) {
    g_fKeysOn = on;
    RegWriteDword(L"FKeysOn", on ? 1u : 0u);
    ApplyNow(hwnd, TRUE);
}

static void SetHotkey(HWND hwnd, BOOL enable) {
    if (enable && !g_hotkeyOn)
        enable = RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K');
    else if (!enable && g_hotkeyOn)
        UnregisterHotKey(hwnd, HOTKEY_ID);
    g_hotkeyOn = enable;
    RegWriteDword(L"Hotkey", enable ? 1u : 0u);
}

static void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
                g_deviceFound ? L"Keyboard: connected" : L"Keyboard: not found");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_fKeysOn ? MF_CHECKED : MF_UNCHECKED), IDM_TOGGLE,
                L"F-keys act as F1-F12");
    AppendMenuW(menu, MF_STRING, IDM_REAPPLY, L"Re-apply now");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_hotkeyOn ? MF_CHECKED : MF_UNCHECKED), IDM_HOTKEY,
                L"Hotkey: Ctrl+Alt+K");
    AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_STARTUP,
                L"Run at startup");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);   // let the menu dismiss on outside clicks
    DestroyMenu(menu);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        UpdateTray(TRUE);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        RebuildIcons();

        g_nid.hWnd = hwnd;
        g_nid.uID  = 1;
        UpdateTray(TRUE);

        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);
        DEV_BROADCAST_DEVICEINTERFACE_W filter;
        memset(&filter, 0, sizeof(filter));
        filter.dbcc_size       = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid  = hidGuid;
        g_hDevNotify = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        if (g_hotkeyOn) {
            g_hotkeyOn = FALSE;
            SetHotkey(hwnd, TRUE);
        }

        ApplyNow(hwnd, TRUE);
        return 0;
    }

    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            PDEV_BROADCAST_HDR header = (PDEV_BROADCAST_HDR)lParam;
            if (header && header->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE_W iface = (PDEV_BROADCAST_DEVICEINTERFACE_W)header;
                if (!ContainsLogitechVid(iface->dbcc_name)) break;
            }
            ScheduleApply(hwnd, wParam == DBT_DEVICEARRIVAL ? APPLY_RETRIES : 2);
        }
        break;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            ScheduleApply(hwnd, APPLY_RETRIES);
        return TRUE;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_UNLOCK || wParam == WTS_SESSION_LOGON)
            ScheduleApply(hwnd, APPLY_RETRIES);
        break;

    case WM_TIMER:
        if (wParam == TIMER_APPLY) {
            BOOL done = (ApplyFKeys(g_fKeysOn) == APPLY_OK);
            if (done) g_deviceFound = TRUE;
            else if (--g_retriesLeft <= 0) { g_deviceFound = FALSE; done = TRUE; }
            if (done) KillTimer(hwnd, TIMER_APPLY);
            UpdateTray(FALSE);
        }
        break;

    case WM_SETTINGCHANGE:
        // The light/dark switch broadcasts wParam 0 + "ImmersiveColorSet".
        if (wParam == 0 && lParam && !lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet")) {
            RebuildIcons();
            UpdateTray(FALSE);
        }
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) SetFKeys(hwnd, !g_fKeysOn);
        break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP)      SetFKeys(hwnd, !g_fKeysOn);
        else if (lParam == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TOGGLE:    SetFKeys(hwnd, !g_fKeysOn); break;
        case IDM_FKEYS_ON:  SetFKeys(hwnd, TRUE);       break;
        case IDM_FKEYS_OFF: SetFKeys(hwnd, FALSE);      break;
        case IDM_REAPPLY:   ApplyNow(hwnd, TRUE);       break;
        case IDM_HOTKEY:    SetHotkey(hwnd, !g_hotkeyOn); break;
        case IDM_STARTUP:   SetStartup(!IsStartupEnabled()); break;
        case IDM_EXIT:      DestroyWindow(hwnd);        break;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_APPLY);
        if (g_hotkeyOn) UnregisterHotKey(hwnd, HOTKEY_ID);
        WTSUnRegisterSessionNotification(hwnd);
        if (g_hDevNotify) UnregisterDeviceNotification(g_hDevNotify);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_iconOff && g_iconOff != g_iconBase && g_iconOff != g_iconOn)
            DestroyIcon(g_iconOff);
        if (g_iconOn && g_iconOn != g_iconBase) DestroyIcon(g_iconOn);
        if (g_iconBase) DestroyIcon(g_iconBase);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ----------------------------------------------------------------- start-up

// Extra product IDs let K380s / Pebble Keys 2 owners run the same binary:
//   K380_FN_switch.exe --pid=0xB37C
// or HKCU\Software\K380FnSwitch\ExtraProductIds = "B37C,B36B".
static void LoadExtraPids(void) {
    WCHAR value[128];
    if (RegReadString(L"ExtraProductIds", value, 128)) {
        WCHAR token[8];
        int n = 0;
        for (const WCHAR *p = value; ; ++p) {
            if (*p == L',' || *p == L' ' || *p == L';' || *p == 0) {
                if (n) {
                    token[n] = 0;
                    USHORT pid;
                    if (ParseHexU16(token, &pid)) AddPid(pid);
                    n = 0;
                }
                if (!*p) break;
            } else if (n < 7) {
                token[n++] = *p;
            }
        }
    }
}

// Returns the WM_COMMAND id requested on the command line, or 0.
static UINT ParseCommandLine(void) {
    UINT command = 0;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 0;

    for (int i = 1; i < argc; ++i) {
        const WCHAR *arg = argv[i];
        while (*arg == L'-' || *arg == L'/') ++arg;
        if (!lstrcmpiW(arg, L"toggle"))      command = IDM_TOGGLE;
        else if (!lstrcmpiW(arg, L"on"))     command = IDM_FKEYS_ON;
        else if (!lstrcmpiW(arg, L"off"))    command = IDM_FKEYS_OFF;
        else if (!lstrcmpiW(arg, L"quit") ||
                 !lstrcmpiW(arg, L"exit"))   command = IDM_EXIT;
        else if (lstrlenW(arg) > 4 &&
                 CompareStringOrdinal(arg, 4, L"pid=", 4, TRUE) == CSTR_EQUAL) {
            USHORT pid;
            if (ParseHexU16(arg + 4, &pid)) AddPid(pid);
        }
    }

    LocalFree(argv);
    return command;
}

static int RunApp(HINSTANCE instance) {
    g_hInst = instance;

    SetProcessDPIAware();
    LoadExtraPids();
    UINT command = ParseCommandLine();

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(CLASS_NAME, NULL);
        if (running && command) PostMessageW(running, WM_COMMAND, command, 0);
        return 0;
    }

    g_fKeysOn  = RegReadDword(L"FKeysOn", 1) != 0;
    g_hotkeyOn = RegReadDword(L"Hotkey", 0) != 0;
    if (command == IDM_FKEYS_ON)  g_fKeysOn = TRUE;
    if (command == IDM_FKEYS_OFF) g_fKeysOn = FALSE;
    if (command == IDM_TOGGLE)    g_fKeysOn = !g_fKeysOn;

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = instance;
    wc.hIcon         = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&wc)) return 1;

    // A real (never shown) top-level window is required: message-only windows
    // do not receive the TaskbarCreated and power broadcasts.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, CLASS_NAME, L"K380 FN Switch",
                                WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!hwnd) return 1;

    if (g_msgTaskbarCreated)
        ChangeWindowMessageFilterEx(hwnd, g_msgTaskbarCreated, MSGFLT_ALLOW, NULL);

    MSG msg;
    memset(&msg, 0, sizeof(msg));
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {   // -1 (error) also ends the loop
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

extern "C" void AppEntryPoint(void) {
    ExitProcess((UINT)RunApp(GetModuleHandleW(NULL)));
}

// Kept so the file also builds the classic way:
//   rc app.rc && cl K380_FN_switch.cpp app.res /O2 /link /SUBSYSTEM:WINDOWS
extern "C" int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    return RunApp(instance);
}
