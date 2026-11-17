#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "world_audio_output.h"

/* Stereo 48 kHz output through waveOut. Samples go into a FIFO; a feeder
 * thread hands them to the device in small blocks as blocks free up, so the
 * driver calls stay off the game thread. Nothing here ever makes the game
 * wait: the video clock alone paces the game, and the caller keeps the level
 * near its target by resampling slightly (see sound_frame). */
enum { BLOCKS = 24, BLOCK = 256, FIFO = 16384, START_LEVEL = 1536 };
static HWAVEOUT device;
static WAVEHDR headers[BLOCKS];
static int16_t blocks[BLOCKS][BLOCK][2];
static int16_t fifo[FIFO][2];
static unsigned fifo_read, fifo_count, prepared, next_block, in_flight, playing;
static CRITICAL_SECTION lock;
static HANDLE wake, feeder;
static volatile LONG stopping;
static int lock_ready;

/* Hand every free block that the FIFO can fill to the device. Caller holds lock. */
static void pump_locked(void)
{
    if (!device) return;
    in_flight = 0;
    for (unsigned i = 0; i < BLOCKS; ++i)
        if ((headers[i].dwFlags & WHDR_INQUEUE) && !(headers[i].dwFlags & WHDR_DONE)) ++in_flight;
    if (!playing && fifo_count < START_LEVEL && in_flight == 0) return;   /* priming */
    while (fifo_count >= BLOCK) {
        WAVEHDR *h = &headers[next_block];
        if ((h->dwFlags & WHDR_INQUEUE) && !(h->dwFlags & WHDR_DONE)) break;
        unsigned first = FIFO - fifo_read < BLOCK ? FIFO - fifo_read : BLOCK;
        memcpy(blocks[next_block], fifo[fifo_read], first * sizeof fifo[0]);
        memcpy(blocks[next_block][first], fifo[0], (BLOCK - first) * sizeof fifo[0]);
        fifo_read = (fifo_read + BLOCK) % FIFO;
        fifo_count -= BLOCK;
        h->dwBufferLength = sizeof blocks[next_block];
        if (waveOutWrite(device, h, sizeof *h) != MMSYSERR_NOERROR) break;
        next_block = (next_block + 1) % BLOCKS;
        ++in_flight;
    }
    if (!playing && in_flight) {
        waveOutRestart(device);
        playing = 1;
    }
    if (playing && !in_flight) {          /* ran dry: pause and prime again */
        waveOutPause(device);
        playing = 0;
    }
}

/* Woken by block completion (CALLBACK_EVENT) and by new samples. */
static DWORD WINAPI feed(void *unused)
{
    (void)unused;
    while (!InterlockedCompareExchange(&stopping, 0, 0)) {
        WaitForSingleObject(wake, 5);
        EnterCriticalSection(&lock);
        pump_locked();
        LeaveCriticalSection(&lock);
    }
    return 0;
}

void world_audio_output_close(void)
{
    if (feeder) {
        InterlockedExchange(&stopping, 1);
        SetEvent(wake);
        WaitForSingleObject(feeder, INFINITE);
        CloseHandle(feeder);
        feeder = NULL;
    }
    if (device) {
        waveOutReset(device);
        for (unsigned i = 0; i < prepared; ++i) waveOutUnprepareHeader(device, &headers[i], sizeof headers[i]);
        waveOutClose(device);
        device = NULL;
    }
    if (wake) { CloseHandle(wake); wake = NULL; }
    prepared = next_block = in_flight = playing = 0;
    fifo_read = fifo_count = 0;
}

int world_audio_output_open(void)
{
    if (device) return 1;
    if (!lock_ready) { InitializeCriticalSection(&lock); lock_ready = 1; }
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!wake) return 0;
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 192000;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, (DWORD_PTR)wake, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        device = NULL;
        world_audio_output_close();
        return 0;
    }
    memset(headers, 0, sizeof headers);
    for (unsigned i = 0; i < BLOCKS; ++i) {
        headers[i].lpData = (LPSTR)blocks[i];
        headers[i].dwBufferLength = sizeof blocks[i];
        if (waveOutPrepareHeader(device, &headers[i], sizeof headers[i]) != MMSYSERR_NOERROR) {
            world_audio_output_close();
            return 0;
        }
        ++prepared;
    }
    waveOutPause(device);
    InterlockedExchange(&stopping, 0);
    feeder = CreateThread(NULL, 0, feed, NULL, 0, NULL);
    if (!feeder) { world_audio_output_close(); return 0; }
    SetThreadPriority(feeder, THREAD_PRIORITY_TIME_CRITICAL);
    return 1;
}

/* The feeder thread services the device; this only wakes it. */
void world_audio_output_pump(void)
{
    if (wake) SetEvent(wake);
}

int world_audio_output_submit(const int16_t *samples, size_t count)
{
    if (!device) return 0;
    EnterCriticalSection(&lock);
    if (count > FIFO) { samples += 2 * (count - FIFO); count = FIFO; }
    if (fifo_count + count > FIFO) {      /* full: drop the oldest samples */
        unsigned drop = (unsigned)(fifo_count + count - FIFO);
        fifo_read = (fifo_read + drop) % FIFO;
        fifo_count -= drop;
    }
    unsigned at = (fifo_read + fifo_count) % FIFO;
    unsigned first = FIFO - at < count ? FIFO - at : (unsigned)count;
    memcpy(fifo[at], samples, first * sizeof fifo[0]);
    memcpy(fifo[0], samples + 2 * first, (count - first) * sizeof fifo[0]);
    fifo_count += (unsigned)count;
    LeaveCriticalSection(&lock);
    SetEvent(wake);
    return 1;
}

unsigned world_audio_output_level(void)
{
    if (!device) return 0;
    EnterCriticalSection(&lock);
    unsigned level = fifo_count + in_flight * BLOCK;
    LeaveCriticalSection(&lock);
    return level;
}

void world_audio_output_trim(unsigned keep)
{
    if (!device) return;
    EnterCriticalSection(&lock);
    unsigned queued = in_flight * BLOCK;
    unsigned allowed = keep > queued ? keep - queued : 0;
    if (fifo_count > allowed) {
        unsigned drop = fifo_count - allowed;
        fifo_read = (fifo_read + drop) % FIFO;
        fifo_count -= drop;
    }
    LeaveCriticalSection(&lock);
}
