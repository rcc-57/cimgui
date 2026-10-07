#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "instrument_ui.h"
#include <stdio.h>
#include <string.h>

void instrument_ui_init(InstrumentState *s)
{
    memset(s, 0, sizeof(*s));
    akip_device_init(&s->device);
    s->port_count = akip_device_ports(s->ports, s->port_labels);
    s->port_index = -1; /* Explicit selection avoids defaulting to another device. */
    snprintf(s->export_path, sizeof(s->export_path), "measurement_history.csv");
    s->auto_scroll = true;
}

static AkipSample sample_at(const AkipDevice *d, size_t index)
{
    return d->history[(d->history_start + index) % AKIP_HISTORY];
}

static void copy_diagnostics(const InstrumentState *s)
{
    const AkipDevice *d = &s->device;
    char text[18000];
    int offset = snprintf(text, sizeof(text),
        "AKIP-2205 / DMMVIEW_G\nPort: %s; 9600 8N1; ending: %s\nStatus: %s\n"
        "Measurements: %u; RX bytes: %llu; rejected: %u\nTX: %s\nRX: %s\n",
        s->port, d->cr_only ? "CR" : "CRLF", d->status,
        d->received, d->received_bytes, d->rejected, d->tx_hex, d->rx_hex);
    for (size_t i = 0; i < d->log_count && offset > 0 && (size_t)offset < sizeof(text); ++i)
        offset += snprintf(text + offset, sizeof(text) - (size_t)offset, "%s\n",
                           d->log[(d->log_start + i) % AKIP_LOG_LINES]);
    igSetClipboardText(text);
}

static void measurement_history(AkipDevice *d)
{
    igSeparatorText("Measurement history");
    igText("%zu / %d history entries. Numeric trace in %s; X = seconds.",
           d->history_count, AKIP_HISTORY, d->history_unit[0] ? d->history_unit : "known units only");
    if (igBeginChild_Str("history", (ImVec2){0, 190}, ImGuiChildFlags_Borders, 0)) {
        ImVec2 origin = igGetCursorScreenPos(), size = igGetContentRegionAvail();
        ImVec2 top = {origin.x + 76, origin.y + 16};
        ImVec2 bottom = {origin.x + size.x - 12, origin.y + size.y - 26};
        ImDrawList *draw = igGetWindowDrawList();
        ImDrawList_AddLine(draw, top, (ImVec2){top.x, bottom.y}, 0xFF777777u, 1);
        ImDrawList_AddLine(draw, (ImVec2){top.x, bottom.y}, bottom, 0xFF777777u, 1);
        size_t valid = 0;
        double min = 0, max = 0;
        for (size_t i = 0; i < d->history_count; ++i) {
            AkipSample sample = sample_at(d, i);
            if (!sample.valid) continue;
            if (!valid || sample.value < min) min = sample.value;
            if (!valid || sample.value > max) max = sample.value;
            ++valid;
        }
        if (!valid) {
            igTextDisabled("No numeric measurement history");
        } else if (bottom.x > top.x && bottom.y > top.y) {
            double span = max - min;
            double pad = span > 0 ? span * 0.1 : (min < 0 ? -min : min) * 0.01 + 1e-9;
            min -= pad; max += pad;
            double first = sample_at(d, 0).time;
            double duration = sample_at(d, d->history_count - 1).time - first;
            if (duration <= 0) duration = 1;
            bool previous_valid = false;
            ImVec2 previous = {0, 0};
            for (size_t i = 0; i < d->history_count; ++i) {
                AkipSample sample = sample_at(d, i);
                if (!sample.valid) { previous_valid = false; continue; }
                ImVec2 point = {
                    top.x + (float)((sample.time - first) / duration) * (bottom.x - top.x),
                    bottom.y - (float)((sample.value - min) / (max - min)) * (bottom.y - top.y)
                };
                if (previous_valid) ImDrawList_AddLine(draw, previous, point, 0xFF80D080u, 1.5f);
                ImDrawList_AddRectFilled(draw, (ImVec2){point.x - 2, point.y - 2},
                                        (ImVec2){point.x + 2, point.y + 2}, 0xFF80D080u, 0, 0);
                previous = point; previous_valid = true;
            }
            char label[48];
            snprintf(label, sizeof(label), "%.5g", max);
            ImDrawList_AddText_Vec2(draw, (ImVec2){origin.x, top.y}, 0xFFCCCCCCu, label, NULL);
            snprintf(label, sizeof(label), "%.5g", min);
            ImDrawList_AddText_Vec2(draw, (ImVec2){origin.x, bottom.y - 12}, 0xFFCCCCCCu, label, NULL);
            snprintf(label, sizeof(label), "0 ... %.1f s", duration);
            ImDrawList_AddText_Vec2(draw, (ImVec2){top.x, bottom.y + 6}, 0xFFCCCCCCu, label, NULL);
        }
        igDummy(size);
    }
    igEndChild();
    if (igButton("Clear history", (ImVec2){0, 0})) akip_device_clear_history(d);
}

