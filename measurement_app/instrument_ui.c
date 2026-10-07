#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "instrument_ui.h"

static void history_placeholder(void)
{
    igSeparatorText("Measurement history");
    igTextWrapped("Measurements will appear after instrument communication is implemented.");
    if (igBeginChild_Str("history", (ImVec2){0, 180}, ImGuiChildFlags_Borders, 0)) {
        ImVec2 origin = igGetCursorScreenPos();
        ImVec2 size = igGetContentRegionAvail();
        ImDrawList *draw = igGetWindowDrawList();
        /* Empty axes only: no generated samples, trace or animation. */
        ImVec2 corner = {origin.x + 24, origin.y + size.y - 24};
        ImDrawList_AddLine(draw, (ImVec2){corner.x, origin.y + 12},
                           corner, 0xFF777777u, 1.0f);
        ImDrawList_AddLine(draw, corner,
                           (ImVec2){origin.x + size.x - 12, corner.y},
                           0xFF777777u, 1.0f);
        igTextDisabled("No measurement history");
        igDummy(size);
    }
    igEndChild();
}

void instrument_ui_draw(InstrumentState *state)
{
    ImGuiViewport *viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (igBegin("Measurement Instrument Control", NULL, flags)) {
        igTextUnformatted("Measurement Instrument Control", NULL);
        igSeparator();

        const char *instruments[] = {"AKIP-2205 Digital Multimeter"};
        igSetNextItemWidth(320);
        igCombo_Str_arr("Instrument", &state->instrument_index, instruments, 1, -1);
        igText("Connection status: Disconnected");
        igBeginDisabled(true);
        igSetNextItemWidth(320);
        igInputTextWithHint("Port", "Not configured", state->port,
                            sizeof(state->port), 0, NULL, NULL);
        igButton("Connect", (ImVec2){0, 0});
        igEndDisabled();

        igSeparatorText("Measurement");
        igTextUnformatted(state->has_measurement ? "Latest measurement" : "No data", NULL);
        igText("Mode: %s", state->has_measurement ? state->mode : "--");
        igText("Value: %s", state->has_measurement ? state->value : "--");
        igText("Unit: %s", state->has_measurement ? state->unit : "--");
        igBeginDisabled(true);
        igButton("Start acquisition", (ImVec2){0, 0});
        igSameLine(0, -1);
        igButton("Stop acquisition", (ImVec2){0, 0});
        igEndDisabled();

        history_placeholder();
        igSeparatorText("Log");
        if (igBeginChild_Str("log", (ImVec2){0, 72}, ImGuiChildFlags_Borders, 0)) {
            igTextUnformatted("GUI ready.", NULL);
            igTextWrapped("Instrument communication has not been implemented.");
        }
        igEndChild();
    }
    igEnd();
}
