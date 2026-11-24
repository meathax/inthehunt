/* Native, hash-checked ROM importer.
 *
 * Windows' own tar.exe (bsdtar, Windows 10 1803+) unpacks the user's ZIP or 7z
 * into a private scratch folder; every member is then identified only by its
 * SHA-1. Regional chips are rebuilt into World chips with the copy-range tables
 * in world_regions.inc and must match the World SHA-1 before they are used.
 * The cache format (native-import.ok) is unchanged from earlier releases. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world_import.h"
#include "world_regions.inc"

#define PATH_CHARS 4096
#define MAX_REGIONS 16
#define MAX_CHIPS 64
#define MAX_FILE_BYTES (64u << 20)

typedef struct {
    const WorldRegionTable *table;
    const WorldChip *chips[MAX_CHIPS];
    uint8_t *data[MAX_CHIPS];
    uint32_t chip_count, found;
    wchar_t *error;
    size_t error_count;
} Import;

typedef struct { char name[64]; long long size; char sha256[80]; } ManifestRegion;
typedef struct {
    long long version;
    char metadata[80], archive[80];
    ManifestRegion regions[MAX_REGIONS];
    int region_count, has_version, has_metadata, has_archive, has_regions;
} Manifest;

static int fail(Import *im, const wchar_t *format, ...)
{
    if (im->error && im->error_count && !im->error[0]) {
        va_list args;
        va_start(args, format);
        vswprintf(im->error, im->error_count, format, args);
        va_end(args);
    }
    return 0;
}

static void widen(const char *text, wchar_t *out, int count)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, out, count)) out[0] = 0;
}

static int join(wchar_t *out, const wchar_t *directory, const wchar_t *name)
{
    int n = swprintf(out, PATH_CHARS, L"%ls\\%ls", directory, name);
    return n > 0 && n < PATH_CHARS;
}

/* ---- hashing (BCrypt) ---- */

typedef struct { BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE hash; ULONG length; } Hash;

static int hash_open(Hash *h, LPCWSTR algorithm)
{
    ULONG got = 0;
    h->alg = NULL; h->hash = NULL; h->length = 0;
    if (BCryptOpenAlgorithmProvider(&h->alg, algorithm, NULL, 0) < 0) { h->alg = NULL; return 0; }
    if (BCryptGetProperty(h->alg, BCRYPT_HASH_LENGTH, (PUCHAR)&h->length, sizeof h->length, &got, 0) < 0 ||
        h->length > 32 || BCryptCreateHash(h->alg, &h->hash, NULL, 0, NULL, 0, 0) < 0) {
        h->hash = NULL;
        BCryptCloseAlgorithmProvider(h->alg, 0); h->alg = NULL;
        return 0;
    }
    return 1;
}

static int hash_add(Hash *h, const void *data, size_t size)
{
    const UCHAR *p = data;
    while (size) {
        ULONG part = size > 0x40000000u ? 0x40000000u : (ULONG)size;
        if (BCryptHashData(h->hash, (PUCHAR)p, part, 0) < 0) return 0;
        p += part; size -= part;
    }
    return 1;
}

static int hash_close(Hash *h, uint8_t *out)
{
    int ok = out && BCryptFinishHash(h->hash, out, h->length, 0) >= 0;
    if (h->hash) BCryptDestroyHash(h->hash);
    if (h->alg) BCryptCloseAlgorithmProvider(h->alg, 0);
    h->hash = NULL; h->alg = NULL;
    return ok;
}

static int hash_buffer(LPCWSTR algorithm, const void *data, size_t size, uint8_t *out)
{
    Hash h;
    if (!hash_open(&h, algorithm)) return 0;
    int ok = hash_add(&h, data, size);
    return hash_close(&h, ok ? out : NULL) && ok;
}

static int hash_file(LPCWSTR algorithm, const wchar_t *path, uint8_t *out)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    Hash h;
    uint8_t *buffer = malloc(1u << 20);
    int ok = buffer && hash_open(&h, algorithm);
    if (ok) {
        DWORD got;
        for (;;) {
            if (!ReadFile(f, buffer, 1u << 20, &got, NULL)) { ok = 0; break; }
            if (!got) break;
            if (!hash_add(&h, buffer, got)) { ok = 0; break; }
        }
        ok = hash_close(&h, ok ? out : NULL) && ok;
    }
    free(buffer);
    CloseHandle(f);
    return ok;
}

