#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "world_audio_output.h"

enum { BUFFER_COUNT = 4, BUFFER_FRAMES = 1024 };
static HWAVEOUT device;
static HANDLE completion;
static WAVEHDR buffers[BUFFER_COUNT];
static int16_t pcm[BUFFER_COUNT][BUFFER_FRAMES];
static unsigned prepared, next_buffer, queued;
static int held;

void world_audio_output_close(void)
{
    if (device) {
        waveOutReset(device);
        for (unsigned i = 0; i < prepared; ++i) waveOutUnprepareHeader(device, &buffers[i], sizeof buffers[i]);
        waveOutClose(device);
        device = NULL;
    }
    if (completion) CloseHandle(completion);
    completion = NULL;
    prepared = next_buffer = queued = 0;
    held = 0;
}

void world_audio_output_pause(int paused)
{
    paused = !!paused;
    if (!device || paused == held) return;
    held = paused;
    if (paused) waveOutPause(device);
    /* Before two buffers are queued the device is still in its start-up pause. */
    else if (queued >= 2) waveOutRestart(device);
}

int world_audio_output_open(void)
{
    if (device) return 1;
    completion = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!completion) return 0;
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 96000;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, (DWORD_PTR)completion, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        world_audio_output_close();
        return 0;
    }
    memset(buffers, 0, sizeof buffers);
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        buffers[i].lpData = (LPSTR)pcm[i];
        buffers[i].dwBufferLength = sizeof pcm[i];
        if (waveOutPrepareHeader(device, &buffers[i], sizeof buffers[i]) != MMSYSERR_NOERROR) {
            world_audio_output_close();
            return 0;
        }
        ++prepared;
    }
    if (waveOutPause(device) != MMSYSERR_NOERROR) {
        world_audio_output_close();
        return 0;
    }
    return 1;
}

int world_audio_output_submit(const int16_t *samples, size_t count)
{
    if (!device || count > BUFFER_FRAMES) return 0;
    if (!count) return 1;
    WAVEHDR *buffer = &buffers[next_buffer];
    DWORD deadline = GetTickCount() + 100;
    while ((buffer->dwFlags & WHDR_INQUEUE) && !(buffer->dwFlags & WHDR_DONE)) {
        if ((LONG)(GetTickCount() - deadline) >= 0) return 0;
        if (WaitForSingleObject(completion, 5) == WAIT_FAILED) return 0;
    }
    memcpy(buffer->lpData, samples, count * sizeof *samples);
    buffer->dwBufferLength = (DWORD)(count * sizeof *samples);
    if (waveOutWrite(device, buffer, sizeof *buffer) != MMSYSERR_NOERROR) return 0;
    next_buffer = (next_buffer + 1) % BUFFER_COUNT;
    if (queued < 2 && ++queued == 2 && !held && waveOutRestart(device) != MMSYSERR_NOERROR) return 0;
    return 1;
}
