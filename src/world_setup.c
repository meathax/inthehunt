/* Native first-run screen and embedded, hash-checked ROM importer. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <wincodec.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include "world_setup.h"

typedef struct { HANDLE process; wchar_t temporary[MAX_PATH]; } Import;
static wchar_t root[32768];
static const wchar_t *files[] = {L"world_import.ps1", L"recomplauncher_setup.ps1", L"world_regions.json"};

static int installation(void)
{
    if (root[0]) return 1;
    DWORD n = GetModuleFileNameW(NULL, root, 32768);
    if (!n || n >= 32768) return 0;
    wchar_t *end = wcsrchr(root, L'\\');
    if (!end) return 0;
    *end = 0;
    size_t len = wcslen(root), suffix = wcslen(L"\\.local\\native");
    if (len > suffix && !_wcsicmp(root + len - suffix, L"\\.local\\native")) root[len - suffix] = 0;
    return 1;
}

const char *world_default_directory(void)
{
    static char path[4096];
    wchar_t wide[32768];
    if (!installation() || swprintf(wide, 32768, L"%ls\\.local\\world", root) < 0 ||
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, path, sizeof path, NULL, NULL)) return NULL;
    return path;
}

static void import_close(Import *job)
{
    if (job->process) { CloseHandle(job->process); job->process = NULL; }
    if (job->temporary[0]) {
        wchar_t file[MAX_PATH];
        for (unsigned i = 0; i < 3; ++i) {
            swprintf(file, MAX_PATH, L"%ls\\%ls", job->temporary, files[i]);
            DeleteFileW(file);
        }
        RemoveDirectoryW(job->temporary);
        job->temporary[0] = 0;
    }
}

static int import_start(Import *job, const wchar_t *archive, const wchar_t *directory)
{
    wchar_t temp[MAX_PATH], file[MAX_PATH], system[MAX_PATH], command[2048], guid[40];
    GUID id;
    if (!installation() || !GetTempPathW(MAX_PATH, temp) || FAILED(CoCreateGuid(&id))) return 0;
    StringFromGUID2(&id, guid, 40);
    if (swprintf(job->temporary, MAX_PATH, L"%lsInTheHunt-%ls", temp, guid) < 0 ||
        !CreateDirectoryW(job->temporary, NULL)) { job->temporary[0] = 0; return 0; }
    for (unsigned i = 0; i < 3; ++i) {
        HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(201 + i), (LPCWSTR)RT_RCDATA);
        HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
        const void *bytes = loaded ? LockResource(loaded) : NULL;
        DWORD size = resource ? SizeofResource(NULL, resource) : 0, written = 0;
        if (!bytes || !size) goto failed;
        swprintf(file, MAX_PATH, L"%ls\\%ls", job->temporary, files[i]);
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) goto failed;
        int ok = WriteFile(f, bytes, size, &written, NULL) && written == size;
        CloseHandle(f);
        if (!ok) goto failed;
    }
    if (!GetSystemDirectoryW(system, MAX_PATH)) goto failed;
    if (swprintf(command, 2048, L"\"%ls\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\\recomplauncher_setup.ps1\"", system, job->temporary) < 0) goto failed;
    /* Paths are inherited Unicode data, never interpolated shell expressions. */
    if (!SetEnvironmentVariableW(L"INTHUNT_IMPORT_ARCHIVE", archive) ||
        !SetEnvironmentVariableW(L"INTHUNT_IMPORT_ROOT", root) ||
        !SetEnvironmentVariableW(L"INTHUNT_IMPORT_DEST", directory)) goto failed;
    STARTUPINFOW si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof si;
    int ok = CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, root, &si, &pi);
    SetEnvironmentVariableW(L"INTHUNT_IMPORT_ARCHIVE", NULL);
    SetEnvironmentVariableW(L"INTHUNT_IMPORT_ROOT", NULL);
    SetEnvironmentVariableW(L"INTHUNT_IMPORT_DEST", NULL);
    if (!ok) goto failed;
    CloseHandle(pi.hThread); job->process = pi.hProcess;
    return 1;
failed:
    import_close(job);
    return 0;
}

int world_setup_import(const wchar_t *archive)
{
    Import job = {0}; DWORD result = 1;
    if (!archive || !archive[0] || !import_start(&job, archive, NULL)) return 1;
    WaitForSingleObject(job.process, INFINITE);
    GetExitCodeProcess(job.process, &result);
    import_close(&job);
    return result ? 1 : 0;
}

