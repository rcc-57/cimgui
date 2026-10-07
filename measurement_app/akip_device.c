#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include "akip_device.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    HANDLE port;
    OVERLAPPED read, write;
    bool reading, writing;
    uint8_t input[256], output[8];
    DWORD output_size;
} Transport;

static void reset_operation(OVERLAPPED *operation)
{
    HANDLE event = operation->hEvent;
    memset(operation, 0, sizeof(*operation));
    operation->hEvent = event;
    ResetEvent(event);
}

static void log_message(AkipDevice *d, const char *format, ...)
{
    size_t index = (d->log_start + d->log_count) % AKIP_LOG_LINES;
    if (d->log_count == AKIP_LOG_LINES) {
        index = d->log_start;
        d->log_start = (d->log_start + 1) % AKIP_LOG_LINES;
    } else ++d->log_count;
    va_list args;
    va_start(args, format);
    vsnprintf(d->log[index], sizeof(d->log[index]), format, args);
    va_end(args);
}

static void hex_text(char *out, size_t capacity, const uint8_t *data, size_t size)
{
    size_t offset = 0;
    out[0] = '\0';
    for (size_t i = 0; i < size && offset + 4 < capacity; ++i)
        offset += (size_t)snprintf(out + offset, capacity - offset, "%02X ", data[i]);
}

static void release_transport(AkipDevice *d)
{
    Transport *t = d->transport;
    if (!t) return;
    if (t->port != INVALID_HANDLE_VALUE) {
        /* Buffers/OVERLAPPED must stay alive until cancellation completes. */
        CancelIoEx(t->port, NULL);
        DWORD ignored;
        if (t->reading) GetOverlappedResult(t->port, &t->read, &ignored, TRUE);
        if (t->writing) GetOverlappedResult(t->port, &t->write, &ignored, TRUE);
        CloseHandle(t->port);
    }
    if (t->read.hEvent) CloseHandle(t->read.hEvent);
    if (t->write.hEvent) CloseHandle(t->write.hEvent);
    free(t);
    d->transport = NULL;
    d->pending_command = 0;
    d->acquiring = false;
    memset(&d->parser, 0, sizeof(d->parser));
}

static void failure(AkipDevice *d, const char *operation, DWORD code)
{
    snprintf(d->status, sizeof(d->status), "%s: Windows error %lu", operation, (unsigned long)code);
    log_message(d, "%s (2=not found, 5=access denied/port busy, 1167=disconnected)", d->status);
    release_transport(d);
    d->connection = AKIP_ERROR;
}

void akip_device_init(AkipDevice *d)
{
    memset(d, 0, sizeof(*d));
    d->interval = 0.4f; /* DMMVIEW_G manual: default log/plot interval is 400 ms. */
    d->history_function = -1;
    snprintf(d->status, sizeof(d->status), "Disconnected");
    log_message(d, "GUI ready. Select the Silicon Labs CP210x COM port for the wired meter.");
}

void akip_device_disconnect(AkipDevice *d)
{
    release_transport(d);
    d->connection = AKIP_DISCONNECTED;
    snprintf(d->status, sizeof(d->status), "Disconnected; retained readings are historical");
    log_message(d, "Port closed. Exit the application before unplugging USB (DMMVIEW_G manual).");
}

void akip_device_clear_history(AkipDevice *d)
{
    d->history_start = d->history_count = 0;
    d->history_function = -1;
    d->history_unit[0] = '\0';
}

