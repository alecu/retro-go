#include "emu_audio_bridge.h"
#include "rg_system.h" // pulls in the target config.h and RG_LOG*

#include <string.h>
#include <stdio.h>

// Only the Ventilastation target has a UART host link. On every other target
// (SDL desktop builds, other boards) the whole bridge compiles to the no-op
// stubs at the bottom, so the chip taps cost nothing there.
#if defined(RG_VS_ENABLE_HOST_BRIDGE)

#include <driver/uart.h>
#include "vs_board_config.h"

// One emulated video frame's worth of encoded register writes. The sustained
// link budget is ~190 bytes/frame at 60fps; this cap is far above that so a
// busy/bursty frame is never silently truncated mid-stream — instead we stop
// appending and count the drop (visible in the periodic stats log).
#define EMU_WIRE_MAX 4096

// UART TX ring buffer: a frame chunk is handed to the driver and drained
// asynchronously so emu_audio_frame_end() does not block the core-0 game loop
// waiting on the 115200 line.
#define EMU_TX_RINGBUF 8192

static bool     active      = false;
static uint8_t  wire[EMU_WIRE_MAX];
static int      wire_len    = 0;
static uint16_t last_idx    = 0;
static int      dropped     = 0;

// Periodic bandwidth stats.
static int64_t  stat_t0     = 0;
static int      stat_bytes  = 0;
static int      stat_frames = 0;
static int      stat_drops  = 0;
static bool     uart_ready  = false;
static vs_board_config_t board_config;

static void emu_uart_init(void)
{
    if (uart_ready)
        return;
    if (!vs_board_config_load(&board_config)) {
        RG_LOGE("emu_audio: board configuration missing; run make configure-board\n");
        return;
    }
    const uart_config_t cfg = {
        .baud_rate = board_config.serial_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Tolerate an already-installed driver (e.g. if a serial input bridge is
    // ever added on the same UART): install only once.
    if (!uart_is_driver_installed(board_config.serial_uart)) {
        ESP_ERROR_CHECK(uart_driver_install(board_config.serial_uart, 256, EMU_TX_RINGBUF, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(board_config.serial_uart, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(board_config.serial_uart, board_config.serial_tx, board_config.serial_rx,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }
    uart_ready = true;
}

static void emu_send(const char *line, const uint8_t *data, size_t len)
{
    // Same framing as the Doom bridge: "<line>\n" then optional binary payload.
    uart_write_bytes(board_config.serial_uart, line, strlen(line));
    uart_write_bytes(board_config.serial_uart, "\n", 1);
    if (data && len)
        uart_write_bytes(board_config.serial_uart, (const char *)data, len);
}

void emu_audio_begin(const char *system)
{
    emu_uart_init();
    if (!uart_ready)
        return;
    char line[32];
    snprintf(line, sizeof(line), "achip %s", system ? system : "unknown");
    emu_send(line, NULL, 0);
    active      = true;
    wire_len    = 0;
    last_idx    = 0;
    dropped     = 0;
    stat_t0     = rg_system_timer();
    stat_bytes  = stat_frames = stat_drops = 0;
    RG_LOGI("emu_audio: started (%s) on UART%d @ %d baud\n",
            system ? system : "?", (int)board_config.serial_uart,
            (int)board_config.serial_baud);
}

void emu_audio_end(void)
{
    if (!active)
        return;
    emu_send("astop", NULL, 0);
    active = false;
    RG_LOGI("emu_audio: stopped\n");
}

void emu_audio_frame_begin(void)
{
    wire_len = 0;
    last_idx = 0;
}

void emu_audio_write(uint8_t op, uint8_t val, uint16_t sample_idx)
{
    if (!active)
        return;
    // delta = samples since the previous write this frame (clamped >= 0; the
    // two chips share one monotonic sample timeline but tiny inversions between
    // independent counters are possible).
    uint32_t delta = (sample_idx >= last_idx) ? (uint32_t)(sample_idx - last_idx) : 0;
    last_idx = sample_idx;

    // Worst case a record is 5 (varint) + 1 (op) + 1 (val) = 7 bytes.
    if (wire_len + 7 > EMU_WIRE_MAX) {
        dropped++;
        return;
    }
    // LEB128 unsigned varint for the sample delta.
    while (delta >= 0x80) {
        wire[wire_len++] = (uint8_t)(delta | 0x80);
        delta >>= 7;
    }
    wire[wire_len++] = (uint8_t)delta;
    wire[wire_len++] = op;
    wire[wire_len++] = val;
}

void emu_audio_frame_end(uint16_t nsamples)
{
    if (!active)
        return;

    // Nothing happened this frame: still advance the host's clock by emitting an
    // empty frame so its render stays time-aligned and the stream never stalls.
    char line[40];
    snprintf(line, sizeof(line), "aframe %d %d", wire_len, (int)nsamples);
    emu_send(line, wire_len ? wire : NULL, wire_len);

    stat_bytes += wire_len + (int)strlen(line) + 1;
    stat_frames++;
    stat_drops += dropped;
    dropped = 0;

    int64_t now = rg_system_timer();
    if (now - stat_t0 >= 3000000) {
        float secs = (now - stat_t0) / 1000000.0f;
        RG_LOGI("emu_audio: %.0f B/s, %.1f fps, %d dropped writes\n",
                stat_bytes / secs, stat_frames / secs, stat_drops);
        stat_t0 = now;
        stat_bytes = stat_frames = stat_drops = 0;
    }
}

#else // no UART host link on this target — inert stubs

void emu_audio_begin(const char *system) { (void)system; }
void emu_audio_end(void) {}
void emu_audio_frame_begin(void) {}
void emu_audio_write(uint8_t op, uint8_t val, uint16_t sample_idx) { (void)op; (void)val; (void)sample_idx; }
void emu_audio_frame_end(uint16_t nsamples) { (void)nsamples; }

#endif
