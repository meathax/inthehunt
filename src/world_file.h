#ifndef WORLD_FILE_H
#define WORLD_FILE_H

/* Runtime paths are UTF-8; Windows file APIs must not use the ANSI code page. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *world_fopen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!n) return NULL;
    wchar_t *wide = malloc((size_t)n * sizeof *wide);
    wchar_t wide_mode[16];
    if (!wide) return NULL;
    FILE *file = NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n) &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wide_mode, 16))
        file = _wfopen(wide, wide_mode);
    free(wide);
    return file;
}
#endif
