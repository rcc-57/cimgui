#ifndef INSTRUMENT_UI_H
#define INSTRUMENT_UI_H

#include "akip_device.h"

typedef struct {
    AkipDevice device;
    char port[16];
    char ports[AKIP_PORTS][16];
    char port_labels[AKIP_PORTS][192];
    int port_count, port_index, terminator_index;
    int instrument_index;
    char export_path[260];
    bool auto_scroll;
} InstrumentState;

void instrument_ui_init(InstrumentState *state);
void instrument_ui_draw(InstrumentState *state, double now);

#endif
