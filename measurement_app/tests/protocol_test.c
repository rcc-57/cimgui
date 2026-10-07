/* Synthetic byte fixtures test the decoder only. They never enter the GUI. */
#include "akip_protocol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

static void payload(uint8_t out[26], int function, int range, const char value[7])
{
    memset(out, ' ', 26);
    out[0] = (uint8_t)('0' + function / 10);
    out[1] = (uint8_t)('0' + function % 10);
    out[2] = (uint8_t)('0' + range);
    out[10] = out[18] = '0';
    memcpy(out + 3, value, 7);
}

static int feed(AkipParser *parser, const uint8_t *bytes, size_t size, AkipEvent *event)
{
    int events = 0;
    for (size_t i = 0; i < size; ++i)
        if (akip_parser_feed(parser, bytes[i], event)) ++events;
    return events;
}

int main(void)
{
    uint8_t data[26], command[8];
    AkipReading reading;
    CHECK(akip_command(AKIP_COMMAND_ONLINE, false, command) == 6);
    CHECK(memcmp(command, "#ONL\r\n", 6) == 0);
    CHECK(akip_command(AKIP_COMMAND_READ, true, command) == 5);
    CHECK(memcmp(command, "#RD?\r", 5) == 0);
    CHECK(command[5] == '\0');
    char reset[8];
    CHECK(akip_reset_command(false, reset, sizeof(reset)) == 6);
    CHECK(strcmp(reset, "#RST\r\n") == 0);
    CHECK(akip_reset_command(true, reset, sizeof(reset)) == 5);
    CHECK(strcmp(reset, "#RST\r") == 0);
    CHECK(akip_reset_command(false, reset, 6) == 0);
    CHECK(akip_reset_command(false, NULL, sizeof(reset)) == 0);
    CHECK(akip_command(AKIP_COMMAND_NONE, false, command) == 0);

    payload(data, 4, 0, "+1.2345");
    CHECK(akip_decode(data, 26, &reading));
    CHECK(reading.function == 4 && reading.numeric && !reading.overload);
    CHECK(strcmp(reading.unit, "V") == 0 && fabs(reading.si_value - 1.2345) < 1e-10);
    payload(data, 5, 0, "-123.45");
    CHECK(akip_decode(data, 26, &reading));
    CHECK(strcmp(reading.unit, "mV") == 0 && fabs(reading.si_value + 0.12345) < 1e-10);
    payload(data, 9, 4, "+1.2345");
    CHECK(akip_decode(data, 26, &reading));
    CHECK(strcmp(reading.unit, "MOhm") == 0 && fabs(reading.si_value - 1234500) < 1e-7);
    payload(data, 12, 3, "+1.2345");
    CHECK(akip_decode(data, 26, &reading));
    CHECK(strcmp(reading.si_unit, "Hz") == 0 && fabs(reading.si_value - 1234.5) < 1e-7);
    payload(data, 13, 0, "+123.45");
    CHECK(akip_decode(data, 26, &reading));
    CHECK(fabs(reading.si_value - 0.00012345) < 1e-12);
    payload(data, 4, 0, "FFFFFFF");
    CHECK(akip_decode(data, 26, &reading) && reading.overload && !reading.numeric);
    payload(data, 6, 0, "+023.45");
    CHECK(akip_decode(data, 26, &reading) && !reading.numeric);
    payload(data, 11, 5, "+123.45");
    CHECK(akip_decode(data, 26, &reading) && !reading.numeric);
    payload(data, 11, 2, "+123.45");
    CHECK(akip_decode(data, 26, &reading) && reading.numeric);
    CHECK(fabs(reading.si_value - 123.45e-9) < 1e-15);
    payload(data, 34, 0, "+1.2345");
    CHECK(!akip_decode(data, 26, &reading));
    payload(data, 4, 9, "+1.2345");
    CHECK(!akip_decode(data, 26, &reading));
    payload(data, 4, 0, "nan    ");
    CHECK(!akip_decode(data, 26, &reading));
    payload(data, 4, 0, "+1.2345");
    CHECK(!akip_decode(data, 25, &reading));
    data[3] = 0;
    CHECK(!akip_decode(data, 26, &reading));

    AkipParser parser = {0};
    AkipEvent event;
    const uint8_t ack[] = {'#', 0x06, 0x00, '\r', '\n'};
    const uint8_t nak[] = {'#', 0x15, 0x00, '\r'};
    CHECK(feed(&parser, ack, 2, &event) == 0);
    CHECK(feed(&parser, ack + 2, sizeof(ack) - 2, &event) == 1 && event.kind == AKIP_ACK);
    CHECK(feed(&parser, nak, sizeof(nak), &event) == 1 && event.kind == AKIP_NAK);
    uint8_t frame[31] = {'#', 'R', 'D'};
    payload(frame + 3, 4, 0, "+1.2345");
    frame[29] = '\r'; frame[30] = '\n';
    CHECK(feed(&parser, frame, 14, &event) == 0);
    CHECK(feed(&parser, frame + 14, 17, &event) == 1 && event.kind == AKIP_READING);
    CHECK(fabs(event.reading.si_value - 1.2345) < 1e-10);
    CHECK(event.size == 29);
    uint8_t spaced[32] = {'#', 'R', 'D', ' '};
    memcpy(spaced + 4, frame + 3, 26); spaced[30] = '\r'; spaced[31] = '\n';
    CHECK(feed(&parser, spaced, sizeof(spaced), &event) == 1 && event.kind == AKIP_READING);
    CHECK(feed(&parser, (const uint8_t *)"noise\r#RDbad\r", 13, &event) == 1 && event.kind == AKIP_INVALID);
    uint8_t flood[200]; memset(flood, 'x', sizeof(flood)); flood[0] = '#';
    CHECK(feed(&parser, flood, sizeof(flood), &event) == 0);
    CHECK(feed(&parser, ack, sizeof(ack), &event) == 1 && event.kind == AKIP_ACK);
    /* A truncated frame must not pollute the next frame after resynchronization. */
    CHECK(feed(&parser, frame, 7, &event) == 0);
    CHECK(feed(&parser, ack, sizeof(ack), &event) == 1 && event.kind == AKIP_ACK);
    CHECK(feed(&parser, frame, sizeof(frame), &event) == 1);
    CHECK(feed(&parser, frame, sizeof(frame), &event) == 1);
    if (failures) return 1;
    puts("Protocol tests passed (no device required).");
    return 0;
}
