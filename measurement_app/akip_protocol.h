#ifndef AKIP_PROTOCOL_H
#define AKIP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int function;
    int range[3];
    char mode[32];
    char value[3][8];
    char unit[24];
    char si_unit[16];
    bool overload;
    bool numeric; /* false if the unit/range is ambiguous in the supplied PDF */
    double si_value;
} AkipReading;

typedef enum { AKIP_ACK, AKIP_NAK, AKIP_READING, AKIP_INVALID } AkipEventKind;
typedef struct {
    AkipEventKind kind;
    AkipReading reading;
    uint8_t bytes[96]; /* complete frame without CR/LF, including binary ACK/NAK */
    size_t size;
} AkipEvent;
typedef struct {
    uint8_t bytes[96];
    size_t size;
} AkipParser;

typedef enum {
    AKIP_COMMAND_NONE,
    AKIP_COMMAND_ONLINE,
    AKIP_COMMAND_READ,
    AKIP_COMMAND_RESET
} AkipCommand;

/* Appendix 1 uses <> as notation, not transmitted bytes.
 * Examples: 23 4F 4E 4C 0D 0A, 23 52 44 3F 0D 0A. */
/* Returns wire byte count, excluding the trailing C-string NUL. */
size_t akip_command(AkipCommand command, bool cr_only, uint8_t out[8]);
/* Example of a user action converted to a documented instrument command.
 * Returns 0 if the output buffer is too small. No I/O happens here. */
size_t akip_reset_command(bool cr_only, char *out, size_t capacity);
bool akip_parser_feed(AkipParser *parser, uint8_t byte, AkipEvent *event);
bool akip_decode(const uint8_t *payload, size_t size, AkipReading *reading);

#endif