static void hex(const uint8_t *bytes, unsigned count, char *out)
{
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < count; ++i) { out[2*i] = digits[bytes[i] >> 4]; out[2*i+1] = digits[bytes[i] & 15]; }
    out[2*count] = 0;
}

/* ---- files ---- */

static int file_size(const wchar_t *path, long long *size)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    *size = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    return 1;
}

static int is_file(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static uint8_t *read_file(const wchar_t *path, size_t *size, size_t limit)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER length;
    uint8_t *data = NULL;
    DWORD got = 0;
    if (GetFileSizeEx(f, &length) && length.QuadPart >= 0 && (unsigned long long)length.QuadPart <= limit &&
        (data = malloc(length.QuadPart ? (size_t)length.QuadPart : 1)) &&
        (!length.QuadPart || (ReadFile(f, data, (DWORD)length.QuadPart, &got, NULL) && got == length.QuadPart))) {
        *size = (size_t)length.QuadPart;
    } else { free(data); data = NULL; }
    CloseHandle(f);
    return data;
}

static int write_new_file(const wchar_t *path, const void *data, size_t size)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = size <= MAXDWORD && WriteFile(f, data, (DWORD)size, &written, NULL) && written == size;
    ok = CloseHandle(f) && ok;
    if (!ok) DeleteFileW(path);
    return ok;
}

static int make_directories(const wchar_t *path)
{
    wchar_t partial[PATH_CHARS];
    size_t n = wcslen(path);
    if (!n || n >= PATH_CHARS) return 0;
    wcscpy(partial, path);
    /* Create each prefix; failures on drive roots and UNC shares are expected and
     * harmless, and the final attribute check decides the result. */
    for (size_t i = 1; i <= n; ++i) {
        if (i == n || partial[i] == L'\\' || partial[i] == L'/') {
            wchar_t saved = partial[i];
            partial[i] = 0;
            if (partial[i-1] != L'\\' && partial[i-1] != L'/') CreateDirectoryW(partial, NULL);
            partial[i] = saved;
        }
    }
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Deletes a scratch tree this importer created. Links are removed, never followed. */
static int remove_tree(const wchar_t *path, int depth)
{
    wchar_t pattern[PATH_CHARS], child[PATH_CHARS];
    WIN32_FIND_DATAW found;
    int ok = 1;
    if (depth > 32 || !join(pattern, path, L"*")) return 0;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
            if (!join(child, path, found.cFileName)) { ok = 0; continue; }
            if (found.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(child, found.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) ok = remove_tree(child, depth + 1) && ok;
                else ok = RemoveDirectoryW(child) && ok;
            } else ok = DeleteFileW(child) && ok;
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    return RemoveDirectoryW(path) && ok;
}

static int random_name(const wchar_t *prefix, const wchar_t *suffix, wchar_t *out, size_t count)
{
    uint8_t bytes[16];
    char text[33];
    if (BCryptGenRandom(NULL, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
    hex(bytes, sizeof bytes, text);
    wchar_t wide[33];
    for (int i = 0; i < 33; ++i) wide[i] = (wchar_t)(unsigned char)(text[i] >= 'A' ? text[i] - 'A' + 'a' : text[i]);
    int n = swprintf(out, count, L"%ls%ls%ls", prefix, wide, suffix);
    return n > 0 && (size_t)n < count;
}

static HANDLE open_lock(const wchar_t *world)
{
    wchar_t path[PATH_CHARS];
    if (!join(path, world, L"native-import.lock")) return INVALID_HANDLE_VALUE;
    ULONGLONG deadline = GetTickCount64() + 30000;
    for (;;) {
        HANDLE lock = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (lock != INVALID_HANDLE_VALUE) return lock;
        DWORD error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED) ||
            GetTickCount64() >= deadline) {
            SetLastError(error);
            return INVALID_HANDLE_VALUE;
        }
        Sleep(50);
    }
}

/* ---- native-import.ok ---- */

typedef struct { const char *p, *end; } Json;

static void json_space(Json *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n')) ++j->p;
}

static int json_char(Json *j, char c)
{
    json_space(j);
    if (j->p < j->end && *j->p == c) { ++j->p; return 1; }
    return 0;
}

/* ASCII strings only; anything else makes the manifest invalid (it is then rebuilt). */
static int json_string(Json *j, char *out, size_t count)
{
    size_t n = 0;
    if (!json_char(j, '"')) return 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x80) return 0;
        if (c == '\\') {
            if (j->p >= j->end) return 0;
            c = *j->p++;
            if (c != '"' && c != '\\' && c != '/') return 0;
        }
        if (out) { if (n + 1 >= count) return 0; out[n] = c; }
        ++n;
    }
    if (j->p >= j->end) return 0;
    ++j->p;
    if (out) out[n] = 0;
    return 1;
}

