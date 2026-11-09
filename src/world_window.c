#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <commdlg.h>
#include <xinput.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "world_window.h"
#include "world_border.h"
#include "world_crt.h"
#include "world_present.h"

enum { WIDTH = 320, HEIGHT = 240, MAX_SCALE = 12 };
static const wchar_t window_class[] = L"InTheHuntNativeViewport";
static HWND window;
static BITMAPINFO bitmap;
static uint32_t frame[WIDTH * HEIGHT];
static WorldInput input;
static HMODULE controller_library;
typedef DWORD (WINAPI *ControllerState)(DWORD, XINPUT_STATE *);
static ControllerState controller_state;
static DWORD retry_at[2];
static int fullscreen;
static unsigned windowed_scale;
static int scale_pending;
static WINDOWPLACEMENT windowed_placement;
/* Direct3D 11 presents when it initialises; GDI is the fallback. */
static int direct3d, vsync_enabled, effect_loaded;
static WorldCrtProfile crt_profile = WORLD_CRT_PIXELS;
static WorldCrtConstants crt_constants;
static const uint32_t *uploaded_border;
static int menu_requested;

static int smaller(int x, int y) { return x < y ? x : y; }

/* The largest whole-number picture that fits, never below one. */
static int fitted_scale(int width, int height)
{
    int scale = smaller(width / WIDTH, height / HEIGHT);
    return scale < 1 ? 1 : scale;
}

static void paint(HWND hwnd, HDC dc)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    int scale = fitted_scale(rect.right, rect.bottom);
    int w = WIDTH * scale, h = HEIGHT * scale;
    int x = (rect.right - w) / 2, y = (rect.bottom - h) / 2;
    world_border_paint(dc, rect.right, rect.bottom);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, WIDTH, HEIGHT, frame, &bitmap,
                  DIB_RGB_COLORS, SRCCOPY);
}

/* The CRT pass needs the viewport it is drawn into. world_present_draw
   places the picture with the same whole-number rule as fitted_scale. */
static int refresh_crt_constants(int force)
{
    RECT rect;
    if (!GetClientRect(window, &rect) || rect.right <= 0 || rect.bottom <= 0) return 1;
    int scale = fitted_scale(rect.right, rect.bottom);
    WorldCrtConstants next;
    world_crt_constants(&next, crt_profile, (unsigned)(WIDTH * scale), (unsigned)(HEIGHT * scale),
                        (unsigned)((rect.right - WIDTH * scale) / 2),
                        (unsigned)((rect.bottom - HEIGHT * scale) / 2));
    if (!force && !memcmp(&next, &crt_constants, sizeof next)) return 1;
    crt_constants = next;
    return world_present_set_constants(&crt_constants, sizeof crt_constants);
}

/* Select the pixel shader for crt_profile on the open presenter. */
static int apply_shader(void)
{
    if (crt_profile == WORLD_CRT_PIXELS) {
        effect_loaded = 0;
        return world_present_set_shader(NULL, NULL, NULL, 0);
    }
    if (effect_loaded) return refresh_crt_constants(1);
    memset(&crt_constants, 0, sizeof crt_constants);
    if (!world_present_set_shader(world_crt_source(), "main", &crt_constants, sizeof crt_constants)) {
        fprintf(stderr, "CRT shader was rejected: %s\n", world_present_error());
        return 0;
    }
    effect_loaded = 1;
    return refresh_crt_constants(1);
}

static int upload_border(void)
{
    unsigned width, height, brightness;
    const uint32_t *pixels = world_border_pixels(&width, &height, &brightness);
    if (pixels && pixels == uploaded_border) {
        world_present_set_background_brightness(brightness);
        return 1;
    }
    if (!world_present_set_background(pixels, width, height, brightness)) return 0;
    uploaded_border = pixels;
    return 1;
}

static int open_direct3d(void)
{
    direct3d = effect_loaded = 0;
    uploaded_border = NULL;
    if (!world_present_open(window)) return 0;
    direct3d = 1;
    if (!apply_shader()) {
        crt_profile = WORLD_CRT_PIXELS;
        apply_shader();
    }
    if (!upload_border()) fprintf(stderr, "Border artwork was not accepted by Direct3D: %s\n", world_present_error());
    return 1;
}

/* Draw the latest frame. A lost device is rebuilt once; if that fails the
   window continues on GDI. */
