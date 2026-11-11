#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <xinput.h>
#include <stdint.h>
#include <string.h>
#include "world_window.h"
#include "world_border.h"

enum { WIDTH = 320, HEIGHT = 240 };
static const wchar_t window_class[] = L"InTheHuntNativeViewport";
static HWND window;
static BITMAPINFO bitmap;
static uint32_t frame[WIDTH * HEIGHT];
static unsigned char keys[256];
static HMODULE controller_library;
typedef DWORD (WINAPI *ControllerState)(DWORD, XINPUT_STATE *);
static ControllerState controller_state;
static DWORD retry_at[2];
static WORD pad_buttons[2];
static SHORT pad_lx[2], pad_ly[2];
static int last_device_pad;
static int fullscreen;
static RECT windowed_rect;

static int smaller(int x, int y) { return x < y ? x : y; }

static void paint(HWND hwnd, HDC dc)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int scale = smaller(rect.right / WIDTH, rect.bottom / HEIGHT);
    if (scale < 1) scale = 1;
    int w = WIDTH * scale, h = HEIGHT * scale;
    int x = (rect.right - w) / 2, y = (rect.bottom - h) / 2;
    world_border_paint(dc, rect.right, rect.bottom);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, WIDTH, HEIGHT, frame, &bitmap,
                  DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        paint(hwnd, (HDC)wp);
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        RECT minimum = { 0, 0, WIDTH, HEIGHT };
        AdjustWindowRect(&minimum, WS_OVERLAPPEDWINDOW, FALSE);
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = minimum.right - minimum.left;
        info->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (wp < sizeof keys) {
            if (message == WM_KEYDOWN && !keys[wp]) last_device_pad = 0;
            keys[wp] = message == WM_KEYDOWN;
        }
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        /* F10 and Alt arrive as system keys; keep them out of the menu bar. */
        if (wp == VK_F10 && wp < sizeof keys) { keys[wp] = message == WM_SYSKEYDOWN; return 0; }
        return DefWindowProcW(hwnd, message, wp, lp);
    case WM_KILLFOCUS:
        memset(keys, 0, sizeof keys);
        return 0;
    case 0x02e0: { /* WM_DPICHANGED, including builds with older SDK headers. */
        const RECT *bounds = (const RECT *)lp;
        SetWindowPos(hwnd, NULL, bounds->left, bounds->top,
                     bounds->right - bounds->left, bounds->bottom - bounds->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_DESTROY:
        window = NULL;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wp, lp);
    }
}

static void open_controllers(void)
{
    static const wchar_t *const names[] = { L"\\xinput1_4.dll", L"\\xinput9_1_0.dll" };
    wchar_t path[MAX_PATH];
    UINT prefix = GetSystemDirectoryW(path, MAX_PATH);
    if (!prefix || prefix >= MAX_PATH - 20) return;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        lstrcpyW(path + prefix, names[i]);
        controller_library = LoadLibraryW(path);
        if (!controller_library) continue;
        controller_state = (ControllerState)(void *)GetProcAddress(controller_library, "XInputGetState");
        if (controller_state) return;
        FreeLibrary(controller_library);
        controller_library = NULL;
    }
}