static int json_integer(Json *j, long long *out)
{
    json_space(j);
    int negative = j->p < j->end && *j->p == '-';
    if (negative) ++j->p;
    if (j->p >= j->end || *j->p < '0' || *j->p > '9') return 0;
    long long value = 0;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        if (value > (LLONG_MAX - 9) / 10) return 0;
        value = value * 10 + (*j->p++ - '0');
    }
    if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E')) return 0;
    *out = negative ? -value : value;
    return 1;
}

static int json_skip(Json *j, int depth)
{
    json_space(j);
    if (depth > 16 || j->p >= j->end) return 0;
    char c = *j->p;
    if (c == '"') return json_string(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        ++j->p;
        if (json_char(j, close)) return 1;
        do {
            if (c == '{' && (!json_string(j, NULL, 0) || !json_char(j, ':'))) return 0;
            if (!json_skip(j, depth + 1)) return 0;
        } while (json_char(j, ','));
        return json_char(j, close);
    }
    const char *start = j->p;
    while (j->p < j->end && strchr("+-.0123456789eEtruefalsn", *j->p)) ++j->p;
    return j->p > start;
}

static int parse_manifest(const char *text, size_t size, Manifest *m)
{
    Json j = {text, text + size};
    char key[64];
    memset(m, 0, sizeof *m);
    if (size >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) j.p += 3;
    if (!json_char(&j, '{')) return 0;
    if (!json_char(&j, '}')) {
        do {
            if (!json_string(&j, key, sizeof key) || !json_char(&j, ':')) return 0;
            if (!strcmp(key, "version")) { if (!json_integer(&j, &m->version)) return 0; m->has_version = 1; }
            else if (!strcmp(key, "metadataSha256")) { if (!json_string(&j, m->metadata, sizeof m->metadata)) return 0; m->has_metadata = 1; }
            else if (!strcmp(key, "archiveSha256")) { if (!json_string(&j, m->archive, sizeof m->archive)) return 0; m->has_archive = 1; }
            else if (!strcmp(key, "regions")) {
                if (!json_char(&j, '[')) return 0;
                m->has_regions = 1;
                if (!json_char(&j, ']')) {
                    do {
                        if (m->region_count >= MAX_REGIONS || !json_char(&j, '{')) return 0;
                        ManifestRegion *r = &m->regions[m->region_count++];
                        r->size = -1;
                        if (!json_char(&j, '}')) {
                            do {
                                if (!json_string(&j, key, sizeof key) || !json_char(&j, ':')) return 0;
                                if (!strcmp(key, "name")) { if (!json_string(&j, r->name, sizeof r->name)) return 0; }
                                else if (!strcmp(key, "size")) { if (!json_integer(&j, &r->size)) return 0; }
                                else if (!strcmp(key, "sha256")) { if (!json_string(&j, r->sha256, sizeof r->sha256)) return 0; }
                                else if (!json_skip(&j, 2)) return 0;
                            } while (json_char(&j, ','));
                            if (!json_char(&j, '}')) return 0;
                        }
                    } while (json_char(&j, ','));
                    if (!json_char(&j, ']')) return 0;
                }
            } else if (!json_skip(&j, 1)) return 0;
        } while (json_char(&j, ','));
        if (!json_char(&j, '}')) return 0;
    }
    json_space(&j);
    return j.p == j.end && m->has_version && m->has_metadata && m->has_regions;
}