static bool send_command(AkipDevice *d, AkipCommand command, double now)
{
    Transport *t = d->transport;
    if (!t || t->writing || d->pending_command) return false;
    t->output_size = (DWORD)(command == AKIP_COMMAND_RESET
        ? akip_reset_command(d->cr_only, (char *)t->output, sizeof(t->output))
        : akip_command(command, d->cr_only, t->output));
    if (!t->output_size) return false;
    reset_operation(&t->write);
    DWORD written = 0;
    BOOL success = WriteFile(t->port, t->output, t->output_size, &written, &t->write);
    if (!success && GetLastError() != ERROR_IO_PENDING) {
        failure(d, "WriteFile", GetLastError());
        return false;
    }
    t->writing = !success;
    if (success && written != t->output_size) {
        failure(d, "Short write", ERROR_WRITE_FAULT);
        return false;
    }
    d->pending_command = command;
    d->deadline = now + (command == AKIP_COMMAND_RESET ? 5.0 : 2.0);
    if (command == AKIP_COMMAND_READ) d->next_read = now + d->interval;
    hex_text(d->tx_hex, sizeof(d->tx_hex), t->output, t->output_size);
    if (command != AKIP_COMMAND_READ || !d->acquiring)
        log_message(d, "TX %s: %s", command == AKIP_COMMAND_ONLINE ? "ONL" :
                    command == AKIP_COMMAND_RESET ? "RST" : "RD?", d->tx_hex);
    return true;
}

bool akip_device_connect(AkipDevice *d, const char *port, bool cr_only, double now)
{
    if (d->transport) return false;
    /* Accept only explicit COM names, never arbitrary file/device paths. */
    if (strlen(port) < 4 || strlen(port) > 8 ||
        (port[0] != 'C' && port[0] != 'c') || (port[1] != 'O' && port[1] != 'o') ||
        (port[2] != 'M' && port[2] != 'm')) {
        log_message(d, "Enter a COM name, e.g. COM3 or COM12.");
        return false;
    }
    for (size_t i = 3; port[i]; ++i) if (port[i] < '0' || port[i] > '9') {
        log_message(d, "Invalid COM number: %s", port); return false;
    }
    int number = atoi(port + 3);
    if (number < 1 || number > 65535) { log_message(d, "COM number must be 1..65535."); return false; }
    Transport *t = calloc(1, sizeof(*t));
    if (!t) { failure(d, "Allocation", ERROR_NOT_ENOUGH_MEMORY); return false; }
    t->port = INVALID_HANDLE_VALUE;
    d->transport = t;
    wchar_t path[32];
    swprintf(path, sizeof(path) / sizeof(path[0]), L"\\\\.\\COM%d", number);
    t->port = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, NULL);
    if (t->port == INVALID_HANDLE_VALUE) { failure(d, "Open COM port", GetLastError()); return false; }
    t->read.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!t->read.hEvent) { failure(d, "Create read event", GetLastError()); return false; }
    t->write.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!t->write.hEvent) { failure(d, "Create write event", GetLastError()); return false; }
    DCB config = {0};
    config.DCBlength = sizeof(config);
    if (!GetCommState(t->port, &config)) { failure(d, "GetCommState", GetLastError()); return false; }
    config.BaudRate = CBR_9600;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = FALSE;
    config.fOutxCtsFlow = config.fOutxDsrFlow = FALSE;
    config.fDtrControl = DTR_CONTROL_DISABLE;
    config.fRtsControl = RTS_CONTROL_DISABLE;
    config.fDsrSensitivity = FALSE;
    config.fOutX = config.fInX = FALSE;
    config.fErrorChar = config.fNull = config.fAbortOnError = FALSE;
    if (!SetCommState(t->port, &config)) { failure(d, "SetCommState", GetLastError()); return false; }
    /* Immediate reads of already received bytes; writes are overlapped. */
    COMMTIMEOUTS timeout = {0};
    timeout.ReadIntervalTimeout = MAXDWORD;
    timeout.WriteTotalTimeoutConstant = 500;
    if (!SetCommTimeouts(t->port, &timeout) || !SetupComm(t->port, 4096, 4096) ||
        !PurgeComm(t->port, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
        failure(d, "Configure COM port", GetLastError()); return false;
    }
    d->cr_only = cr_only;
    d->connection = AKIP_CONNECTING;
    d->has_reading = false;
    d->received = d->rejected = d->timeouts = 0;
    d->received_bytes = 0;
    d->break_before_next = false;
    d->tx_hex[0] = d->rx_hex[0] = '\0';
    akip_device_clear_history(d);
    snprintf(d->status, sizeof(d->status), "Port open; waiting for ONL acknowledgement");
    log_message(d, "%s opened: 9600 8N1, no flow control, ending=%s", port, cr_only ? "CR" : "CRLF");
    return send_command(d, AKIP_COMMAND_ONLINE, now);
}

