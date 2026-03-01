#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pthread.h>
#include <signal.h>

#include <AudioToolbox/AudioToolbox.h>
#include <Accelerate/Accelerate.h>

#define SAMPLE_RATE 44100.0
#define NUM_CHANNELS 1
#define BYTES_PER_SAMPLE 4
#define BUFFER_SIZE 4096 
#define NUM_BUFFERS 3

#define LOG2_FFT_SIZE 12 // 2^12 = 4096
#define FFT_HALF_SIZE (1 << (LOG2_FFT_SIZE - 1))

const char *blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
const int num_blocks = sizeof(blocks) / sizeof(blocks[0]);

AudioQueueRef queue;
bool is_running = true;
float sensitivity = 1.0f;
int bar_width = 1;

struct termios orig_termios;

void set_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void signal_handler(int sig) {
    (void)sig;
    is_running = false;
    restore_terminal();
    printf("\033[2J\033[H\033[?25h\n");
    fflush(stdout);
    _exit(0);
}

void *input_thread_func(void *arg) {
    (void)arg;
    char c;
    while (is_running) {
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == '+' || c == '=') {
                sensitivity = fminf(sensitivity + 0.2f, 5.0f);
            } else if (c == '-') {
                sensitivity = fmaxf(sensitivity - 0.2f, 0.1f);
            } else if (c == ']') {
                bar_width = (bar_width < 4) ? bar_width + 1 : 4;
            } else if (c == '[') {
                bar_width = (bar_width > 1) ? bar_width - 1 : 1;
            } else if (c == 'q' || c == '\n') {
                is_running = false;
                break;
            }
        }
    }
    return NULL;
}

FFTSetup fft_setup;
float *in_real;
float *in_imag;
DSPSplitComplex split_complex;
float *magnitudes;
float *window;

void compute_fft(float *samples, int num_samples) {
    vDSP_vmul(samples, 1, window, 1, in_real, 1, num_samples);
    vDSP_vclr(in_imag, 1, num_samples);
    vDSP_ctoz((DSPComplex *)in_real, 2, &split_complex, 1, num_samples / 2);
    vDSP_fft_zrip(fft_setup, &split_complex, 1, LOG2_FFT_SIZE, FFT_FORWARD);
    vDSP_zvabs(&split_complex, 1, magnitudes, 1, num_samples / 2);

    float scale = 1.0f / (2.0f * num_samples);
    vDSP_vsmul(magnitudes, 1, &scale, magnitudes, 1, num_samples / 2);
}

void draw_visualizer() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int cols = w.ws_col;
    int rows = w.ws_row;

    if (cols <= 0 || rows <= 0) return;

    int num_bars = cols / bar_width;
    if (num_bars <= 0) return;

    float col_mags[num_bars];
    for(int i = 0; i < num_bars; i++) col_mags[i] = 0.0f;
    float max_mag = 0.001f; // prevent div by zero

    // sub-bass starts ~40Hz, end_bin restricts to primary audible range
    int start_bin = 4;
    int end_bin = 200;

    float log_min = log10(start_bin);
    float log_max = log10(end_bin);
    float log_range = log_max - log_min;

    for (int i = 0; i < num_bars; i++) {
        float log_start = log_min + ((float)i / num_bars) * log_range;
        float log_end   = log_min + ((float)(i + 1) / num_bars) * log_range;

        int bin_start = (int)pow(10, log_start);
        int bin_end   = (int)pow(10, log_end);

        if(bin_start < start_bin) bin_start = start_bin;

        // ensure at least 1 bin per column so all columns get data
        if(bin_end <= bin_start) bin_end = bin_start + 1;

        // clamp to prevent overreading the magnitudes array
        if(bin_end > FFT_HALF_SIZE) bin_end = FFT_HALF_SIZE;

        float sum = 0.0f;
        int count = 0;

        for(int j = bin_start; j < bin_end && j < FFT_HALF_SIZE; j++) {
            sum += magnitudes[j];
            count++;
        }

        col_mags[i] = (count > 0) ? (sum / count) : 0.0f;

        // soft noise gate, zero out very quiet bins
        if (col_mags[i] < 0.0008f) {
            col_mags[i] = 0.0f;
        }

        // boost higher frequencies slightly since they tend to have lower amplitude
        float boost_factor = 1.0f + ((float)i / num_bars) * 3.0f;
        float volume_dampener = 1.5f;

        col_mags[i] *= (boost_factor * volume_dampener);

        if (col_mags[i] > max_mag) {
            max_mag = col_mags[i];
        }
    }

    // decay max_mag over time to avoid jumpy transitions
    static float prev_max_mag = 0.001f;
    if (max_mag < prev_max_mag * 0.95f) {
        max_mag = prev_max_mag * 0.95f;
    }

    // hard floor so silence stays flat at row 0
    float absolute_minimum_mag = 0.15f;
    if (max_mag < absolute_minimum_mag) {
        max_mag = absolute_minimum_mag;
    }

    prev_max_mag = max_mag;

    float smoothed_mags[num_bars];
    for (int i = 0; i < num_bars; i++) smoothed_mags[i] = col_mags[i];

    for (int i = 0; i < num_bars; i++) {
        float val = col_mags[i];
        if (i > 0) val = val * 0.4f + col_mags[i-1] * 0.3f;
        if (i < num_bars - 1) val = val + col_mags[i+1] * 0.3f;
        smoothed_mags[i] = val;
    }

    // temporal EMA: 65% new frame, 35% old, responsive but smooth
    static float temporal_mags[2048] = {0};
    for (int i = 0; i < num_bars && i < 2048; i++) {
        temporal_mags[i] = smoothed_mags[i] * 0.65f + temporal_mags[i] * 0.35f;
        smoothed_mags[i] = temporal_mags[i];
    }

    // move cursor to top-left without clearing to avoid flicker
    printf("\033[H");

    int bar_rows = rows - 1; // bottom row reserved for status

    for (int r = bar_rows; r > 0; r--) {
        // t=0 at bottom (green), t=1 at top (red)
        float t = (float)(r - 1) / bar_rows;
        int R = (int)(t * 255);
        int G = (int)((1.0f - t) * 200);
        int B = 0;

        for (int b = 0; b < num_bars; b++) {
            float normalized = smoothed_mags[b] / max_mag;

            // gamma curve so the visualizer feels taller and fuller
            normalized = powf(normalized, 0.7f) * sensitivity;

            if(normalized > 1.0f) normalized = 1.0f;

            float height_val = normalized * bar_rows;

            const char *block;
            if (height_val >= r) {
                block = blocks[8];
            } else if (height_val >= r - 1 && height_val > 0) {
                int block_idx = (int)((height_val - (r - 1)) * 8);
                if (block_idx < 0) block_idx = 0;
                if (block_idx > 8) block_idx = 8;
                block = blocks[block_idx];
            } else {
                block = NULL;
            }

            for (int w = 0; w < bar_width; w++) {
                if (block == NULL) {
                    printf(" ");
                } else {
                    printf("\033[38;2;%d;%d;%dm%s\033[0m", R, G, B, block);
                }
            }
        }
        printf("\n");
    }

    // status line at bottom — drawn last, cursor is already here after bar loop
    printf("\033[90msensitivity: %.1f  bar width: %d  (+/- sens, [/] width, q quit)\033[0m\033[K",
           sensitivity, bar_width);
    fflush(stdout);
}