/* Get-WorldNativeCacheManifest: the cache matches this table and every region file. */
static int cached_manifest(const WorldRegionTable *t, const wchar_t *world, Manifest *m)
{
    wchar_t path[PATH_CHARS], name[96];
    size_t size;
    if (!join(path, world, L"native-import.ok")) return 0;
    char *text = (char *)read_file(path, &size, 1u << 20);
    if (!text) return 0;
    int ok = parse_manifest(text, size, m) && m->version == 1 && !_stricmp(m->metadata, t->metadata_sha256) &&
             m->region_count == (int)t->region_count;
    free(text);
    for (uint32_t r = 0; ok && r < t->region_count; ++r) {
        const WorldRegion *region = &t->regions[r];
        int matches = 0, index = -1;
        for (int i = 0; i < m->region_count; ++i)
            if (!_stricmp(m->regions[i].name, region->name)) { ++matches; index = i; }
        if (matches != 1 || m->regions[index].size != region->size) { ok = 0; break; }
        uint8_t digest[32];
        char text_digest[65];
        long long length;
        wchar_t wide[64];
        widen(region->name, wide, 64);
        swprintf(name, 96, L"%ls.bin", wide);
        ok = join(path, world, name) && file_size(path, &length) && length == region->size &&
             hash_file(BCRYPT_SHA256_ALGORITHM, path, digest);
        if (ok) { hex(digest, 32, text_digest); ok = !_stricmp(text_digest, m->regions[index].sha256); }
    }
    return ok;
}

static int write_manifest(const WorldRegionTable *t, const wchar_t *world, const char *archive_sha256,
                          char region_sha256[][65])
{
    char text[8192];
    int n = snprintf(text, sizeof text, "{\n  \"version\": 1,\n  \"metadataSha256\": \"%s\",\n  \"archiveSha256\": \"%s\",\n  \"regions\": [",
                     t->metadata_sha256, archive_sha256);
    for (uint32_t r = 0; n > 0 && (size_t)n < sizeof text && r < t->region_count; ++r) {
        int add = snprintf(text + n, sizeof text - n,
                           "%s\n    {\n      \"name\": \"%s\",\n      \"size\": %u,\n      \"sha256\": \"%s\"\n    }",
                           r ? "," : "", t->regions[r].name, (unsigned)t->regions[r].size, region_sha256[r]);
        n = add < 0 ? -1 : n + add;
    }
    if (n < 0 || (size_t)n >= sizeof text) return 0;
    int add = snprintf(text + n, sizeof text - n, "\n  ]\n}\n");
    if (add < 0 || (size_t)(n + add) >= sizeof text) return 0;
    n += add;

    wchar_t path[PATH_CHARS], temporary[PATH_CHARS], name[96];
    if (!join(path, world, L"native-import.ok") || !random_name(L"manifest-", L".tmp", name, 96) ||
        !join(temporary, world, name) || !write_new_file(temporary, text, (size_t)n)) return 0;
    int ok = is_file(path) ? ReplaceFileW(path, temporary, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)
                           : MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH);
    if (is_file(temporary)) DeleteFileW(temporary);
    return ok;
}

/* New-WorldNativeCacheManifest: upgrade older caches from the chip hashes in the images. */
static int rebuild_manifest(const WorldRegionTable *t, const wchar_t *world)
{
    char sha[MAX_REGIONS][65];
    if (t->region_count > MAX_REGIONS) return 0;
    for (uint32_t r = 0; r < t->region_count; ++r) {
        const WorldRegion *region = &t->regions[r];
        wchar_t path[PATH_CHARS], name[96], wide[64];
        size_t size;
        widen(region->name, wide, 64);
        swprintf(name, 96, L"%ls.bin", wide);
        uint8_t *image = join(path, world, name) ? read_file(path, &size, MAX_FILE_BYTES) : NULL;
        int ok = image && size == region->size;
        for (uint32_t c = 0; ok && c < region->chip_count; ++c) {
            const WorldChip *chip = &region->chips[c];
            uint8_t *data = malloc(chip->size), digest[32];
            ok = data != NULL;
            for (uint32_t i = 0; ok && i < chip->size; ++i) data[i] = image[chip->offset + (size_t)chip->stride * i];
            ok = ok && hash_buffer(BCRYPT_SHA1_ALGORITHM, data, chip->size, digest) && !memcmp(digest, chip->sha1, 20);
            free(data);
        }
        uint8_t digest[32];
        ok = ok && hash_buffer(BCRYPT_SHA256_ALGORITHM, image, size, digest);
        free(image);
        if (!ok) return 0;
        hex(digest, 32, sha[r]);
    }
    return write_manifest(t, world, "", sha);
}

/* ---- archive members ---- */

