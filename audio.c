#include "audio.h"

#include <stdlib.h>
#include <string.h>

/* runs on the AudioQueue thread; re-enqueues the buffer immediately so the queue never starves */
static void audio_callback(
    void *user_data,
    AudioQueueRef queue,
    AudioQueueBufferRef buf,
    const AudioTimeStamp *start_time,
    UInt32 num_packets,
    const AudioStreamPacketDescription *pkt_desc)
{
    (void)start_time; (void)num_packets; (void)pkt_desc;

    AudioState *s = (AudioState *)user_data;
    const float *samples = (const float *)buf->mAudioData;
    int n = (int)(buf->mAudioDataByteSize / sizeof(float));
    if (n > BUFFER_SIZE) n = BUFFER_SIZE;

    pthread_mutex_lock(&s->mutex);
    memcpy(s->raw_samples, samples, (size_t)n * sizeof(float));
    s->samples_ready = true;
    pthread_mutex_unlock(&s->mutex);

    AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
}

void audio_init(AudioState *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mutex, NULL);

    s->fft_setup = vDSP_create_fftsetup(LOG2_FFT_SIZE, FFT_RADIX2);
    s->in_real   = (float *)malloc(BUFFER_SIZE * sizeof(float));
    s->in_imag   = (float *)malloc(BUFFER_SIZE * sizeof(float));
    s->window    = (float *)malloc(BUFFER_SIZE * sizeof(float));

    s->split_complex.realp = s->in_real;
    s->split_complex.imagp = s->in_imag;

    vDSP_hann_window(s->window, BUFFER_SIZE, vDSP_HANN_NORM);
}

void audio_start(AudioState *s) {
    AudioStreamBasicDescription fmt = {
        .mSampleRate       = SAMPLE_RATE,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mFramesPerPacket  = 1,
        .mChannelsPerFrame = NUM_CHANNELS,
        .mBitsPerChannel   = BYTES_PER_SAMPLE * 8,
        .mBytesPerPacket   = BYTES_PER_SAMPLE,
        .mBytesPerFrame    = BYTES_PER_SAMPLE,
    };

    AudioQueueNewInput(&fmt, audio_callback, s, NULL,
                       kCFRunLoopCommonModes, 0, &s->queue);

    AudioQueueBufferRef bufs[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        AudioQueueAllocateBuffer(s->queue, BUFFER_SIZE * sizeof(float), &bufs[i]);
        AudioQueueEnqueueBuffer(s->queue, bufs[i], 0, NULL);
    }

    AudioQueueStart(s->queue, NULL);
}

void audio_stop(AudioState *s) {
    AudioQueueStop(s->queue, true);
    AudioQueueDispose(s->queue, true);
    s->queue = NULL;
}

void audio_cleanup(AudioState *s) {
    if (s->queue) audio_stop(s);
    vDSP_destroy_fftsetup(s->fft_setup);
    free(s->in_real);
    free(s->in_imag);
    free(s->window);
    pthread_mutex_destroy(&s->mutex);
}

void audio_compute_fft(AudioState *s, const float *samples) {
    /* apply Hann window */
    vDSP_vmul(samples, 1, s->window, 1, s->in_real, 1, BUFFER_SIZE);
    vDSP_vclr(s->in_imag, 1, BUFFER_SIZE);

    /* pack into split complex and run FFT */
    vDSP_ctoz((DSPComplex *)s->in_real, 2, &s->split_complex, 1, BUFFER_SIZE / 2);
    vDSP_fft_zrip(s->fft_setup, &s->split_complex, 1, LOG2_FFT_SIZE, FFT_FORWARD);

    /* magnitudes */
    vDSP_zvabs(&s->split_complex, 1, s->fft_out, 1, BUFFER_SIZE / 2);

    float scale = 1.0f / (2.0f * BUFFER_SIZE);
    vDSP_vsmul(s->fft_out, 1, &scale, s->fft_out, 1, BUFFER_SIZE / 2);

    /* keep a copy of the raw samples for the oscilloscope mode */
    memcpy(s->display_samples, samples, BUFFER_SIZE * sizeof(float));
}
