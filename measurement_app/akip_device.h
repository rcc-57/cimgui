#ifndef AKIP_DEVICE_H
#define AKIP_DEVICE_H

#include "akip_protocol.h"

#define AKIP_HISTORY 1024
#define AKIP_LOG_LINES 64
#define AKIP_PORTS 128

typedef enum { AKIP_DISCONNECTED, AKIP_CONNECTING, AKIP_ONLINE, AKIP_RESETTING, AKIP_ERROR } AkipConnection;
typedef struct {
    double time, value;
    bool valid; /* false draws a gap instead of a made-up numeric value */
} AkipSample;
typedef struct {
    void *transport; /* Win32 handles and overlapped buffers, owned by akip_device.c */
    AkipParser parser;
    AkipConnection connection;
    bool acquiring, has_reading, cr_only, break_before_next;
    AkipCommand pending_command;
    float interval;
    double deadline, next_read, last_read_time;
    unsigned timeouts, received, rejected;
    unsigned long long received_bytes;
    AkipReading reading;
    AkipSample history[AKIP_HISTORY];
    size_t history_start, history_count;
    int history_function;
    char history_unit[16];
    char status[160], tx_hex[64], rx_hex[300];
    char log[AKIP_LOG_LINES][240];
    size_t log_start, log_count;
} AkipDevice;

void akip_device_init(AkipDevice *device);
bool akip_device_connect(AkipDevice *device, const char *port, bool cr_only, double now);
void akip_device_disconnect(AkipDevice *device);
void akip_device_tick(AkipDevice *device, double now);
void akip_device_read_once(AkipDevice *device, double now);
bool akip_device_reset(AkipDevice *device, double now);
void akip_device_start(AkipDevice *device, double now);
void akip_device_stop(AkipDevice *device);
void akip_device_clear_history(AkipDevice *device);
bool akip_device_export(AkipDevice *device, const char *path);
/* Lists existing COM names without opening or probing any of them. */
int akip_device_ports(char ports[AKIP_PORTS][16], char labels[AKIP_PORTS][192]);

#endif
