#include "viz.h"

#include <ncurses.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Unicode block characters
 * ========================================================================= */
static const char *VBLOCKS[] = {
    " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
};
#define VBLOCK_FULL "█"

/* =========================================================================
 * Color system — xterm-256 green → yellow → red, no init_color() needed.
 *
 * Uses the 6×6×6 color cube (indices 16-231): index = 16 + 36*r + 6*g + b.
 * Works on any terminal with COLORS >= 256 without touching init_color().
 *
 *   t = 0.0 (bottom)  →  green  (r=0, g=5, b=0)  →  palette index  46
 *   t = 0.5           →  yellow (r=5, g=5, b=0)  →  palette index 226
 *   t = 1.0 (top)     →  red    (r=5, g=0, b=0)  →  palette index 196
 * ========================================================================= */

void viz_init(void) {
    int half = NUM_GRADIENT / 2;
    for (int i = 0; i < NUM_GRADIENT; i++) {
        short fg;
        if (i < half) {
            /* green → yellow: r rises 0→5, g stays 5 */
            int r = (int)roundf((float)i / (half - 1) * 5.0f);
            if (r > 5) r = 5;
            fg = (short)(16 + 36 * r + 30);   /* +30 = 6*5 (g=5, b=0) */
        } else {
            /* yellow → red: r stays 5, g falls 5→0 */
            int g = (int)roundf((float)(NUM_GRADIENT - 1 - i) / (half - 1) * 5.0f);
            if (g > 5) g = 5;
            fg = (short)(16 + 180 + 6 * g);   /* +180 = 36*5 (r=5, b=0) */
        }
        init_pair((short)(i + 1), fg, -1);
    }
    init_pair(STATUS_PAIR_DARK,  COLOR_WHITE, -1);
    init_pair(STATUS_PAIR_LIGHT, COLOR_BLACK, -1);
}

void viz_cleanup(void) {}

/* Returns ncurses pair index for normalised height t∈[0,1] */
static inline int cpair(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int lvl = (int)(t * (NUM_GRADIENT - 1));
    if (lvl >= NUM_GRADIENT) lvl = NUM_GRADIENT - 1;
    return lvl + 1;
}

/* =========================================================================
 * compute_display_mags
 * ========================================================================= */
static float disp_mags[MAX_BARS];

static void compute_display_mags(
    const float *raw, float *out, int num_bars,
    int start_bin, int end_bin, float sensitivity)
{
    static float temporal[MAX_BARS] = {0};
    static float prev_max = 0.001f;

    float col_mags[MAX_BARS];
    float max_mag = 0.001f;

    float log_min   = log10f((float)start_bin);
    float log_max   = log10f((float)end_bin);
    float log_range = log_max - log_min;

    for (int i = 0; i < num_bars; i++) {
        float ls = log_min + ((float)i       / num_bars) * log_range;
        float le = log_min + ((float)(i + 1) / num_bars) * log_range;

        int b0 = (int)powf(10.0f, ls);
        int b1 = (int)powf(10.0f, le);
        if (b0 < start_bin)      b0 = start_bin;
        if (b1 <= b0)            b1 = b0 + 1;
        if (b1 > FFT_HALF_SIZE)  b1 = FFT_HALF_SIZE;

        float sum = 0.0f; int cnt = 0;
        for (int j = b0; j < b1 && j < FFT_HALF_SIZE; j++) { sum += raw[j]; cnt++; }
        col_mags[i] = cnt > 0 ? sum / cnt : 0.0f;

        if (col_mags[i] < 0.0008f) col_mags[i] = 0.0f;

        float boost = 1.0f + ((float)i / num_bars) * 3.0f;
        col_mags[i] *= boost * 1.5f;
        if (col_mags[i] > max_mag) max_mag = col_mags[i];
    }

    if (max_mag < prev_max * 0.95f) max_mag = prev_max * 0.95f;
    if (max_mag < 0.15f)            max_mag = 0.15f;
    prev_max = max_mag;

    float spatial[MAX_BARS];
    for (int i = 0; i < num_bars; i++) {
        float v = col_mags[i];
        if (i > 0)            v = v * 0.4f + col_mags[i - 1] * 0.3f;
        if (i < num_bars - 1) v = v       + col_mags[i + 1] * 0.3f;
        spatial[i] = v;
    }

    for (int i = 0; i < num_bars && i < MAX_BARS; i++) {
        temporal[i] = spatial[i] * 0.65f + temporal[i] * 0.35f;
        float n = powf(temporal[i] / max_mag, 0.7f) * sensitivity;
        if (n > 1.0f) n = 1.0f;
        if (n < 0.0f) n = 0.0f;
        out[i] = n;
    }
}

