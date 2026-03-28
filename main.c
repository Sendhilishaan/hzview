#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <math.h>

#include <ncurses.h>

#include "audio.h"
#include "viz.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------- */
static AudioState audio;
static volatile int is_running = 1;

/* -------------------------------------------------------------------------
 * Signal handler — sets flag; ncurses cleans up in main
 * ------------------------------------------------------------------------- */
static void on_signal(int sig) {
    (void)sig;
    is_running = 0;
}

/* -------------------------------------------------------------------------
 * Clamp helpers
 * ------------------------------------------------------------------------- */
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* -------------------------------------------------------------------------
 * Key handler
 * ------------------------------------------------------------------------- */
static void handle_key(int ch, Config *cfg) {
    switch (ch) {
        case '+': case '=':
            cfg->sensitivity = clampf(cfg->sensitivity + 0.2f, 0.1f, 5.0f);
            break;
        case '-':
            cfg->sensitivity = clampf(cfg->sensitivity - 0.2f, 0.1f, 5.0f);
            break;
        case ']':
            cfg->bar_width = clampi(cfg->bar_width + 1, 1, 8);
            break;
        case '[':
            cfg->bar_width = clampi(cfg->bar_width - 1, 1, 8);
            break;
        case 'l': case 'L':
            cfg->light_mode = !cfg->light_mode;
            clear(); /* force full redraw with new background */
            break;
        case 'q': case 'Q': case 27: /* ESC */
            is_running = 0;
            break;
        case KEY_RESIZE:
            /* viz_render handles resize detection automatically */
            clear();
            break;
        default:
            break;
    }
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
    /* locale must be set before initscr() for UTF-8 block chars to work */
    setlocale(LC_ALL, "");

    /* config */
    Config cfg;
    config_default(&cfg);
    const char *cfg_path = config_default_path();
    if (config_load(&cfg, cfg_path) != 0) {
        /* first run — write default config so the user can discover options */
        config_save(&cfg, cfg_path);
    }

    /* audio */
    audio_init(&audio);
    audio_start(&audio);

    /* ncurses init */
    initscr();
    if (!has_colors()) {
        endwin();
        audio_stop(&audio);
        audio_cleanup(&audio);
        fprintf(stderr, "hzview: terminal does not support colors\n");
        return 1;
    }
    start_color();
    use_default_colors(); /* allow -1 = transparent background */
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); /* non-blocking getch */

    viz_init();

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ---- render loop ---- */
    float local_samples[BUFFER_SIZE];

    while (is_running) {
        /* grab new audio data if available */
        pthread_mutex_lock(&audio.mutex);
        bool got_new = audio.samples_ready;
        if (got_new) {
            memcpy(local_samples, audio.raw_samples, BUFFER_SIZE * sizeof(float));
            audio.samples_ready = false;
        }
        pthread_mutex_unlock(&audio.mutex);

        if (got_new) {
            audio_compute_fft(&audio, local_samples);
        }

        /* input */
        int ch = getch();
        if (ch != ERR) handle_key(ch, &cfg);

        /* render */
        viz_render(&audio, &cfg);

        /* target ~60 fps; audio fires ~11 fps so most frames reuse last data */
        usleep(16000);
    }

    /* ---- shutdown ---- */
    viz_cleanup();
    endwin();

    audio_stop(&audio);
    audio_cleanup(&audio);

    /* persist current settings */
    config_save(&cfg, cfg_path);

    return 0;
}