static void add_sample(AkipDevice *d, double now, bool valid, double value)
{
    size_t index = (d->history_start + d->history_count) % AKIP_HISTORY;
    if (d->history_count == AKIP_HISTORY) {
        index = d->history_start;
        d->history_start = (d->history_start + 1) % AKIP_HISTORY;
    } else ++d->history_count;
    d->history[index] = (AkipSample){now, value, valid};
}

static void accept_event(AkipDevice *d, const AkipEvent *e, double now)
{
    hex_text(d->rx_hex, sizeof(d->rx_hex), e->bytes, e->size);
    if (e->kind == AKIP_ACK && d->pending_command == AKIP_COMMAND_RESET) {
        d->pending_command = AKIP_COMMAND_NONE;
        d->next_read = now + 1.0; /* Conservative startup pause, not a device timing guarantee. */
        snprintf(d->status, sizeof(d->status), "Reset acknowledged; waiting to re-enter online mode");
        log_message(d, "RX RST ACK: %s. Verify the power-on/reset behavior on the meter.", d->rx_hex);
    } else if (e->kind == AKIP_ACK && d->pending_command == AKIP_COMMAND_ONLINE) {
        d->pending_command = 0;
        d->connection = AKIP_ONLINE;
        d->timeouts = 0;
        snprintf(d->status, sizeof(d->status), "Online: ACK received (model not identified)");
        log_message(d, "RX ACK: %s. Read once to verify measurements against the meter.", d->rx_hex);
    } else if (e->kind == AKIP_READING && d->pending_command == AKIP_COMMAND_READ) {
        if (d->history_function != e->reading.function ||
            strcmp(d->history_unit, e->reading.si_unit) != 0) {
            akip_device_clear_history(d);
            d->history_function = e->reading.function;
            snprintf(d->history_unit, sizeof(d->history_unit), "%s", e->reading.si_unit);
            log_message(d, "Mode changed: %s; chart starts a new series.", e->reading.mode);
        }
        d->reading = e->reading;
        d->has_reading = true;
        d->last_read_time = now;
        ++d->received;
        d->timeouts = 0;
        d->pending_command = 0;
        if (d->break_before_next && d->history_count) add_sample(d, now, false, 0);
        d->break_before_next = false;
        add_sample(d, now, d->reading.numeric, d->reading.si_value);
        snprintf(d->status, sizeof(d->status), "Online: valid RD frame received");
        if (d->received == 1 || !d->acquiring)
            log_message(d, "RX RD: %s %s %s", d->reading.mode, d->reading.value[0], d->reading.unit);
    } else if (e->kind == AKIP_NAK && d->pending_command) {
        AkipCommand rejected_command = d->pending_command;
        log_message(d, "RX NAK: command %s rejected; acquisition stopped.",
                    rejected_command == AKIP_COMMAND_ONLINE ? "ONL" :
                    rejected_command == AKIP_COMMAND_RESET ? "RST" : "RD?");
        bool handshake = rejected_command != AKIP_COMMAND_READ;
        d->pending_command = 0;
        d->acquiring = false;
        if (handshake) {
            release_transport(d);
            d->connection = AKIP_ERROR;
            snprintf(d->status, sizeof(d->status), "%s rejected (NAK)",
                     rejected_command == AKIP_COMMAND_RESET ? "RST" : "ONL");
        } else {
            log_message(d, "For RD NAK: leave memory/record-browsing mode on the meter.");
            add_sample(d, now, false, 0);
            snprintf(d->status, sizeof(d->status), "Online; RD rejected (NAK); last reading is historical");
        }
    } else {
        ++d->rejected;
        if (d->rejected <= 3 || d->rejected % 100 == 0)
            log_message(d, "Unexpected/invalid frame (%u): %s", d->rejected, d->rx_hex);
    }
}

