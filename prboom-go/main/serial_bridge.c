#include "serial_bridge.h"
#include "rg_system.h" // pulls in the target config.h and RG_LOG*

#include <string.h>
#include <driver/uart.h>
#include "vs_board_config.h"

#define SB_RX_BUF 256
#define SB_TX_BUF 256

static bool serial_ready = false;
static vs_board_config_t board_config;

void sb_init(void)
{
    if (serial_ready)
        return;
    if (!vs_board_config_load(&board_config)) {
        RG_LOGE("sb: board configuration missing; run make configure-board\n");
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

    ESP_ERROR_CHECK(uart_driver_install(board_config.serial_uart, SB_RX_BUF, SB_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(board_config.serial_uart, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(board_config.serial_uart, board_config.serial_tx, board_config.serial_rx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    serial_ready = true;

    RG_LOGI("sb: UART%d host link up (tx=%d rx=%d, %d baud)\n",
            (int)board_config.serial_uart, (int)board_config.serial_tx,
            (int)board_config.serial_rx, (int)board_config.serial_baud);
}

void sb_send(const char *line, const uint8_t *data, size_t len)
{
    sb_init();
    if (!serial_ready)
        return;
    // Same framing as serialcomms.send(): "line" + "\n" + optional binary payload.
    uart_write_bytes(board_config.serial_uart, line, strlen(line));
    uart_write_bytes(board_config.serial_uart, "\n", 1);
    if (data && len)
        uart_write_bytes(board_config.serial_uart, (const char *)data, len);
}

bool sb_connected(void)
{
    sb_init();
    return serial_ready;
}

int sb_recv_input(void)
{
    sb_init();
    if (!serial_ready)
        return -1;
    uint8_t byte;
    int n = uart_read_bytes(board_config.serial_uart, &byte, 1, 0);
    return n == 1 ? (int)byte : -1;
}