int world_window_open(void)
{
    if (window) return 1;
    typedef BOOL (WINAPI *SetDpiContext)(HANDLE);
    SetDpiContext set_dpi_context = (SetDpiContext)(void *)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    typedef BOOL (WINAPI *SetDpiAware)(void);
    SetDpiAware set_dpi_aware = (SetDpiAware)(void *)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
    if ((!set_dpi_context || !set_dpi_context((HANDLE)(INT_PTR)-4)) && set_dpi_aware)
        set_dpi_aware();
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = window_class;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;

    memset(frame, 0, sizeof frame);
    memset(keys, 0, sizeof keys);
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = WIDTH;
    bitmap.bmiHeader.biHeight = -HEIGHT;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;

    RECT work;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
        SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    RECT decoration = { 0, 0, 0, 0 };
    AdjustWindowRect(&decoration, WS_OVERLAPPEDWINDOW, FALSE);
    int dw = decoration.right - decoration.left, dh = decoration.bottom - decoration.top;
    int scale = smaller(3, smaller((work.right - work.left - dw) / WIDTH,
                                   (work.bottom - work.top - dh) / HEIGHT));
    if (scale < 1) scale = 1;
    int w = WIDTH * scale + dw, h = HEIGHT * scale + dh;
    window = CreateWindowExW(0, window_class, L"In the Hunt", WS_OVERLAPPEDWINDOW,
                             (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2,
                             w, h, NULL, NULL, wc.hInstance, NULL);
    if (!window) return 0;
    open_controllers();
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    return 1;
}

static void poll_controller(unsigned player, uint16_t *held, uint16_t *buttons)
{
    if (!controller_state) return;
    DWORD now = GetTickCount();
    if (retry_at[player] && (LONG)(now - retry_at[player]) < 0) return;
    XINPUT_STATE state;
    if (controller_state(player, &state) != ERROR_SUCCESS) {
        retry_at[player] = now + 1000;
        return;
    }
    retry_at[player] = 0;
    XINPUT_GAMEPAD *pad = &state.Gamepad;
    WORD pressed = pad->wButtons;
    if ((pressed & ~pad_buttons[player]) || (pad->sThumbLX > 12000 && pad_lx[player] <= 12000) ||
        (pad->sThumbLX < -12000 && pad_lx[player] >= -12000) ||
        (pad->sThumbLY > 12000 && pad_ly[player] <= 12000) ||
        (pad->sThumbLY < -12000 && pad_ly[player] >= -12000)) last_device_pad = 1;
    pad_buttons[player] = pressed;
    pad_lx[player] = pad->sThumbLX;
    pad_ly[player] = pad->sThumbLY;
    unsigned value = 0;
    if ((pressed & XINPUT_GAMEPAD_DPAD_RIGHT) || pad->sThumbLX > 12000) value |= 1;
    if ((pressed & XINPUT_GAMEPAD_DPAD_LEFT) || pad->sThumbLX < -12000) value |= 2;
    if ((pressed & XINPUT_GAMEPAD_DPAD_DOWN) || pad->sThumbLY < -12000) value |= 4;
    if ((pressed & XINPUT_GAMEPAD_DPAD_UP) || pad->sThumbLY > 12000) value |= 8;
    if (pressed & XINPUT_GAMEPAD_A) value |= 0x80;
    if (pressed & XINPUT_GAMEPAD_B) value |= 0x40;
    *held |= (uint16_t)(value << (player * 8));
    if (pressed & XINPUT_GAMEPAD_START) *buttons |= (uint16_t)(1 << player);
    if (pressed & XINPUT_GAMEPAD_BACK) *buttons |= (uint16_t)(4 << player);
}

int world_window_poll(uint16_t *pads, uint16_t *system)
{
    *pads = *system = 0xffff;
    if (!window) return 0;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { world_window_close(); return 0; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!window) return 0;
    uint16_t held = keys[VK_RIGHT] | keys[VK_LEFT] << 1 | keys[VK_DOWN] << 2 |
                    keys[VK_UP] << 3 | keys['X'] << 6 | keys['Z'] << 7;
    held |= (uint16_t)((keys['D'] | keys['A'] << 1 | keys['S'] << 2 |
                       keys['W'] << 3 | keys['H'] << 6 | keys['G'] << 7) << 8);
    uint16_t buttons = keys['1'] | keys['2'] << 1 | keys['5'] << 2 | keys['6'] << 3;
    if (GetForegroundWindow() == window)
        for (unsigned player = 0; player < 2; ++player) poll_controller(player, &held, &buttons);
    else
        memset(pad_buttons, 0, sizeof pad_buttons), memset(pad_lx, 0, sizeof pad_lx), memset(pad_ly, 0, sizeof pad_ly);
    *pads = (uint16_t)~held;
    *system = (uint16_t)~buttons;
    return 1;
}

void world_window_present(const uint32_t *pixels)
{
    if (!window || !pixels) return;
    memcpy(frame, pixels, sizeof frame);
    if (!IsIconic(window)) {
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
    }
}

int world_window_set_border(const char *path, unsigned brightness)
{
    if (!world_border_set(path, brightness)) return 0;
    if (window) InvalidateRect(window, NULL, FALSE);
    return 1;
}

void world_window_close(void)
{
    if (window) DestroyWindow(window);
    if (controller_library) FreeLibrary(controller_library);
    controller_state = NULL;
    controller_library = NULL;
    memset(retry_at, 0, sizeof retry_at);
    world_border_close();
}

/* Menu navigation from either keyboard or any pad, as held levels:
 * up 1, down 2, left 4, right 8, accept 16, back 32, page back 64, page on 128.
 * The toggle level is F1, Esc, or L3 and R3 held together. */
unsigned world_window_menu_buttons(unsigned *toggle, int *gamepad)
{
    unsigned held = 0;
    held |= (keys[VK_UP] | keys['W']) ? 1 : 0;
    held |= (keys[VK_DOWN] | keys['S']) ? 2 : 0;
    held |= (keys[VK_LEFT] | keys['A']) ? 4 : 0;
    held |= (keys[VK_RIGHT] | keys['D']) ? 8 : 0;
    held |= (keys[VK_RETURN] | keys[VK_SPACE] | keys['Z']) ? 16 : 0;
    held |= (keys[VK_BACK] | keys['X']) ? 32 : 0;
    held |= (keys['Q'] | keys[VK_PRIOR]) ? 64 : 0;
    held |= (keys['E'] | keys[VK_NEXT] | keys[VK_TAB]) ? 128 : 0;
    unsigned toggle_level = keys[VK_F1] ? 1 : 0;
    unsigned escape = keys[VK_ESCAPE] ? 1 : 0;
    for (unsigned player = 0; player < 2; ++player) {
        WORD b = pad_buttons[player];
        held |= ((b & XINPUT_GAMEPAD_DPAD_UP) || pad_ly[player] > 12000) ? 1 : 0;
        held |= ((b & XINPUT_GAMEPAD_DPAD_DOWN) || pad_ly[player] < -12000) ? 2 : 0;
        held |= ((b & XINPUT_GAMEPAD_DPAD_LEFT) || pad_lx[player] < -12000) ? 4 : 0;
        held |= ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || pad_lx[player] > 12000) ? 8 : 0;
        held |= (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) ? 16 : 0;
        held |= (b & XINPUT_GAMEPAD_B) ? 32 : 0;
        held |= (b & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 64 : 0;
        held |= (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 128 : 0;
        if ((b & XINPUT_GAMEPAD_LEFT_THUMB) && (b & XINPUT_GAMEPAD_RIGHT_THUMB)) toggle_level = 1;
    }
    if (toggle) *toggle = toggle_level | escape << 1;
    if (gamepad) *gamepad = last_device_pad;
    return held;
}

/* Borderless full screen on the window's monitor, or a window whose client
 * area is scale times 320x240 (clamped to the work area). */
void world_window_set_display(int full, unsigned scale)
{
    if (!window) return;
    if (full && !fullscreen) GetWindowRect(window, &windowed_rect);
    fullscreen = full;
    if (full) {
        MONITORINFO info;
        info.cbSize = sizeof info;
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return;
    }
    SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    MONITORINFO info;
    info.cbSize = sizeof info;
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
    RECT work = info.rcWork, frame_rect = {0, 0, 0, 0};
    AdjustWindowRect(&frame_rect, WS_OVERLAPPEDWINDOW, FALSE);
    int dw = frame_rect.right - frame_rect.left, dh = frame_rect.bottom - frame_rect.top;
    int fit = smaller((work.right - work.left - dw) / WIDTH, (work.bottom - work.top - dh) / HEIGHT);
    if (scale < 1) scale = 1;
    if ((int)scale > fit && fit >= 1) scale = (unsigned)fit;
    int w = WIDTH * (int)scale + dw, h = HEIGHT * (int)scale + dh;
    SetWindowPos(window, HWND_NOTOPMOST, (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2,
                 w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}
