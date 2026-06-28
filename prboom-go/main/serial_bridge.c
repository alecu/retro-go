#include "serial_bridge.h"
#include "rg_system.h" // pulls in the target config.h (RG_VS_SERIAL_*)

#include <string.h>
#include <driver/uart.h>
#include <driver/gpio.h> // GPIO_NUM_* used by RG_VS_SERIAL_TX/RX

#ifndef RG_VS_SERIAL_UART_NUM
#define RG_VS_SERIAL_UART_NUM 2
#endif
#ifndef RG_VS_SERIAL_TX
#define RG_VS_SERIAL_TX 10
#endif
#ifndef RG_VS_SERIAL_RX
#define RG_VS_SERIAL_RX 9
#endif
#ifndef RG_VS_SERIAL_BAUD
#define RG_VS_SERIAL_BAUD 115200
#endif

#define SB_RX_BUF 256
#define SB_TX_BUF 256

void sb_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = RG_VS_SERIAL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(RG_VS_SERIAL_UART_NUM, SB_RX_BUF, SB_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RG_VS_SERIAL_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(RG_VS_SERIAL_UART_NUM, RG_VS_SERIAL_TX, RG_VS_SERIAL_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    RG_LOGI("sb: UART%d host link up (tx=%d rx=%d, %d baud)\n",
            RG_VS_SERIAL_UART_NUM, RG_VS_SERIAL_TX, RG_VS_SERIAL_RX, RG_VS_SERIAL_BAUD);
}

void sb_send(const char *line, const uint8_t *data, size_t len)
{
    // Same framing as serialcomms.send(): "line" + "\n" + optional binary payload.
    uart_write_bytes(RG_VS_SERIAL_UART_NUM, line, strlen(line));
    uart_write_bytes(RG_VS_SERIAL_UART_NUM, "\n", 1);
    if (data && len)
        uart_write_bytes(RG_VS_SERIAL_UART_NUM, (const char *)data, len);
}

bool sb_connected(void)
{
    // The serial link is point-to-point and always considered up.
    return true;
}

int sb_recv_input(void)
{
    uint8_t byte;
    int n = uart_read_bytes(RG_VS_SERIAL_UART_NUM, &byte, 1, 0);
    return n == 1 ? (int)byte : -1;
}
