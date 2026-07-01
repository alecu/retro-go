#include "vs_host_bridge.h"

#include "rg_system.h"

#if defined(ESP_PLATFORM) && defined(RG_VS_SERIAL_UART_NUM)

#include <string.h>

#ifndef RG_VS_SERIAL_TX
#define RG_VS_SERIAL_TX 10
#endif
#ifndef RG_VS_SERIAL_RX
#define RG_VS_SERIAL_RX 9
#endif
#ifndef RG_VS_SERIAL_BAUD
#define RG_VS_SERIAL_BAUD 115200
#endif

#if RG_VS_ENABLE_TCP_BRIDGE

#include "rg_network.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <nvs.h>
#include <sys/socket.h>
#include <unistd.h>

#define VS_HOST_PORT 5005
#define VS_HOST_CONNECT_TIMEOUT_S 30

static bool bridge_initialized = false;
static int server_fd = -1;
static int client_fd = -1;

static void vs_host_try_accept(void)
{
    if (server_fd < 0)
        return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (fd < 0)
        return;

    if (client_fd >= 0)
        close(client_fd);

    client_fd = fd;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    RG_LOGI("vs_host: emulator connected\n");
}

void vs_host_bridge_init(void)
{
    if (bridge_initialized)
        return;
    bridge_initialized = true;

    rg_wifi_config_t config = {0};

    if (!rg_network_init())
    {
        RG_LOGE("vs_host: network init failed\n");
        return;
    }

    {
        nvs_handle_t h;
        if (nvs_open("voom_wifi", NVS_READONLY, &h) == ESP_OK)
        {
            size_t len = sizeof(config.ssid) - 1;
            nvs_get_blob(h, "ssid", config.ssid, &len);
            config.ssid[len] = '\0';

            len = sizeof(config.password) - 1;
            nvs_get_blob(h, "password", config.password, &len);
            config.password[len] = '\0';
            nvs_close(h);
        }
    }

    if (!config.ssid[0] && !rg_network_wifi_read_config(0, &config))
    {
        RG_LOGE("vs_host: no WiFi config found\n");
        return;
    }

    RG_LOGI("vs_host: connecting to '%s'...\n", config.ssid);
    rg_network_wifi_set_config(&config);

    if (!rg_network_wifi_start())
    {
        RG_LOGE("vs_host: WiFi start failed\n");
        return;
    }

    for (int i = 0; i < VS_HOST_CONNECT_TIMEOUT_S * 2; i++)
    {
        rg_network_t info = rg_network_get_info();
        if (info.state == RG_NETWORK_CONNECTED)
        {
            RG_LOGI("vs_host: WiFi connected, IP=%s\n", info.ip_addr);
            break;
        }
        rg_usleep(500000);
    }

    rg_network_t info = rg_network_get_info();
    if (info.state != RG_NETWORK_CONNECTED)
    {
        RG_LOGE("vs_host: WiFi connection timed out\n");
        return;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        RG_LOGE("vs_host: socket() failed: %d\n", errno);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(VS_HOST_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        RG_LOGE("vs_host: bind() failed: %d\n", errno);
        close(server_fd);
        server_fd = -1;
        return;
    }

    listen(server_fd, 1);

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    RG_LOGI("vs_host: TCP server listening on port %d\n", VS_HOST_PORT);
}

void vs_host_bridge_send(const char *line, const uint8_t *data, size_t len)
{
    vs_host_bridge_init();
    vs_host_try_accept();

    if (client_fd < 0)
        return;

    size_t hlen = strlen(line);
    char header[hlen + 2];
    memcpy(header, line, hlen);
    header[hlen] = '\n';
    header[hlen + 1] = '\0';

    if (send(client_fd, header, hlen + 1, 0) < 0)
        goto disconnect;

    if (data && len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            int n = send(client_fd, data + sent, len - sent, 0);
            if (n > 0)
            {
                sent += n;
            }
            else if (n < 0 && errno == EAGAIN)
            {
                rg_usleep(100);
            }
            else
            {
                goto disconnect;
            }
        }
    }
    return;

disconnect:
    RG_LOGW("vs_host: send failed (errno=%d), disconnecting\n", errno);
    close(client_fd);
    client_fd = -1;
}

int vs_host_bridge_recv_input(void)
{
    vs_host_bridge_init();
    vs_host_try_accept();

    if (client_fd < 0)
        return -1;

    uint8_t byte;
    int n = recv(client_fd, &byte, 1, 0);
    if (n == 1)
        return (int)byte;
    if (n < 0 && (errno == EAGAIN || errno == ENOTCONN))
        return -1;

    RG_LOGW("vs_host: client disconnected (n=%d errno=%d)\n", n, errno);
    close(client_fd);
    client_fd = -1;
    return -1;
}

bool vs_host_bridge_connected(void)
{
    vs_host_bridge_init();
    vs_host_try_accept();
    return client_fd >= 0;
}

#else

#include <driver/gpio.h>
#include <driver/uart.h>

#define VS_HOST_RX_BUF 256
#define VS_HOST_TX_RINGBUF 8192

static bool bridge_initialized = false;

void vs_host_bridge_init(void)
{
    if (bridge_initialized)
        return;
    bridge_initialized = true;

    const uart_config_t cfg = {
        .baud_rate = RG_VS_SERIAL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (!uart_is_driver_installed(RG_VS_SERIAL_UART_NUM))
    {
        ESP_ERROR_CHECK(uart_driver_install(RG_VS_SERIAL_UART_NUM, VS_HOST_RX_BUF, VS_HOST_TX_RINGBUF, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(RG_VS_SERIAL_UART_NUM, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(RG_VS_SERIAL_UART_NUM, RG_VS_SERIAL_TX, RG_VS_SERIAL_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }

    RG_LOGI("vs_host: UART%d host link up (tx=%d rx=%d, %d baud)\n",
            RG_VS_SERIAL_UART_NUM, RG_VS_SERIAL_TX, RG_VS_SERIAL_RX, RG_VS_SERIAL_BAUD);
}

void vs_host_bridge_send(const char *line, const uint8_t *data, size_t len)
{
    vs_host_bridge_init();
    uart_write_bytes(RG_VS_SERIAL_UART_NUM, line, strlen(line));
    uart_write_bytes(RG_VS_SERIAL_UART_NUM, "\n", 1);
    if (data && len)
        uart_write_bytes(RG_VS_SERIAL_UART_NUM, (const char *)data, len);
}

int vs_host_bridge_recv_input(void)
{
    vs_host_bridge_init();

    uint8_t byte;
    int n = uart_read_bytes(RG_VS_SERIAL_UART_NUM, &byte, 1, 0);
    return n == 1 ? (int)byte : -1;
}

bool vs_host_bridge_connected(void)
{
    vs_host_bridge_init();
    return true;
}

#endif

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

#endif
