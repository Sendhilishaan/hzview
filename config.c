#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

void config_default(Config *cfg) {
    cfg->sensitivity = 1.0f;
    cfg->bar_width   = 2;
    cfg->freq_min    = 40;
    cfg->freq_max    = 14000;
    cfg->light_mode  = false;
}

static bool parse_bool(const char *s) {
    return strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "yes") == 0;
}

int config_load(Config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char key[64] = {0}, val[64] = {0};
        if (sscanf(p, "%63[^= \t\n] = %63[^\n#]", key, val) != 2) continue;

        /* trim trailing whitespace from val */
        int vlen = (int)strlen(val);
        while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t')) val[--vlen] = '\0';

        if      (strcmp(key, "sensitivity") == 0) cfg->sensitivity = (float)atof(val);
        else if (strcmp(key, "bar_width")   == 0) cfg->bar_width   = atoi(val);
        else if (strcmp(key, "freq_min")    == 0) cfg->freq_min    = atoi(val);
        else if (strcmp(key, "freq_max")    == 0) cfg->freq_max    = atoi(val);
        else if (strcmp(key, "light_mode")  == 0) cfg->light_mode  = parse_bool(val);
    }

    fclose(f);
    return 0;
}

int config_save(const Config *cfg, const char *path) {
    /* ensure parent directory exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755); /* ok if already exists */
    }

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f,
        "# hzview configuration\n"
        "# edit and restart to apply\n\n"
        "sensitivity  = %.2f\n"
        "bar_width    = %d\n\n"
        "# frequency range (Hz)\n"
        "freq_min     = %d\n"
        "freq_max     = %d\n\n"
        "light_mode   = %s\n",
        cfg->sensitivity,
        cfg->bar_width,
        cfg->freq_min,
        cfg->freq_max,
        cfg->light_mode ? "true" : "false"
    );

    fclose(f);
    return 0;
}

const char *config_default_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(path, sizeof(path), "%s/.config/hzview/config", home);
    return path;
}