void instrument_ui_draw(InstrumentState *s, double now)
{
    AkipDevice *d = &s->device;
    ImGuiViewport *viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (igBegin("Measurement Instrument Control", NULL, flags)) {
        igTextUnformatted("Measurement Instrument Control", NULL);
        const char *instruments[] = {"AKIP-2205 Digital Multimeter"};
        igSetNextItemWidth(320);
        igCombo_Str_arr("Instrument", &s->instrument_index, instruments, 1, -1);
        igTextWrapped("Connection: %s", d->status);

        igBeginDisabled(d->transport != NULL);
        if (s->port_count > 0) {
            const char *names[AKIP_PORTS];
            for (int i = 0; i < s->port_count; ++i) names[i] = s->port_labels[i];
            igSetNextItemWidth(450);
            if (igCombo_Str_arr("Available COM ports", &s->port_index, names, s->port_count, -1))
                snprintf(s->port, sizeof(s->port), "%s", s->ports[s->port_index]);
        } else igTextDisabled("No COM ports found; check Windows Device Manager.");
        igSameLine(0, -1);
        if (igButton("Refresh ports", (ImVec2){0, 0})) {
            s->port_count = akip_device_ports(s->ports, s->port_labels);
            s->port_index = -1;
            for (int i = 0; i < s->port_count; ++i)
                if (strcmp(s->ports[i], s->port) == 0) s->port_index = i;
        }
        igSetNextItemWidth(160);
        igInputText("Port", s->port, sizeof(s->port), 0, NULL, NULL);
        igSameLine(0, -1);
        const char *endings[] = {"CRLF (default)", "CR (compatibility)"};
        igSetNextItemWidth(190);
        igCombo_Str_arr("Command ending", &s->terminator_index, endings, 2, -1);
        igBeginDisabled(s->port[0] == '\0');
        if (igButton("Connect", (ImVec2){0, 0}))
            akip_device_connect(d, s->port, s->terminator_index == 1, now);
        igEndDisabled();
        igEndDisabled();
        igSameLine(0, -1);
        igBeginDisabled(d->transport == NULL);
        if (igButton("Disconnect", (ImVec2){0, 0})) akip_device_disconnect(d);
        igEndDisabled();
        igTextDisabled("9600 baud, 8N1, no flow control. Only the selected port is opened.");
        igTextDisabled("Wired AKIP-2205: select CP210x USB to UART Bridge. Pair software is for wireless only.");

        igSeparatorText("Measurement");
        if (d->has_reading) {
            igText("Mode: %s (code %02d, range %d)", d->reading.mode, d->reading.function, d->reading.range[0]);
            igText("Value: %s   Unit: %s", d->reading.overload ? "OL / overrange" : d->reading.value[0], d->reading.unit);
            igText("Last response: %.1f s ago; %s", now - d->last_read_time,
                   d->connection != AKIP_ONLINE || !d->acquiring ? "retained reading" : "acquisition active");
            if (!d->reading.numeric && !d->reading.overload)
                igTextWrapped("Unit encoding is ambiguous in the PDF; verify on the meter. No numeric point is plotted.");
        } else igTextUnformatted("No data. Connect, then press Read once or Start acquisition.", NULL);
        igBeginDisabled(d->connection != AKIP_ONLINE || d->pending_command != 0 || d->acquiring);
        if (igButton("Read once", (ImVec2){0, 0})) akip_device_read_once(d, now);
        igSameLine(0, -1);
        if (igButton("Start acquisition", (ImVec2){0, 0})) akip_device_start(d, now);
        igSameLine(0, -1);
        if (igButton("Reset instrument", (ImVec2){0, 0})) igOpenPopup_Str("Confirm instrument reset", 0);
        igEndDisabled();
        igSameLine(0, -1);
        igBeginDisabled(!d->acquiring);
        if (igButton("Stop acquisition", (ImVec2){0, 0})) akip_device_stop(d);
        igEndDisabled();
        if (igBeginPopupModal("Confirm instrument reset", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            igTextWrapped("RST is documented as equivalent to powering on the meter.\n"
                          "Old GUI readings/history will be cleared. Observe the actual meter.");
            igBeginDisabled(d->connection != AKIP_ONLINE || d->pending_command || d->acquiring);
            if (igButton("Send RST", (ImVec2){0, 0})) {
                akip_device_reset(d, now);
                igCloseCurrentPopup();
            }
            igEndDisabled();
            igSameLine(0, -1);
            if (igButton("Cancel", (ImVec2){0, 0})) igCloseCurrentPopup();
            igEndPopup();
        }
        igSetNextItemWidth(220);
        igSliderFloat("Polling interval", &d->interval, 0.4f, 5.0f, "%.2f s", 0);
        igTextDisabled("Change measurement mode/range with the meter's front-panel controls.");

        measurement_history(d);
        igSetNextItemWidth(320);
        igInputText("CSV path (new file)", s->export_path, sizeof(s->export_path), 0, NULL, NULL);
        igSameLine(0, -1);
        igBeginDisabled(d->history_count == 0 || s->export_path[0] == '\0');
        if (igButton("Export CSV", (ImVec2){0, 0})) akip_device_export(d, s->export_path);
        igEndDisabled();

        igSeparatorText("Log / diagnostics");
        igText("Measurements: %u; RX bytes: %llu; invalid/unexpected: %u; request: %s", d->received, d->received_bytes, d->rejected,
               d->pending_command == AKIP_COMMAND_ONLINE ? "ONL pending" :
               d->pending_command == AKIP_COMMAND_READ ? "RD? pending" :
               d->pending_command == AKIP_COMMAND_RESET ? "RST pending" : "idle");
        igTextWrapped("Last TX hex: %s", d->tx_hex[0] ? d->tx_hex : "--");
        igTextWrapped("Last RX hex (frame/chunk, may be truncated): %s", d->rx_hex[0] ? d->rx_hex : "--");
        igCheckbox("Auto-scroll log", &s->auto_scroll);
        igSameLine(0, -1);
        if (igButton("Copy diagnostics", (ImVec2){0, 0})) copy_diagnostics(s);
        if (igBeginChild_Str("log", (ImVec2){0, 100}, ImGuiChildFlags_Borders, 0)) {
            for (size_t i = 0; i < d->log_count; ++i)
                igTextWrapped("%s", d->log[(d->log_start + i) % AKIP_LOG_LINES]);
            if (s->auto_scroll) igSetScrollHereY(1.0f);
        }
        igEndChild();
    }
    igEnd();
}
