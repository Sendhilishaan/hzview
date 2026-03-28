#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>
#include <pthread.h>
#include <stdbool.h>

#define SAMPLE_RATE      44100.0f
#define NUM_CHANNELS     1
#define BYTES_PER_SAMPLE 4
#define BUFFER_SIZE      4096
#define NUM_BUFFERS      3
#define LOG2_FFT_SIZE    12
#define FFT_SIZE         (1 << LOG2_FFT_SIZE)   /* 4096 */
#define FFT_HALF_SIZE    (FFT_SIZE / 2)          /* 2048 */

typedef struct {
    /* shared between audio thread and main thread — guarded by mutex */
    pthread_mutex_t mutex;
    float           raw_samples[BUFFER_SIZE];
    volatile bool   samples_ready;

    /* owned exclusively by main thread after copying raw_samples */
    float           fft_out[FFT_HALF_SIZE];     /* raw magnitude spectrum */
    float           display_samples[BUFFER_SIZE]; /* copy for oscilloscope */

    /* vDSP resources */
    FFTSetup        fft_setup;
    float          *in_real;
    float          *in_imag;
    DSPSplitComplex split_complex;
    float          *window;

    /* AudioQueue */
    AudioQueueRef   queue;
} AudioState;

void audio_init(AudioState *s);
void audio_start(AudioState *s);
void audio_stop(AudioState *s);
void audio_cleanup(AudioState *s);

/* called from the main thread after copying raw_samples */
void audio_compute_fft(AudioState *s, const float *samples);