typedef struct {
    Import job;
    HWND browse, quit;
    HFONT font;
    wchar_t directory[32768];
    const wchar_t *status;
    int done, success, busy;
    BYTE *art;
    UINT art_width, art_height;
} Setup;

static void load_art(Setup *s)
{
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return;
    IWICImagingFactory *factory = NULL; IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL; IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(204), (LPCWSTR)RT_RCDATA);
    HGLOBAL data = resource ? LoadResource(NULL, resource) : NULL;
    HRESULT hr = data ? CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory,(void **)&factory) : E_FAIL;
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateStream(factory,&stream);
    if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromMemory(stream,LockResource(data),SizeofResource(NULL,resource));
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateDecoderFromStream(factory,(IStream *)stream,NULL,WICDecodeMetadataCacheOnLoad,&decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder,0,&frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame,&s->art_width,&s->art_height);
    if (SUCCEEDED(hr) && (!s->art_width || !s->art_height || s->art_width>4096 || s->art_height>4096)) hr=E_FAIL;
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(factory,&converter);
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)frame,
        &GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        UINT size=s->art_width*s->art_height*4;
        s->art=malloc(size);
        hr=s->art ? IWICFormatConverter_CopyPixels(converter,NULL,s->art_width*4,size,s->art) : E_OUTOFMEMORY;
    }
    if (FAILED(hr)) { free(s->art); s->art=NULL; }
    if(converter) IWICFormatConverter_Release(converter);
    if(frame) IWICBitmapFrameDecode_Release(frame);
    if(decoder) IWICBitmapDecoder_Release(decoder);
    if(stream) IWICStream_Release(stream);
    if(factory) IWICImagingFactory_Release(factory);
    if(SUCCEEDED(initialized)) CoUninitialize();
}

static void label(HDC dc, const wchar_t *text, int x, int y, int w, int h, int size, COLORREF color, int bold)
{
    HFONT font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font); RECT r = {x,y,x+w,y+h};
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(font);
}

static void paint(HWND window, Setup *s)
{
    PAINTSTRUCT ps; HDC target = BeginPaint(window, &ps); RECT r; GetClientRect(window, &r);
    HDC dc = CreateCompatibleDC(target); HBITMAP bitmap = CreateCompatibleBitmap(target, r.right, r.bottom);
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH background=CreateSolidBrush(RGB(5,20,31)); FillRect(dc,&r,background); DeleteObject(background);
    int art_height=r.bottom*470/700;
    if(s->art) {
        BITMAPINFO info={0}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=s->art_width; info.bmiHeader.biHeight=-(LONG)s->art_height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        /* Cover the artwork area without stretching; keep the submarine trio in view. */
        int sw=s->art_width, sh=s->art_height;
        if((long long)sw*art_height > (long long)sh*r.right) sw=sh*r.right/art_height;
        else sh=sw*art_height/r.right;
        SetStretchBltMode(dc,COLORONCOLOR);
        StretchDIBits(dc,0,0,r.right,art_height,(s->art_width-sw)/2,(s->art_height-sh)/2,
            sw,sh,s->art,&info,DIB_RGB_COLORS,SRCCOPY);
    }
    SetMapMode(dc, MM_ANISOTROPIC); SetWindowExtEx(dc,880,700,NULL); SetViewportExtEx(dc,r.right,r.bottom,NULL);
    label(dc,L"IN THE HUNT",33,25,800,60,42,RGB(3,22,42),1);
    label(dc,L"IN THE HUNT",31,23,800,60,42,RGB(255,233,164),1);
    label(dc,L"Choose your ROM",32,490,816,38,28,RGB(244,241,221),1);
    label(dc,L"World edition  /  inthunt.zip or inthunt.7z",32,535,816,28,18,RGB(151,201,211),0);
    label(dc,s->status,32,574,816,45,16,s->busy?RGB(244,220,149):RGB(199,222,222),0);
    SetMapMode(dc, MM_TEXT); BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); EndPaint(window,&ps);
}

