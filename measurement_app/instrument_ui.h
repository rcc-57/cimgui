#ifndef INSTRUMENT_UI_H
#define INSTRUMENT_UI_H

#include <stdbool.h>

/* The future AKIP-2205 module will update this state on the GUI thread.
 * No instrument I/O belongs in the drawing code. */
typedef struct {
    bool has_measurement;
    char mode[64];
    char value[64];
    char unit[32];
    char port[128];
    int instrument_index;
} InstrumentState;

void instrument_ui_draw(InstrumentState *state);

#endif