void akip_device_tick(AkipDevice *d, double now)
{
    Transport *t = d->transport;
    if (!t) return;
    if (t->writing) {
        DWORD count;
        if (GetOverlappedResult(t->port, &t->write, &count, FALSE)) {
            t->writing = false;
            if (count != t->output_size) { failure(d, "Short write", ERROR_WRITE_FAULT); return; }
        } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
            failure(d, "Write completion", GetLastError()); return;
        }
    }
    DWORD errors;
    COMSTAT stat;
    if (!ClearCommError(t->port, &errors, &stat)) { failure(d, "COM disconnected", GetLastError()); return; }
    if (errors) { failure(d, "Serial framing/overflow error", ERROR_INVALID_DATA); return; }
    DWORD count = 0;
    if (t->reading) {
        if (GetOverlappedResult(t->port, &t->read, &count, FALSE)) t->reading = false;
        else {
            DWORD error = GetLastError();
            if (error != ERROR_IO_INCOMPLETE) { failure(d, "Read completion", error); return; }
            count = 0;
        }
    } else {
        reset_operation(&t->read);
        if (!ReadFile(t->port, t->input, sizeof(t->input), &count, &t->read)) {
            if (GetLastError() != ERROR_IO_PENDING) { failure(d, "ReadFile", GetLastError()); return; }
            t->reading = true;
            count = 0;
        }
    }
    if (count) {
        d->received_bytes += count;
        hex_text(d->rx_hex, sizeof(d->rx_hex), t->input, count);
    }
    for (DWORD i = 0; i < count; ++i) {
        AkipEvent event;
        if (akip_parser_feed(&d->parser, t->input[i], &event)) accept_event(d, &event, now);
        if (!d->transport) return;
    }
    if (d->pending_command && now >= d->deadline) {
        ++d->timeouts;
        log_message(d, "Response timeout (%u). Last TX: %s; last RX: %s", d->timeouts, d->tx_hex, d->rx_hex);
        /* Close instead of issuing a new request: the protocol has no sequence
         * number, so a delayed reply must not be mistaken for a new sample. */
        release_transport(d);
        d->connection = AKIP_ERROR;
        snprintf(d->status, sizeof(d->status), "Response timeout; disconnected. Check port/cable/CRLF setting.");
        return;
    }
    if (d->connection == AKIP_RESETTING && !d->pending_command && now >= d->next_read) {
        if (send_command(d, AKIP_COMMAND_ONLINE, now)) {
            d->connection = AKIP_CONNECTING;
            snprintf(d->status, sizeof(d->status), "Reconnecting after reset; waiting for ONL ACK");
        }
    } else if (d->connection == AKIP_ONLINE && d->acquiring && !d->pending_command && now >= d->next_read)
        send_command(d, AKIP_COMMAND_READ, now);
}

void akip_device_read_once(AkipDevice *d, double now)
{
    if (d->connection == AKIP_ONLINE) send_command(d, AKIP_COMMAND_READ, now);
}

bool akip_device_reset(AkipDevice *d, double now)
{
    if (d->connection != AKIP_ONLINE || d->pending_command || d->acquiring) return false;
    if (!send_command(d, AKIP_COMMAND_RESET, now)) return false;
    d->connection = AKIP_RESETTING;
    d->has_reading = false;
    akip_device_clear_history(d);
    snprintf(d->status, sizeof(d->status), "RST sent; waiting for reset acknowledgement");
    log_message(d, "Reset requested by user. Acquisition remains stopped; old history cleared.");
    return true;
}

void akip_device_start(AkipDevice *d, double now)
{
    if (d->connection != AKIP_ONLINE) return;
    /* Also break on resume if a final in-flight response arrived after Stop. */
    if (d->history_count) d->break_before_next = true;
    d->acquiring = true;
    d->next_read = now;
    log_message(d, "Acquisition started (interval %.2f s).", (double)d->interval);
}

void akip_device_stop(AkipDevice *d)
{
    d->acquiring = false;
    d->break_before_next = true;
    log_message(d, "Acquisition stopped. An in-flight request may deliver one final reading.");
}