static void present_direct3d(int vsync)
{
    if (crt_profile != WORLD_CRT_PIXELS) refresh_crt_constants(0);
    if (world_present_draw(frame, vsync)) return;
    fprintf(stderr, "Direct3D presentation failed: %s\n", world_present_error());
    if (open_direct3d() && world_present_draw(frame, vsync)) return;
    world_present_close();
    direct3d = effect_loaded = 0;
    uploaded_border = NULL;
    fprintf(stderr, "Continuing with the GDI renderer.\n");
    InvalidateRect(window, NULL, FALSE);
}

static void work_area(RECT *work, RECT *monitor)
{
    MONITORINFO info;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    HMONITOR handle = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : NULL;
    if (handle && GetMonitorInfoW(handle, &info)) {
        *work = info.rcWork;
        if (monitor) *monitor = info.rcMonitor;
        return;
    }
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, work, 0))
        SetRect(work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (monitor) SetRect(monitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

/* Outer window rectangle for a windowed picture, centered on the work area.
 * Returns the scale after clamping it to what the desktop can hold. */
static unsigned windowed_rect(unsigned scale, RECT *outer)
{
    RECT work, decoration = { 0, 0, 0, 0 };
    work_area(&work, NULL);
    AdjustWindowRect(&decoration, WS_OVERLAPPEDWINDOW, FALSE);
    int dw = decoration.right - decoration.left, dh = decoration.bottom - decoration.top;
    int fit = fitted_scale(work.right - work.left - dw, work.bottom - work.top - dh);
    if (scale < 1) scale = 1;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    if ((int)scale > fit) scale = (unsigned)fit;
    int w = WIDTH * (int)scale + dw, h = HEIGHT * (int)scale + dh;
    outer->left = (work.left + work.right - w) / 2;
    outer->top = (work.top + work.bottom - h) / 2;
    outer->right = outer->left + w;
    outer->bottom = outer->top + h;
    return scale;
}

static void cover_monitor(void)
{
    RECT work, monitor;
    work_area(&work, &monitor);
    SetWindowPos(window, HWND_TOP, monitor.left, monitor.top,
                 monitor.right - monitor.left, monitor.bottom - monitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (!direct3d) paint(hwnd, dc);
        EndPaint(hwnd, &ps);
        if (direct3d && hwnd == window) present_direct3d(0);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        if (!direct3d) paint(hwnd, (HDC)wp);
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
        /* Latch the first Esc press so a tap between polls still opens the menu. */
        if (wp == VK_ESCAPE && message == WM_KEYDOWN && !(lp & 0x40000000)) menu_requested = 1;
        world_input_key(&input, (unsigned)wp, message == WM_KEYDOWN);
        return 0;
    case WM_SETFOCUS:
        world_input_set_focus(&input, 1);
        return 0;
    case WM_KILLFOCUS:
        world_input_set_focus(&input, 0);
        return 0;
    case 0x02e0: { /* WM_DPICHANGED, including builds with older SDK headers. */
        const RECT *bounds = (const RECT *)lp;
        if (fullscreen) cover_monitor();
        else SetWindowPos(hwnd, NULL, bounds->left, bounds->top,
                          bounds->right - bounds->left, bounds->bottom - bounds->top,
                          SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_DESTROY:
        if (direct3d) world_present_close();
        direct3d = effect_loaded = 0;
        uploaded_border = NULL;
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

void world_window_set_fullscreen(int enabled)
{
    enabled = !!enabled;
    if (!window || enabled == fullscreen) return;
    fullscreen = enabled;
    if (enabled) {
        windowed_placement.length = sizeof windowed_placement;
        if (!GetWindowPlacement(window, &windowed_placement)) windowed_placement.length = 0;
        SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        cover_monitor();
    } else {
        SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        if (windowed_placement.length) SetWindowPlacement(window, &windowed_placement);
        SetWindowPos(window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        /* A scale chosen while fullscreen takes effect on the way back. */
        if (scale_pending) world_window_set_scale(windowed_scale);
    }
    InvalidateRect(window, NULL, FALSE);
}

unsigned world_window_set_scale(unsigned scale)
{
    RECT outer;
    windowed_scale = windowed_rect(scale, &outer);
    scale_pending = window && fullscreen;
    if (window && !fullscreen) {
        if (IsZoomed(window) || IsIconic(window)) ShowWindow(window, SW_RESTORE);
        SetWindowPos(window, NULL, outer.left, outer.top, outer.right - outer.left,
                     outer.bottom - outer.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return windowed_scale;
}

int world_window_open(const WorldWindowOptions *options, const WorldInputBindings *bindings)
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
    world_input_init(&input, bindings);
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = WIDTH;
    bitmap.bmiHeader.biHeight = -HEIGHT;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;

    RECT outer;
    fullscreen = scale_pending = menu_requested = 0;
    windowed_placement.length = 0;
    windowed_scale = windowed_rect(options ? options->window_scale : 3, &outer);
    window = CreateWindowExW(0, window_class, L"In the Hunt", WS_OVERLAPPEDWINDOW,
                             outer.left, outer.top, outer.right - outer.left,
                             outer.bottom - outer.top, NULL, NULL, wc.hInstance, NULL);
    if (!window) return 0;
    open_controllers();
    vsync_enabled = options && options->vsync;
    crt_profile = WORLD_CRT_PIXELS;
    if (options && options->shader) world_crt_parse(options->shader, &crt_profile);
    if (!open_direct3d())
        fprintf(stderr, "Direct3D 11 is unavailable (%s). Using the GDI renderer.\n", world_present_error());
    ShowWindow(window, SW_SHOWNORMAL);
    if (options && options->fullscreen) world_window_set_fullscreen(1);
    UpdateWindow(window);
    return 1;
}

static void poll_controller(unsigned player)
{
    if (!controller_state) return;
    DWORD now = GetTickCount();
    if (retry_at[player] && (LONG)(now - retry_at[player]) < 0) return;
    XINPUT_STATE state;
    if (controller_state(player, &state) != ERROR_SUCCESS) {
        retry_at[player] = now + 1000;
        world_input_controller(&input, player, NULL);
        return;
    }
    retry_at[player] = 0;
    WorldInputController pad;
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.buttons = state.Gamepad.wButtons;
    pad.left_x = state.Gamepad.sThumbLX;
    pad.left_y = state.Gamepad.sThumbLY;
    pad.right_x = state.Gamepad.sThumbRX;
    pad.right_y = state.Gamepad.sThumbRY;
    pad.left_trigger = state.Gamepad.bLeftTrigger;
    pad.right_trigger = state.Gamepad.bRightTrigger;
    world_input_controller(&input, player, &pad);
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
    /* world_input drops controller state while the window lacks focus. */
    for (unsigned player = 0; player < WORLD_INPUT_PLAYERS; ++player) poll_controller(player);
    world_input_ports(&input, pads, system);
    return 1;
}

WorldInput *world_window_input(void) { return &input; }

int world_window_take_menu_request(void)
{
    int requested = menu_requested;
    menu_requested = 0;
    return requested;
}

int world_window_pick_image(char *path, size_t capacity)
{
    wchar_t chosen[1024] = L"";
    OPENFILENAMEW dialog;
    memset(&dialog, 0, sizeof dialog);
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Pictures (PNG, JPEG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
    dialog.lpstrFile = chosen;
    dialog.nMaxFile = sizeof chosen / sizeof chosen[0];
    dialog.lpstrTitle = L"Choose border artwork";
    /* The game opens its data relative to the working directory. */
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!window || !path || !capacity || !GetOpenFileNameW(&dialog)) return 0;
    int length = WideCharToMultiByte(CP_UTF8, 0, chosen, -1, NULL, 0, NULL, NULL);
    if (length <= 0 || (size_t)length > capacity) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, chosen, -1, path, (int)capacity, NULL, NULL) > 0;
}

void world_window_present(const uint32_t *pixels, int allow_vsync)
{
    if (!window || !pixels) return;
    memcpy(frame, pixels, sizeof frame);
    if (IsIconic(window)) return;
    if (direct3d) present_direct3d(vsync_enabled && allow_vsync);
    else {
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
    }
}

int world_window_set_border(const char *path, unsigned brightness)
{
    if (!world_border_set(path, brightness)) return 0;
    if (direct3d && !upload_border()) {
        /* Keep both renderers on the same, black, selection. */
        world_border_set("", brightness);
        upload_border();
        return 0;
    }
    if (window) InvalidateRect(window, NULL, FALSE);
    return 1;
}

int world_window_direct3d(void) { return direct3d; }

const char *world_window_shader(void) { return world_crt_id(crt_profile); }

void world_window_set_vsync(int enabled) { vsync_enabled = !!enabled; }

int world_window_set_shader(const char *id)
{
    WorldCrtProfile profile, previous = crt_profile;
    if (!direct3d || !world_crt_parse(id, &profile)) return 0;
    crt_profile = profile;
    if (apply_shader()) return 1;
    crt_profile = previous;
    apply_shader();
    return 0;
}

void world_window_close(void)
{
    if (window) DestroyWindow(window);
    if (direct3d) world_present_close();
    direct3d = effect_loaded = 0;
    uploaded_border = NULL;
    if (controller_library) FreeLibrary(controller_library);
    controller_state = NULL;
    controller_library = NULL;
    memset(retry_at, 0, sizeof retry_at);
    fullscreen = 0;
    world_border_close();
}
