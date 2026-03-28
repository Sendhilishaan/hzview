#pragma once

#include "config.h"
#include "audio.h"

#define NUM_GRADIENT   32
#define MAX_BARS       1024

/* color pair layout:
 *   1 .. NUM_GRADIENT      gradient levels (xterm-256 green→yellow→red)
 *   NUM_GRADIENT + 1       status bar dark  (white text)
 *   NUM_GRADIENT + 2       status bar light (black text)  */
#define STATUS_PAIR_DARK  (NUM_GRADIENT + 1)
#define STATUS_PAIR_LIGHT (NUM_GRADIENT + 2)

void viz_init(void);
void viz_cleanup(void);
void viz_render(AudioState *audio, Config *cfg);