static int run_tar(const wchar_t *directory, DWORD *exit_code)
{
    wchar_t system[MAX_PATH], tar[MAX_PATH + 16], command[MAX_PATH + 64];
    UINT n = GetSystemDirectoryW(system, MAX_PATH);
    if (!n || n >= MAX_PATH || swprintf(tar, MAX_PATH + 16, L"%ls\\tar.exe", system) < 0 || !is_file(tar)) return 0;
    /* Relative names only: bsdtar's narrow argv cannot carry every Unicode path. */
    if (swprintf(command, MAX_PATH + 64, L"\"%ls\" -xf archive", tar) < 0) return 0;

    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                              OPEN_EXISTING, 0, NULL);
    if (null == INVALID_HANDLE_VALUE) return 0;
    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &list_size);
    LPPROC_THREAD_ATTRIBUTE_LIST list = malloc(list_size);
    int ok = list && InitializeProcThreadAttributeList(list, 1, 0, &list_size);
    int initialized = ok;
    /* Hand the child only the NUL handle, nothing else this process may hold. */
    ok = ok && UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &null, sizeof null, NULL, NULL);
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi = {0};
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = null;
    si.lpAttributeList = list;
    ok = ok && CreateProcessW(tar, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                              NULL, directory, &si.StartupInfo, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        if (WaitForSingleObject(pi.hProcess, 240000) != WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            *exit_code = 1;
        } else if (!GetExitCodeProcess(pi.hProcess, exit_code)) *exit_code = 1;
        CloseHandle(pi.hProcess);
    }
    if (initialized) DeleteProcThreadAttributeList(list);
    free(list);
    CloseHandle(null);
    return ok;
}

static int process_member(Import *im, const wchar_t *path, long long length)
{
    int wanted = 0;
    for (uint32_t i = 0; i < im->chip_count; ++i) if (im->chips[i]->size == length && !im->data[i]) wanted = 1;
    if (!wanted) return 1;
    size_t size;
    uint8_t digest[32], *data = read_file(path, &size, MAX_FILE_BYTES);
    if (!data) return fail(im, L"Cannot read a member of the ROM archive.");
    if (!hash_buffer(BCRYPT_SHA1_ALGORITHM, data, size, digest)) { free(data); return fail(im, L"Cannot hash ROM data."); }
    for (uint32_t i = 0; i < im->chip_count; ++i) {
        const WorldChip *chip = im->chips[i];
        const WorldAlternate *alternate = NULL;
        int direct = !memcmp(digest, chip->sha1, 20);
        for (uint32_t a = 0; !direct && !alternate && a < chip->alternate_count; ++a)
            if (!memcmp(digest, chip->alternates[a].sha1, 20)) alternate = &chip->alternates[a];
        if ((!direct && !alternate) || chip->size != size || im->data[i]) continue;
        if (direct) { im->data[i] = data; ++im->found; return 1; }
        /* Each pair copies (offset, length) from this verified regional chip.
         * The native game's ROM table addresses use the World revision. */
        uint8_t *world = malloc(chip->size), check[32];
        size_t filled = 0;
        int ok = world != NULL;
        for (uint32_t r = 0; ok && filled < chip->size; ++r) {
            const WorldCopyRange *range = &alternate->ranges[r];
            ok = r < alternate->range_count && range->length && range->length <= chip->size - filled &&
                 range->offset <= size && range->length <= size - range->offset;
            if (ok) { memcpy(world + filled, data + range->offset, range->length); filled += range->length; }
        }
        free(data);
        if (!ok) { free(world); return fail(im, L"Invalid ROM copy range."); }
        wchar_t file[64];
        widen(chip->file, file, 64);
        if (!hash_buffer(BCRYPT_SHA1_ALGORITHM, world, chip->size, check) || memcmp(check, chip->sha1, 20)) {
            free(world);
            return fail(im, L"World ROM mismatch for %ls.", file);
        }
        im->data[i] = world; ++im->found;
        return 1;
    }
    free(data);
    return 1;
}

static int walk(Import *im, const wchar_t *directory, int depth)
{
    wchar_t pattern[PATH_CHARS], child[PATH_CHARS];
    WIN32_FIND_DATAW found;
    if (depth > 16 || !join(pattern, directory, L"*")) return 1;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return 1;
    int ok = 1;
    do {
        if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..") ||
            (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !join(child, directory, found.cFileName)) continue;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = walk(im, child, depth + 1);
        else ok = process_member(im, child, ((long long)found.nFileSizeHigh << 32) | found.nFileSizeLow);
    } while (ok && im->found < im->chip_count && FindNextFileW(find, &found));
    FindClose(find);
    return ok;
}

