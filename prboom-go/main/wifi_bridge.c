#include "wifi_bridge.h"
#include "rg_system.h"
#include "rg_network.h"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <nvs.h>

#define WB_PORT 5005
#define WB_CONNECT_TIMEOUT_S 30

static int server_fd = -1;
static int client_fd = -1;

static void wb_try_accept(void)
{
    if (server_fd < 0)
        return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (fd < 0)
        return; // EAGAIN or error - no pending connection

    if (client_fd >= 0)
        close(client_fd);

    client_fd = fd;
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    RG_LOGI("wb: emulator connected\n");
}

void wb_init(void)
{
    rg_wifi_config_t config = {0};

    if (!rg_network_init())
    {
        RG_LOGE("wb: network init failed\n");
        return;
    }

    // MicroPython writes credentials to "voom_wifi" NVS namespace before
    // booting prboom-go (see native_apps.py _sync_wifi_to_nvs()).
    {
        nvs_handle_t h;
        if (nvs_open("voom_wifi", NVS_READONLY, &h) == ESP_OK)
        {
            size_t len;
            len = sizeof(config.ssid) - 1;
            nvs_get_blob(h, "ssid", config.ssid, &len);
            config.ssid[len] = '\0';
            len = sizeof(config.password) - 1;
            nvs_get_blob(h, "password", config.password, &len);
            config.password[len] = '\0';
            nvs_close(h);
            if (config.ssid[0])
                RG_LOGI("wb: WiFi config from NVS (voom_wifi): ssid='%s'\n", config.ssid);
        }
    }

    if (!config.ssid[0] && !rg_network_wifi_read_config(0, &config))
    {
        RG_LOGE("wb: no WiFi config found (tried voom_wifi NVS and rg_settings slot 0)\n");
        return;
    }

    RG_LOGI("wb: connecting to '%s'...\n", config.ssid);
    rg_network_wifi_set_config(&config);

    if (!rg_network_wifi_start())
    {
        RG_LOGE("wb: WiFi start failed\n");
        return;
    }

    for (int i = 0; i < WB_CONNECT_TIMEOUT_S * 2; i++)
    {
        rg_network_t info = rg_network_get_info();
        if (info.state == RG_NETWORK_CONNECTED)
        {
            RG_LOGI("wb: WiFi connected, IP=%s\n", info.ip_addr);
            break;
        }
        rg_usleep(500000);
    }

    rg_network_t info = rg_network_get_info();
    if (info.state != RG_NETWORK_CONNECTED)
    {
        RG_LOGE("wb: WiFi connection timed out\n");
        return;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        RG_LOGE("wb: socket() failed: %d\n", errno);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WB_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        RG_LOGE("wb: bind() failed: %d\n", errno);
        close(server_fd);
        server_fd = -1;
        return;
    }

    listen(server_fd, 1);

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    RG_LOGI("wb: TCP server listening on port %d\n", WB_PORT);
}

bool wb_connected(void)
{
    wb_try_accept();
    return client_fd >= 0;
}

void wb_send(const char *line, const uint8_t *data, size_t len)
{
    if (client_fd < 0)
        return;

    static int send_count = 0;
    if (++send_count <= 5 || send_count % 100 == 0)
        RG_LOGD("wb: send #%d cmd='%s' data=%zu bytes\n", send_count, line, len);

    // Send header: "line\n"
    size_t hlen = strlen(line);
    char header[hlen + 2];
    memcpy(header, line, hlen);
    header[hlen]     = '\n';
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
    RG_LOGW("wb: send failed (errno=%d), disconnecting\n", errno);
    close(client_fd);
    client_fd = -1;
}

int wb_recv_input(void)
{
    // Accept new connections every tick so the emulator can connect even before
    // the first frame is sent (e.g. when the fan isn't spinning yet).
    wb_try_accept();

    if (client_fd < 0)
        return -1;

    uint8_t byte;
    int n = recv(client_fd, &byte, 1, 0);
    if (n == 1)
        return (int)byte;
    // EAGAIN = no data yet; ENOTCONN = TCP handshake still in progress after accept()
    if (n < 0 && (errno == EAGAIN || errno == ENOTCONN))
        return -1;

    // n == 0 means clean close; n < 0 with other errno means error
    RG_LOGW("wb: client disconnected (n=%d errno=%d)\n", n, errno);
    close(client_fd);
    client_fd = -1;
    return -1;
}
