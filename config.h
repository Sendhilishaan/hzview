#pragma once

#include <stdbool.h>

typedef struct {
    float       sensitivity;       /* 0.1 – 5.0 */
    int         bar_width;         /* 1 – 8 */
    int         freq_min;          /* Hz */
    int         freq_max;          /* Hz */
    bool        light_mode;        /* light terminal background */
} Config;

void        config_default(Config *cfg);
int         config_load(Config *cfg, const char *path);
int         config_save(const Config *cfg, const char *path);
const char *config_default_path(void);