/* Unpacks one archive into scratch\<slot> and collects chips. Returns 0 only on a hard error. */
static int read_archive(Import *im, const wchar_t *archive, const wchar_t *scratch, int slot, int *decoded)
{
    wchar_t directory[PATH_CHARS], name[16], copy[PATH_CHARS];
    DWORD code = 1;
    swprintf(name, 16, L"a%d", slot);
    if (!join(directory, scratch, name) || !CreateDirectoryW(directory, NULL) || !join(copy, directory, L"archive") ||
        !CopyFileW(archive, copy, TRUE)) return fail(im, L"Cannot copy the ROM archive into the game folder.");
    if (!run_tar(directory, &code)) return fail(im, L"Windows tar.exe is required to read ROM archives (Windows 10 1803 or later).");
    DeleteFileW(copy);
    /* Members are identified only by hash, so a partly unreadable archive can still supply every chip. */
    *decoded = code == 0;
    return walk(im, directory, 0);
}

/* ---- import ---- */

static int install(Import *im, const wchar_t *world, const wchar_t *scratch, const char *archive_sha256, int *retain)
{
    const WorldRegionTable *t = im->table;
    char sha[MAX_REGIONS][65];
    for (uint32_t r = 0; r < t->region_count; ++r) {
        const WorldRegion *region = &t->regions[r];
        uint8_t *image = calloc(region->size, 1), digest[32];
        if (!image) return fail(im, L"Out of memory.");
        for (uint32_t c = 0; c < region->chip_count; ++c) {
            const WorldChip *chip = &region->chips[c];
            const uint8_t *data = NULL;
            for (uint32_t i = 0; i < im->chip_count; ++i) if (im->chips[i] == chip) data = im->data[i];
            for (uint32_t i = 0; data && i < chip->size; ++i) image[chip->offset + (size_t)chip->stride * i] = data[i];
        }
        wchar_t path[PATH_CHARS], name[96], wide[64];
        widen(region->name, wide, 64);
        swprintf(name, 96, L"%ls.bin", wide);
        int ok = join(path, scratch, name) && write_new_file(path, image, region->size) &&
                 hash_buffer(BCRYPT_SHA256_ALGORITHM, image, region->size, digest);
        free(image);
        if (!ok) return fail(im, L"Cannot write imported data into the game folder.");
        hex(digest, 32, sha[r]);
    }

    wchar_t marker[PATH_CHARS], previous[PATH_CHARS];
    int existed[MAX_REGIONS] = {0}, installed = 0, ok = 1;
    if (!join(marker, world, L"native-import.ok") || !join(previous, scratch, L"previous-marker")) return fail(im, L"Path too long.");
    if (is_file(marker) && !MoveFileExW(marker, previous, 0)) ok = 0;
    for (uint32_t r = 0; ok && r < t->region_count; ++r) {
        wchar_t target[PATH_CHARS], source[PATH_CHARS], backup[PATH_CHARS], name[96], wide[64];
        widen(t->regions[r].name, wide, 64);
        swprintf(name, 96, L"%ls.bin", wide);
        join(target, world, name); join(source, scratch, name);
        swprintf(backup, PATH_CHARS, L"%ls.previous", source);
        existed[r] = is_file(target);
        ok = existed[r] ? ReplaceFileW(target, source, backup, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)
                        : MoveFileExW(source, target, MOVEFILE_WRITE_THROUGH);
        if (ok) installed = (int)r + 1;
    }
    if (ok && write_manifest(t, world, archive_sha256, sha)) return 1;

    DWORD install_error = GetLastError();
    int restored = 1;
    for (int r = installed - 1; r >= 0; --r) {
        wchar_t target[PATH_CHARS], backup[PATH_CHARS], name[96], wide[64];
        widen(t->regions[r].name, wide, 64);
        swprintf(name, 96, L"%ls.bin", wide);
        join(target, world, name); join(backup, scratch, name);
        wcscat(backup, L".previous");
        restored = (existed[r] ? ReplaceFileW(target, backup, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)
                               : DeleteFileW(target)) && restored;
    }
    if (is_file(previous)) restored = MoveFileExW(previous, marker, 0) && restored;
    if (!restored) {
        *retain = 1;
        return fail(im, L"World import could not restore its previous cache. Recovery files remain in %ls.", scratch);
    }
    return fail(im, L"Cannot install imported data into the game folder (error %lu).", (unsigned long)install_error);
}