/* =========================================================================
 * draw_bars
 * ========================================================================= */
static void draw_bars(int bar_rows, int cols, int num_bars, int bw) {
    int offset = (cols - num_bars * bw) / 2;  /* center bar block horizontally */

    for (int r = 0; r < bar_rows; r++) {
        int   rb = bar_rows - 1 - r;          /* 0 = bottom row */
        float t  = (float)rb / (bar_rows > 1 ? bar_rows - 1 : 1);
        int   cp = cpair(t);                  /* green at bottom, red at top */

        for (int b = 0; b < num_bars; b++) {
            float hc    = disp_mags[b] * bar_rows;
            int h_cells = (int)hc;
            int h_part  = (int)((hc - h_cells) * 8);

            const char *block = NULL;
            if      (rb < h_cells)                block = VBLOCK_FULL;
            else if (rb == h_cells && h_part > 0) block = VBLOCKS[h_part];

            for (int w = 0; w < bw; w++) {
                int col = offset + b * bw + w;
                if (col < 0 || col >= cols) continue;
                if (block) {
                    attron(COLOR_PAIR(cp));
                    mvaddstr(r, col, block);
                    attroff(COLOR_PAIR(cp));
                } else {
                    mvaddch(r, col, ' ');
                }
            }
        }
    }
}

/* =========================================================================
 * draw_status
 * ========================================================================= */
static void draw_status(int row, const Config *cfg) {
    int    sp   = cfg->light_mode ? STATUS_PAIR_LIGHT : STATUS_PAIR_DARK;
    chtype bold = cfg->light_mode ? 0 : A_BOLD;

    attron(COLOR_PAIR(sp) | bold);
    mvprintw(row, 0,
             " sens:%.1f  w:%d"
             "  │  +/- sens  [/] width  l light  q quit",
             cfg->sensitivity, cfg->bar_width);
    clrtoeol();
    attroff(COLOR_PAIR(sp) | bold);
}

/* =========================================================================
 * viz_render
 * ========================================================================= */
static int prev_rows = 0, prev_cols = 0;

void viz_render(AudioState *audio, Config *cfg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (rows != prev_rows || cols != prev_cols) {
        clear();
        prev_rows = rows; prev_cols = cols;
    }
    if (rows < 3 || cols < 4) return;

    int bar_rows  = rows - 1;
    int bw        = cfg->bar_width > 0 ? cfg->bar_width : 1;
    int start_bin = (int)((float)cfg->freq_min * FFT_SIZE / SAMPLE_RATE);
    int end_bin   = (int)((float)cfg->freq_max * FFT_SIZE / SAMPLE_RATE);
    if (start_bin < 1)           start_bin = 1;
    if (end_bin > FFT_HALF_SIZE) end_bin   = FFT_HALF_SIZE;
    if (end_bin <= start_bin)    end_bin   = start_bin + 1;

    int num_bars = cols / bw;
    if (num_bars < 1)        num_bars = 1;
    if (num_bars > MAX_BARS) num_bars = MAX_BARS;

    compute_display_mags(audio->fft_out, disp_mags, num_bars,
                         start_bin, end_bin, cfg->sensitivity);

    draw_bars(bar_rows, cols, num_bars, bw);
    draw_status(rows - 1, cfg);
    refresh();
}
