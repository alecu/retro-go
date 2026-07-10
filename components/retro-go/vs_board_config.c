#include "vs_board_config.h"

#ifdef ESP_PLATFORM

#include <nvs.h>
#include <nvs_flash.h>

#define VS_BOARD_NAMESPACE "vs_board"

bool vs_board_config_load(vs_board_config_t *config)
{
    if (!config || nvs_flash_init() != ESP_OK)
        return false;

    nvs_handle_t nvs;
    if (nvs_open(VS_BOARD_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return false;

    bool ok =
        nvs_get_i32(nvs, "hall_gpio", &config->hall_gpio) == ESP_OK &&
        nvs_get_i32(nvs, "irdiode_gpio", &config->irdiode_gpio) == ESP_OK &&
        nvs_get_i32(nvs, "led_spi_host", &config->led_spi_host) == ESP_OK &&
        nvs_get_i32(nvs, "led_clk", &config->led_clk) == ESP_OK &&
        nvs_get_i32(nvs, "led_mosi", &config->led_mosi) == ESP_OK &&
        nvs_get_i32(nvs, "led_cs", &config->led_cs) == ESP_OK &&
        nvs_get_i32(nvs, "led_freq", &config->led_freq) == ESP_OK &&
        nvs_get_i32(nvs, "serial_uart", &config->serial_uart) == ESP_OK &&
        nvs_get_i32(nvs, "serial_tx", &config->serial_tx) == ESP_OK &&
        nvs_get_i32(nvs, "serial_rx", &config->serial_rx) == ESP_OK &&
        nvs_get_i32(nvs, "serial_baud", &config->serial_baud) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

#else

bool vs_board_config_load(vs_board_config_t *config)
{
    (void)config;
    return false;
}

#endif