static int import_locked(Import *im, const wchar_t *archive, const wchar_t *world, int *retain, wchar_t *scratch)
{
    const WorldRegionTable *t = im->table;
    uint8_t digest[32];
    char archive_sha256[65];
    Manifest manifest;
    wchar_t full[PATH_CHARS], name[96], directory[PATH_CHARS], *file_part = NULL;

    DWORD n = GetFullPathNameW(archive, PATH_CHARS, full, &file_part);
    if (!n || n >= PATH_CHARS || !is_file(full) || !hash_file(BCRYPT_SHA256_ALGORITHM, full, digest))
        return fail(im, L"Cannot read the ROM archive.");
    hex(digest, 32, archive_sha256);
    if (cached_manifest(t, world, &manifest) && !_stricmp(manifest.archive, archive_sha256)) return 1;

    if (!random_name(L"import-", L"", name, 96) || !join(scratch, world, name) || !CreateDirectoryW(scratch, NULL)) {
        scratch[0] = 0;
        return fail(im, L"Cannot create a working folder in the game folder.");
    }
    int decoded = 0;
    if (!read_archive(im, full, scratch, 0, &decoded)) return 0;
    if (im->found > 0 && im->found < im->chip_count && file_part) {
        wcsncpy(directory, full, (size_t)(file_part - full));
        directory[file_part - full] = 0;
        static const wchar_t *parents[] = {L"inthunt.zip", L"inthunt.7z"};
        for (int p = 0, slot = 1; p < 2 && im->found < im->chip_count; ++p) {
            wchar_t parent[PATH_CHARS];
            int parent_decoded = 0;
            if (swprintf(parent, PATH_CHARS, L"%ls%ls", directory, parents[p]) < 0 || !_wcsicmp(parent, full) ||
                !is_file(parent)) continue;
            if (!read_archive(im, parent, scratch, slot++, &parent_decoded)) return 0;
            /* A single archive hash cannot identify a two-archive import. */
            archive_sha256[0] = 0;
        }
    }
    for (uint32_t i = 0; i < im->chip_count; ++i) {
        if (im->data[i]) continue;
        wchar_t file[64];
        widen(im->chips[i]->file, file, 64);
        if (!decoded && !im->found)
            return fail(im, L"Cannot decode %ls. Check that the archive is complete and readable.", file_part ? file_part : full);
        return fail(im, L"Missing ROM data for %ls. For a split set, keep inthunt.zip or inthunt.7z beside the selected archive.", file);
    }
    return install(im, world, scratch, archive_sha256, retain);
}

int world_import_rom(const WorldRegionTable *table, const wchar_t *archive, const wchar_t *world,
                     wchar_t *error, size_t error_count)
{
    Import im;
    memset(&im, 0, sizeof im);
    im.table = table; im.error = error; im.error_count = error_count;
    if (error && error_count) error[0] = 0;
    if (!archive || !archive[0]) return fail(&im, L"Choose your In the Hunt MAME ZIP or 7z archive.");
    if (!world || !world[0] || !make_directories(world)) return fail(&im, L"Cannot create the game data folder.");
    if (table->region_count > MAX_REGIONS) return fail(&im, L"Internal ROM table error.");
    for (uint32_t r = 0; r < table->region_count; ++r)
        for (uint32_t c = 0; c < table->regions[r].chip_count; ++c) {
            if (im.chip_count >= MAX_CHIPS) return fail(&im, L"Internal ROM table error.");
            im.chips[im.chip_count++] = &table->regions[r].chips[c];
        }

    HANDLE lock = open_lock(world);
    if (lock == INVALID_HANDLE_VALUE) return fail(&im, L"Another World import is still running. Try launching again after it finishes.");
    wchar_t scratch[PATH_CHARS] = L"";
    int retain = 0;
    int ok = import_locked(&im, archive, world, &retain, scratch);
    if (ok) {
        Manifest manifest;
        ok = cached_manifest(table, world, &manifest) || rebuild_manifest(table, world);
        if (!ok) fail(&im, L"Imported data failed validation.");
    }
    if (scratch[0] && !retain) remove_tree(scratch, 0);
    for (uint32_t i = 0; i < im.chip_count; ++i) free(im.data[i]);
    CloseHandle(lock);
    return ok;
}
