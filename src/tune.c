#include "tune.h"
#include <stdlib.h>
#include <string.h>

float ipr_tune[TU_COUNT] = {
#define X(n, d) (float)(d),
    IPR_TUNE_LIST(X)
#undef X
};
static const char *NAMES[TU_COUNT] = {
#define X(n, d) #n,
    IPR_TUNE_LIST(X)
#undef X
};
static int done;

void ipr_tune_init(void) {
    if (done) return;
    done = 1;
    const char *e = getenv("IPR_TUNE");
    if (!e) return;
    char buf[4096];
    strncpy(buf, e, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (char *tok = strtok(buf, ", ;\n"); tok; tok = strtok(NULL, ", ;\n")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < TU_COUNT; i++)
            if (!strcmp(tok, NAMES[i])) { ipr_tune[i] = (float)atof(eq + 1); break; }
    }
}
