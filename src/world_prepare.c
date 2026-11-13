/* RecompLauncher import-only entry point. No shell interpolation of ROM paths. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
int wmain(int argc, wchar_t **argv)
{
    wchar_t self[32768], system[32768], command[65536];
    if (argc != 2 || !argv[1][0]) { fwprintf(stderr, L"Choose your World ROM archive.\n"); return 1; }
    DWORD n = GetModuleFileNameW(NULL, self, 32768);
    if (!n || n >= 32768) return 1;
    wchar_t *slash = wcsrchr(self, L'\\');
    if (!slash) return 1;
    *slash = 0;
    n = GetSystemDirectoryW(system, 32768);
    if (!n || n >= 32768 || !SetEnvironmentVariableW(L"INTHUNT_IMPORT_ARCHIVE", argv[1])) return 1;
    int count = swprintf(command, 65536,
        L"\"%ls\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%ls\\tools\\recomplauncher_setup.ps1\"", system, self);
    if (count < 0 || count >= 65536) return 1;
    STARTUPINFOW si = {0}; PROCESS_INFORMATION pi = {0};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, self, &si, &pi)) {
        fprintf(stderr, "Could not start ROM importer (Windows error %lu).\n", GetLastError()); return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess);
    return (int)code;
}