void audio_callback(void *custom_data, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp *start_time, UInt32 num_packets, const AudioStreamPacketDescription *packet_desc) {
    (void)custom_data;
    (void)start_time;
    (void)num_packets;
    (void)packet_desc;

    float *samples = (float *)buffer->mAudioData;
    int num_samples = buffer->mAudioDataByteSize / sizeof(float);

    if (num_samples > 0 && is_running) {
        compute_fft(samples, num_samples);
        draw_visualizer();
        AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
    }
}

void init_fft() {
    fft_setup = vDSP_create_fftsetup(LOG2_FFT_SIZE, FFT_RADIX2);
    in_real = (float *)malloc(BUFFER_SIZE * sizeof(float));
    in_imag = (float *)malloc(BUFFER_SIZE * sizeof(float));
    magnitudes = (float *)malloc(FFT_HALF_SIZE * sizeof(float));
    window = (float *)malloc(BUFFER_SIZE * sizeof(float));
    
    split_complex.realp = in_real;
    split_complex.imagp = in_imag;

    vDSP_hann_window(window, BUFFER_SIZE, vDSP_HANN_NORM);
}

void cleanup_fft() {
    vDSP_destroy_fftsetup(fft_setup);
    free(in_real);
    free(in_imag);
    free(magnitudes);
    free(window);
}

int main() {
    printf("Initializing Audio Visualizer...\n");

    init_fft();

    AudioStreamBasicDescription format;
    format.mSampleRate = SAMPLE_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = NUM_CHANNELS;
    format.mBitsPerChannel = BYTES_PER_SAMPLE * 8;
    format.mBytesPerPacket = BYTES_PER_SAMPLE;
    format.mBytesPerFrame = BYTES_PER_SAMPLE;

    AudioQueueNewInput(&format, audio_callback, NULL, NULL, kCFRunLoopCommonModes, 0, &queue);

    AudioQueueBufferRef buffers[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        AudioQueueAllocateBuffer(queue, BUFFER_SIZE * sizeof(float), &buffers[i]);
        AudioQueueEnqueueBuffer(queue, buffers[i], 0, NULL);
    }

    // hide terminal cursor
    printf("\033[?25l");
    fflush(stdout);

    set_raw_mode();
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Starting Audio Capture. Press +/- sensitivity, [/] width, q to quit.\n");
    AudioQueueStart(queue, NULL);

    pthread_t input_thread;
    pthread_create(&input_thread, NULL, input_thread_func, NULL);
    pthread_join(input_thread, NULL);

    is_running = false;
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);

    restore_terminal();

    // show terminal cursor
    printf("\033[2J\033[H\033[?25h");
    fflush(stdout);

    cleanup_fft();
    printf("Exited.\n");

    return 0;
}
