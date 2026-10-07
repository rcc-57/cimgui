#include "akip_protocol.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t akip_command(AkipCommand command, bool cr_only, uint8_t out[8])
{
    const char *text;
    switch (command) {
    case AKIP_COMMAND_ONLINE: text = "#ONL\r\n"; break;
    case AKIP_COMMAND_READ: text = "#RD?\r\n"; break;
    case AKIP_COMMAND_RESET: text = "#RST\r\n"; break;
    default: out[0] = '\0'; return 0;
    }
    size_t length = cr_only ? 5 : 6;
    memcpy(out, text, length);
    out[length] = '\0';
    return length;
}

size_t akip_reset_command(bool cr_only, char *out, size_t capacity)
{
    size_t length = cr_only ? 5 : 6;
    if (!out || capacity <= length) return 0;
    uint8_t command[8];
    akip_command(AKIP_COMMAND_RESET, cr_only, command);
    memcpy(out, command, length + 1);
    return length;
}

static bool decimal(const char *text, double *value)
{
    /* strtod alone also accepts exponents/NaN/Inf. The wire format doesn't. */
    const char *p = text;
    if (*p == '+' || *p == '-') ++p;
    bool digit = false, point = false;
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digit = true;
        else if (*p == '.' && !point) point = true;
        else return false;
    }
    if (!digit) return false;
    char *end;
    *value = strtod(text, &end);
    return *end == '\0' && isfinite(*value);
}

bool akip_decode(const uint8_t *data, size_t size, AkipReading *r)
{
    if (size != 26 || data[0] < '0' || data[0] > '3' ||
        data[1] < '0' || data[1] > '9') return false;
    memset(r, 0, sizeof(*r));
    r->function = (data[0] - '0') * 10 + data[1] - '0';
    if (r->function > 33) return false; /* 34 is memory browsing, not live data */
    for (int channel = 0; channel < 3; ++channel) {
        size_t offset = 2 + (size_t)channel * 8;
        r->range[channel] = data[offset] >= '0' && data[offset] <= '9'
                          ? data[offset] - '0' : -1;
        /* Secondary fields can contain unused/status bytes. Preserve them
         * for diagnostics without guessing their numeric interpretation. */
        for (int i = 0; i < 7; ++i) {
            uint8_t byte = data[offset + 1 + (size_t)i];
            if (channel == 0 && (byte < 32 || byte > 126)) return false;
            r->value[channel][i] = byte >= 32 && byte <= 126 ? (char)byte : '?';
        }
    }
    static const char *const modes[] = {
        "AC voltage", "dBm (V)", "AC millivolts", "dBm (mV)",
        "DC voltage", "DC millivolts", "Thermocouple", "Continuity",
        "Diode", "Resistance", "RTD temperature", "Capacitance",
        "Frequency / duty", "DC microamps", "DC milliamps", "DC amps",
        "AC microamps", "AC milliamps", "AC amps", "Peak DC mV",
        "Peak DC V", "Peak DC uA", "Peak DC mA", "Peak DC A",
        "AC mV + Hz", "AC V + Hz", "AC uA + Hz", "AC mA + Hz",
        "AC A + Hz", "AC+DC mV", "AC+DC V", "AC+DC uA",
        "AC+DC mA", "AC+DC A"
    };
    snprintf(r->mode, sizeof(r->mode), "%s", modes[r->function]);
    int range = r->range[0], max_range = 0;
    const char *unit = "", *si = "";
    double factor = 1;
    bool known = true;
    switch (r->function) {
    case 0: case 4: case 20: case 30: unit = si = "V"; max_range = 3; break;
    case 2: case 5: case 19: case 29: unit = "mV"; si = "V"; factor = 1e-3; break;
    case 1: unit = si = "dBm"; max_range = 3; break;
    case 3: unit = si = "dBm"; break;
    case 6: case 10:
        /* The PDF doesn't define the bytes of the Celsius/Fahrenheit flag. */
        unit = "deg (see meter)"; known = false; break;
    case 7: unit = si = "Ohm"; break;
    case 8: unit = si = "V"; break;
    case 9:
        max_range = 6; si = "Ohm";
        unit = range == 0 ? "Ohm" : range <= 3 ? "kOhm" : "MOhm";
        factor = range == 0 ? 1 : range <= 3 ? 1e3 : 1e6;
        break;
    case 11:
        max_range = 7; si = "F";
        unit = range <= 2 ? "nF" : "uF";
        factor = range <= 2 ? 1e-9 : 1e-6;
        /* Appendix 2 repeats range 5 and disagrees with Appendix 5. */
        if (range >= 5) { unit = "range? (see meter)"; known = false; }
        break;
    case 12: case 24: case 25: case 26: case 27: case 28:
        max_range = 6; si = "Hz";
        unit = range <= 2 ? "Hz" : range <= 5 ? "kHz" : "MHz";
        factor = range <= 2 ? 1 : range <= 5 ? 1e3 : 1e6;
        break;
    case 13: case 16: case 21: case 31:
        unit = "uA"; si = "A"; factor = 1e-6; max_range = 1; break;
    case 14: case 17: case 22:
        unit = "mA"; si = "A"; factor = 1e-3; max_range = 1; break;
    case 32:
        unit = "mA"; si = "A"; factor = 1e-3; max_range = 3;
        /* Appendix 2 lists 2/3, Appendix 5 lists 0/1 for this mode. */
        break;
    case 15: case 18: case 23:
        unit = si = "A"; max_range = 1; break;
    case 33:
        unit = si = "A"; max_range = 5;
        /* Conflicting 4/5 vs 0/1 in the supplied document. */
        if (range > 1 && range < 4) return false;
        break;
    default: return false;
    }
    if (range < 0 || range > max_range) return false;
    snprintf(r->unit, sizeof(r->unit), "%s", unit);
    snprintf(r->si_unit, sizeof(r->si_unit), "%s", si);
    r->overload = memcmp(r->value[0], "FFFFFFF", 7) == 0;
    double number = 0;
    if (!r->overload && !decimal(r->value[0], &number)) return false;
    r->numeric = !r->overload && known;
    r->si_value = number * factor;
    return true;
}

bool akip_parser_feed(AkipParser *p, uint8_t byte, AkipEvent *e)
{
    if (byte == '#') { p->size = 1; p->bytes[0] = '#'; return false; }
    if (!p->size) return false;
    if (byte != '\r' && byte != '\n') {
        if (p->size == sizeof(p->bytes)) { p->size = 0; return false; }
        p->bytes[p->size++] = byte;
        return false;
    }
    memset(e, 0, sizeof(*e));
    e->kind = AKIP_INVALID;
    e->size = p->size;
    memcpy(e->bytes, p->bytes, p->size);
    if (p->size == 3 && p->bytes[2] == 0 && p->bytes[1] == 0x06) e->kind = AKIP_ACK;
    else if (p->size == 3 && p->bytes[2] == 0 && p->bytes[1] == 0x15) e->kind = AKIP_NAK;
    else if (p->size >= 3 && p->bytes[1] == 'R' && p->bytes[2] == 'D') {
        size_t offset = 3;
        /* Some presentations insert a single space before the 26-byte data. */
        if (p->size == 30 && p->bytes[3] == ' ') offset = 4;
        if (akip_decode(p->bytes + offset, p->size - offset, &e->reading))
            e->kind = AKIP_READING;
    }
    p->size = 0;
    return true;
}