static LRESULT CALLBACK procedure(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    Setup *s = (Setup *)GetWindowLongPtrW(w,GWLP_USERDATA);
    if (message == WM_NCCREATE) { s = ((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)s); }
    if (!s) return DefWindowProcW(w,message,wp,lp);
    switch(message) {
    case WM_CREATE:
        load_art(s);
        s->browse = CreateWindowW(L"BUTTON",L"Browse for ROM...",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,0,0,w,(HMENU)1,NULL,NULL);
        s->quit = CreateWindowW(L"BUTTON",L"Exit",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,w,(HMENU)2,NULL,NULL);
        SetFocus(s->browse); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize=(POINT){640,550}; return 0;
    case WM_SIZE: {
        RECT r; GetClientRect(w,&r); int x=r.right, y=r.bottom;
        MoveWindow(s->browse,x*32/880,y*632/700,x*280/880,y*44/700,TRUE);
        MoveWindow(s->quit,x*738/880,y*632/700,x*110/880,y*44/700,TRUE);
        HFONT font=CreateFontW(-18*y/700,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        SendMessageW(s->browse,WM_SETFONT,(WPARAM)font,TRUE); SendMessageW(s->quit,WM_SETFONT,(WPARAM)font,TRUE);
        if(s->font) DeleteObject(s->font); s->font=font; InvalidateRect(w,NULL,FALSE); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(w,s); return 0;
    case WM_COMMAND:
        if (LOWORD(wp)==2 || LOWORD(wp)==IDCANCEL) { SendMessageW(w,WM_CLOSE,0,0); return 0; }
        if (LOWORD(wp)==1 && !s->busy) {
            wchar_t archive[32768]=L"";
            OPENFILENAMEW pick={0}; pick.lStructSize=sizeof pick; pick.hwndOwner=w;
            pick.lpstrFilter=L"World ROM archive (*.zip; *.7z)\0*.zip;*.7z\0All files\0*.*\0";
            pick.lpstrFile=archive; pick.nMaxFile=32768; pick.lpstrTitle=L"Choose your World In the Hunt ROM";
            pick.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
            if(!GetOpenFileNameW(&pick)) return 0;
            s->busy=import_start(&s->job,archive,s->directory);
            s->status=s->busy?L"Importing and checking your ROM. The game will start automatically...":L"Could not start import. Extract the game to a writable folder and try again.";
            if(s->busy) { EnableWindow(s->browse,FALSE); EnableWindow(s->quit,FALSE); SetWindowTextW(s->browse,L"Importing..."); SetTimer(w,1,100,NULL); }
            InvalidateRect(w,NULL,FALSE); return 0;
        } break;
    case WM_TIMER:
        if(s->job.process && WaitForSingleObject(s->job.process,0)==WAIT_OBJECT_0) {
            DWORD result=1; GetExitCodeProcess(s->job.process,&result); import_close(&s->job); KillTimer(w,1); s->busy=0;
            if(!result) { s->success=1; DestroyWindow(w); }
            else { s->status=L"Import failed. Use the World ROM set and a writable game folder, then try again.";
                EnableWindow(s->browse,TRUE); EnableWindow(s->quit,TRUE); SetWindowTextW(s->browse,L"Browse for ROM..."); SetFocus(s->browse); InvalidateRect(w,NULL,FALSE); }
        } return 0;
    case WM_CLOSE: if(!s->busy) DestroyWindow(w); return 0;
    case WM_DESTROY: s->done=1; if(s->font) DeleteObject(s->font); free(s->art); return 0;
    }
    return DefWindowProcW(w,message,wp,lp);
}

int world_setup_screen(const char *directory)
{
    Setup s={0};
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,directory,-1,s.directory,32768)) return 0;
    s.status=L"Required data is copied into the game folder. Your archive stays untouched.";
    SetProcessDPIAware();
    WNDCLASSW wc={0}; wc.lpfnWndProc=procedure; wc.hInstance=GetModuleHandleW(NULL);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1)); wc.lpszClassName=L"InTheHuntFirstRun";
    RegisterClassW(&wc);
    RECT rect={0,0,880,700}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    HWND w=CreateWindowW(wc.lpszClassName,L"In the Hunt - Welcome",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,wc.hInstance,&s);
    if(!w) return 0;
    ShowWindow(w,SW_SHOW); SetForegroundWindow(w); SetFocus(s.browse);
    MSG msg;
    while(!s.done && GetMessageW(&msg,NULL,0,0)>0) if(!IsDialogMessageW(w,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return s.success;
}