int akip_device_ports(char ports[AKIP_PORTS][16], char labels[AKIP_PORTS][192])
{
    /* QueryDosDevice enumerates names without opening instruments. */
    char *names = malloc(65536);
    if (!names) return -1;
    DWORD length = QueryDosDeviceA(NULL, names, 65536);
    if (!length) { free(names); return -1; }
    int count = 0;
    for (const char *name = names; *name && count < AKIP_PORTS; name += strlen(name) + 1) {
        if (strncmp(name, "COM", 3) != 0 || strlen(name) > 8 || name[3] < '0' || name[3] > '9') continue;
        bool valid = true;
        for (size_t i = 3; name[i]; ++i) if (name[i] < '0' || name[i] > '9') valid = false;
        if (valid) snprintf(ports[count++], 16, "%s", name);
    }
    free(names);
    /* Numeric order: COM2 before COM10. */
    for (int i = 0; i < count; ++i) for (int j = i + 1; j < count; ++j)
        if (atoi(ports[i] + 3) > atoi(ports[j] + 3)) {
            char tmp[16]; memcpy(tmp, ports[i], 16); memcpy(ports[i], ports[j], 16); memcpy(ports[j], tmp, 16);
        }
    for (int i = 0; i < count; ++i) snprintf(labels[i], 192, "%s", ports[i]);
    /* Show the same friendly device names as Device Manager, as instructed
     * by the DMMVIEW_G manual. All registry access here is read-only. */
    HDEVINFO devices = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devices == INVALID_HANDLE_VALUE) return count; /* COM names still usable */
    for (DWORD index = 0; ; ++index) {
        SP_DEVINFO_DATA info = {0};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices, index, &info)) break;
        wchar_t device_class[64] = {0};
        DWORD type;
        if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_CLASS, &type,
                (BYTE *)device_class, sizeof(device_class), NULL) || type != REG_SZ ||
            _wcsicmp(device_class, L"Ports") != 0) continue;
        HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t port[16] = {0};
        DWORD bytes = sizeof(port);
        LONG result = RegQueryValueExW(key, L"PortName", NULL, &type, (BYTE *)port, &bytes);
        RegCloseKey(key);
        if (result != ERROR_SUCCESS || type != REG_SZ) continue;
        port[15] = L'\0';
        char name[16];
        if (!WideCharToMultiByte(CP_UTF8, 0, port, -1, name, sizeof(name), NULL, NULL)) continue;
        for (int i = 0; i < count; ++i) {
            if (strcmp(ports[i], name) != 0) continue;
            wchar_t friendly[256] = {0};
            if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_FRIENDLYNAME, &type,
                    (BYTE *)friendly, sizeof(friendly), NULL)) continue;
            friendly[255] = L'\0';
            char text[160];
            if (type == REG_SZ && WideCharToMultiByte(CP_UTF8, 0, friendly, -1,
                    text, sizeof(text), NULL, NULL))
                snprintf(labels[i], 192, "%s | %s", name, text);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return count;
}

bool akip_device_export(AkipDevice *d, const char *path)
{
    /* Exclusive creation preserves any existing export. */
    wchar_t wide_path[260];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, 260)) {
        log_message(d, "Invalid UTF-8 export path."); return false;
    }
    HANDLE file = CreateFileW(wide_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { log_message(d, "CSV creation failed (Windows error %lu): %s", (unsigned long)GetLastError(), path); return false; }
    bool success = true;
    const char *header = "app_elapsed_seconds,value_si,unit,valid,function\r\n";
    DWORD count;
    DWORD length = (DWORD)strlen(header);
    if (!WriteFile(file, header, length, &count, NULL) || count != length) success = false;
    for (size_t i = 0; success && i < d->history_count; ++i) {
        AkipSample sample = d->history[(d->history_start + i) % AKIP_HISTORY];
        char row[160], value[40] = "";
        if (sample.valid) snprintf(value, sizeof(value), "%.12g", sample.value);
        int n = snprintf(row, sizeof(row), "%.6f,%s,%s,%d,%02d\r\n", sample.time,
                         value, d->history_unit, sample.valid ? 1 : 0, d->history_function);
        length = (DWORD)n;
        if (!WriteFile(file, row, length, &count, NULL) || count != length) success = false;
    }
    CloseHandle(file);
    log_message(d, "%s: %s", success ? "CSV saved" : "CSV write failed (file may be partial)", path);
    return success;
}
