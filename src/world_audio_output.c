#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "world_audio_output.h"

/* Mono 48 kHz output through waveOut. Samples go into a FIFO and are handed
 * to the device in small blocks as blocks free up; nothing here ever waits,
 * so the video clock alone paces the game and the caller keeps the level
 * near its target by resampling slightly (see sound_frame). */
enum { BLOCKS = 24, BLOCK = 256, FIFO = 16384, START_LEVEL = 1536 };
static HWAVEOUT device;
static WAVEHDR headers[BLOCKS];
static int16_t blocks[BLOCKS][BLOCK];
static int16_t fifo[FIFO];
static unsigned fifo_read, fifo_count, prepared, next_block, in_flight, playing;

void world_audio_output_close(void)
{
    if (device) {
        waveOutReset(device);
        for (unsigned i = 0; i < prepared; ++i) waveOutUnprepareHeader(device, &headers[i], sizeof headers[i]);
        waveOutClose(device);
        device = NULL;
    }
    prepared = next_block = in_flight = playing = 0;
    fifo_read = fifo_count = 0;
}

int world_audio_output_open(void)
{
    if (device) return 1;
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 96000;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        device = NULL;
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
    return 1;
}

/* Hand every free block that the FIFO can fill to the device. */
void world_audio_output_pump(void)
{
    if (!device) return;
    in_flight = 0;
    for (unsigned i = 0; i < BLOCKS; ++i)
        if ((headers[i].dwFlags & WHDR_INQUEUE) && !(headers[i].dwFlags & WHDR_DONE)) ++in_flight;
    if (!playing && fifo_count < START_LEVEL && in_flight == 0) return;   /* priming */
    while (fifo_count >= BLOCK) {
        WAVEHDR *h = &headers[next_block];
        if ((h->dwFlags & WHDR_INQUEUE) && !(h->dwFlags & WHDR_DONE)) break;
        for (unsigned i = 0; i < BLOCK; ++i) {
            blocks[next_block][i] = fifo[fifo_read];
            fifo_read = (fifo_read + 1) % FIFO;
        }
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

int world_audio_output_submit(const int16_t *samples, size_t count)
{
    if (!device) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (fifo_count == FIFO) {         /* full: drop the oldest sample */
            fifo_read = (fifo_read + 1) % FIFO;
            --fifo_count;
        }
        fifo[(fifo_read + fifo_count) % FIFO] = samples[i];
        ++fifo_count;
    }
    world_audio_output_pump();
    return 1;
}

unsigned world_audio_output_level(void)
{
    return device ? fifo_count + in_flight * BLOCK : 0;
}
