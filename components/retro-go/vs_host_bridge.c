#include "vs_host_bridge.h"

#include "rg_system.h"

#if defined(ESP_PLATFORM) && defined(RG_VS_ENABLE_HOST_BRIDGE)

#include <string.h>

#include "vs_board_config.h"

// ---- Input protocol v2 byte-stream parser ----
// Feeds the UART transport below. Mirrors
// apps/micropython/ventilastation/input_parser.py: joystick frames
// ('*' + 3 data bytes) and command lines (alnum-prefixed, '\n'-terminated)
// share the same byte stream (see docs/internals/input-protocol-v2.md in vsdk).
typedef enum
{
    VS_SCAN,
    VS_JOY,
    VS_CMD,
} vs_parse_state_t;

#define VS_CMD_MAX 256

static vs_parse_state_t vs_state = VS_SCAN;
static uint8_t vs_joy_buf[3];
static int vs_joy_pos = 0;
static char vs_cmd_buf[VS_CMD_MAX + 1];
static int vs_cmd_len = 0;

static uint8_t vs_joy1 = 0;
static uint8_t vs_joy2 = 0;
static uint8_t vs_extra = 0;

static void vs_handle_command(const char *cmd)
{
    if (strcmp(cmd, "reset") == 0)
    {
        rg_system_restart();
        return;
    }

    if (strncmp(cmd, "ota_start ", 10) == 0)
    {
        const char *url = cmd + 10;
        FILE *f = fopen(RG_STORAGE_ROOT "/ota_request", "w");
        if (f)
        {
            fputs(url, f);
            fclose(f);
        }
        rg_system_restart();
        return;
    }

    // Every other command (eg. wifi_config) is silently ignored -- native
    // apps only care about joystick state, reset, and OTA.
}

static void vs_feed_bytes(const uint8_t *data, int n)
{
    for (int i = 0; i < n; ++i)
    {
        uint8_t b = data[i];
        switch (vs_state)
        {
        case VS_SCAN:
            if (b == 0x2A)
            {
                vs_state = VS_JOY;
                vs_joy_pos = 0;
            }
            else if ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z'))
            {
                vs_state = VS_CMD;
                vs_cmd_len = 0;
                vs_cmd_buf[vs_cmd_len++] = (char)b;
            }
            break;

        case VS_JOY:
            vs_joy_buf[vs_joy_pos++] = b;
            if (vs_joy_pos == 3)
            {
                vs_joy1 = vs_joy_buf[0] & 0x7F;
                vs_joy2 = vs_joy_buf[1] & 0x7F;
                vs_extra = vs_joy_buf[2] & 0x7F;
                vs_state = VS_SCAN;
            }
            break;

        case VS_CMD:
            if (b == 0x0A)
            {
                vs_cmd_buf[vs_cmd_len] = '\0';
                if (vs_cmd_len > 0)
                    vs_handle_command(vs_cmd_buf);
                vs_state = VS_SCAN;
            }
            else if (vs_cmd_len >= VS_CMD_MAX)
            {
                vs_state = VS_SCAN; // drop an over-long command, resync on next '*'/alnum
            }
            else
            {
                vs_cmd_buf[vs_cmd_len++] = (char)b;
            }
            break;
        }
    }
}

uint8_t vs_host_bridge_get_joy2(void)
{
    return vs_joy2;
}

uint8_t vs_host_bridge_get_extra(void)
{
    return vs_extra;
}


#include <driver/gpio.h>
#include <driver/uart.h>

#define VS_HOST_RX_BUF 256
#define VS_HOST_TX_RINGBUF 8192

static bool bridge_initialized = false;
static vs_board_config_t board_config;

void vs_host_bridge_init(void)
{
    if (bridge_initialized)
        return;
    if (!vs_board_config_load(&board_config)) {
        RG_LOGE("vs_host: board configuration missing; run make configure-board\n");
        return;
    }
    bridge_initialized = true;

    const uart_config_t cfg = {
        .baud_rate = board_config.serial_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (!uart_is_driver_installed(board_config.serial_uart))
    {
        ESP_ERROR_CHECK(uart_driver_install(board_config.serial_uart, VS_HOST_RX_BUF, VS_HOST_TX_RINGBUF, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(board_config.serial_uart, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(board_config.serial_uart, board_config.serial_tx, board_config.serial_rx,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }

    RG_LOGI("vs_host: UART%d host link up (tx=%d rx=%d, %d baud)\n",
            (int)board_config.serial_uart, (int)board_config.serial_tx,
            (int)board_config.serial_rx, (int)board_config.serial_baud);
}

void vs_host_bridge_send(const char *line, const uint8_t *data, size_t len)
{
    vs_host_bridge_init();
    if (!bridge_initialized)
        return;
    uart_write_bytes(board_config.serial_uart, line, strlen(line));
    uart_write_bytes(board_config.serial_uart, "\n", 1);
    if (data && len)
        uart_write_bytes(board_config.serial_uart, (const char *)data, len);
}

int vs_host_bridge_recv_input(void)
{
    vs_host_bridge_init();
    if (!bridge_initialized)
        return -1;

    uint8_t buf[64];
    int n;
    while ((n = uart_read_bytes(board_config.serial_uart, buf, sizeof(buf), 0)) > 0)
        vs_feed_bytes(buf, n);

    return (int)vs_joy1;
}

bool vs_host_bridge_connected(void)
{
    vs_host_bridge_init();
    return bridge_initialized;
}


#else

void vs_host_bridge_init(void) {}

void vs_host_bridge_send(const char *line, const uint8_t *data, size_t len)
{
    (void)line;
    (void)data;
    (void)len;
}

int vs_host_bridge_recv_input(void)
{
    return -1;
}

bool vs_host_bridge_connected(void)
{
    return false;
}

uint8_t vs_host_bridge_get_joy2(void)
{
    return 0;
}

uint8_t vs_host_bridge_get_extra(void)
{
    return 0;
}

#endif
