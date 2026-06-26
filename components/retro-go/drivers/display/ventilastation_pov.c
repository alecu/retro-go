#include "ventilastation_pov.h"

#if RG_VENTILASTATION_POV_ENABLED

#include <math.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "intensidades.h"
#include "rg_system.h"

#define RG_VS_COLUMNS 256
#define RG_VS_PIXELS 54
#define RG_VS_FASTEST_CREDIBLE_TURN_US 10000
#define RG_VS_TAU 6.28318530717958647692

static uint8_t *vs_data = NULL;
static uint32_t *vs_palette = NULL;
static uint16_t *vs_projection_table = NULL;
static spi_device_handle_t spi_handle;
static char *spi_buf = NULL;
static uint32_t *extra_buf = NULL;
static uint32_t *pixels0 = NULL;
static uint32_t *pixels1 = NULL;
static int buf_size = 0;
static int screen_width = 0;
static int screen_height = 0;
static int last_column = 0;

static volatile int64_t last_turn = 0;
static volatile int64_t last_turn_duration = 102400;

static rg_vs_tcp_send_fn      tcp_send_fn      = NULL;
static rg_vs_tcp_connected_fn tcp_connected_fn = NULL;

void rg_vs_pov_set_tcp_bridge(rg_vs_tcp_send_fn send_fn, rg_vs_tcp_connected_fn connected_fn)
{
    tcp_send_fn      = send_fn;
    tcp_connected_fn = connected_fn;
}

#define VS_TCP_FRAME_DIVISOR 2

// Send the current vs_palette to the emulator in ABGR byte order (A, B, G, R
// per entry) so the emulator's set_palettes() can apply its byte-swap and
// produce the correct RGBA value for OpenGL.
static void vs_tcp_send_palette(void)
{
    if (!tcp_send_fn || !tcp_connected_fn || !tcp_connected_fn())
        return;

    uint8_t buf[256 * 4];
    for (int i = 0; i < 256; i++)
    {
        uint32_t c = vs_palette[i]; // 0x00RRGGBB
        buf[i * 4 + 0] = 0xFF;                 // A - fully opaque
        buf[i * 4 + 1] = (c >> 0)  & 0xFF;    // B
        buf[i * 4 + 2] = (c >> 8)  & 0xFF;    // G
        buf[i * 4 + 3] = (c >> 16) & 0xFF;    // R
    }
    tcp_send_fn("palette 1", buf, sizeof(buf));
}

// Project the current vs_data framebuffer to 256 columns × 54 palette
// indices and send as a "frame" command.  Called every VS_TCP_FRAME_DIVISOR
// Doom frames so we don't swamp the WiFi link.
static void vs_tcp_send_frame(void)
{
    static int frame_count = 0;
    static bool was_connected = false;

    if (++frame_count % VS_TCP_FRAME_DIVISOR != 0)
        return;

    if (!tcp_send_fn || !tcp_connected_fn || !tcp_connected_fn())
    {
        was_connected = false;
        return;
    }

    // Resend palette whenever the emulator reconnects (it missed the initial one).
    if (!was_connected)
    {
        was_connected = true;
        RG_LOGI("vs_pov: emulator (re)connected — resending palette\n");
        vs_tcp_send_palette();
    }

    static uint8_t tcp_frame[RG_VS_COLUMNS * RG_VS_PIXELS];
    for (int angle = 0; angle < RG_VS_COLUMNS; angle++)
    {
        for (int led = 0; led < RG_VS_PIXELS; led++)
        {
            uint16_t pos = vs_projection_table[angle * RG_VS_PIXELS + led];
            int x = ((pos >> 8) & 0xFF) - 128 + screen_width / 2;
            int y = (pos & 0xFF)        - 128 + screen_height / 2;
            tcp_frame[angle * RG_VS_PIXELS + led] = vs_data[y * screen_width + x];
        }
    }
    RG_LOGI("vs_pov: sending frame #%d (%d bytes)\n", frame_count / VS_TCP_FRAME_DIVISOR, (int)sizeof(tcp_frame));
    tcp_send_fn("frame", tcp_frame, sizeof(tcp_frame));
}

static void vs_setup_projection_table(void) {
    int center_x = screen_width / 2;
    int center_y = screen_height / 2;
    int radius = RG_MIN(center_x, center_y) - 2;

    for (int angle = 0; angle < RG_VS_COLUMNS; angle++) {
        for (int led = 0; led < RG_VS_PIXELS; led++) {
            int x = 128 + (int)(radius * (led + 1) / RG_VS_PIXELS * cos(angle * RG_VS_TAU / RG_VS_COLUMNS));
            int y = 128 + (int)(radius * (led + 1) / RG_VS_PIXELS * sin(angle * RG_VS_TAU / RG_VS_COLUMNS));
            vs_projection_table[angle * RG_VS_PIXELS + led] = (x << 8) + y;
        }
    }
}

static void project_angle(int angle, uint32_t row[RG_VS_PIXELS]) {
    for (int led = 0; led < RG_VS_PIXELS; led++) {
        uint16_t pos = vs_projection_table[angle * RG_VS_PIXELS + led];
        int x = ((pos >> 8) & 0xff) - 128 + screen_width / 2;
        int y = (pos & 0xff) - 128 + screen_height / 2;
        uint8_t px = vs_data[y * screen_width + x];
        uint32_t doom_color = vs_palette[px];
        int level = intensidades_por_led[led];
        uint32_t color = (brillos[led] & 0x1f) | 0xe0 |
            intensidades[level][(doom_color & 0xff0000) >> 16] << 24 |
            intensidades[level][(doom_color & 0x00ff00) >> 8] << 16 |
            intensidades[level][doom_color & 0x0000ff] << 8;
        row[led] = color;
    }
}

static void spi_start_buses(void) {
    esp_err_t ret;
    const spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = RG_VS_LED_MOSI,
        .sclk_io_num = RG_VS_LED_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ret = spi_bus_initialize(RG_VS_LED_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "spi_bus_initialize failed.");

    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_20M,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 2,
    };
    ret = spi_bus_add_device(RG_VS_LED_SPI_HOST, &devcfg, &spi_handle);
    RG_ASSERT(ret == ESP_OK, "spi_bus_add_device failed.");
}

static void spi_acquire(void) {
    esp_err_t ret = spi_device_acquire_bus(spi_handle, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
}

static void vsspi_init(void) {
    buf_size = 4 + RG_VS_PIXELS * 4 * 2 + 8;
    spi_buf = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    extra_buf = heap_caps_malloc(buf_size / 2, MALLOC_CAP_DEFAULT);
    RG_ASSERT(spi_buf && extra_buf, "ventilastation SPI buffers alloc failed");
    memset(spi_buf, 0xff, buf_size);
    memset(extra_buf, 0x01, buf_size / 2);
    ((uint32_t *)spi_buf)[0] = 0;
    pixels0 = (uint32_t *)(spi_buf + 4);
    pixels1 = (uint32_t *)(spi_buf + RG_VS_PIXELS * 4);
    for (int n = 0; n < RG_VS_PIXELS; n++) {
        pixels0[n] = 0x010000ff;
        pixels1[n] = 0x000100ff;
    }
}

static void spi_write(const void *data_in, size_t len) {
    spi_transaction_t transaction = {
        .length = len * 8,
        .tx_buffer = data_in,
    };
    esp_err_t ret = spi_device_polling_transmit(spi_handle, &transaction);
    ESP_ERROR_CHECK(ret);
}

static void IRAM_ATTR hall_neg_sensed(void *arg) {
    int64_t this_turn = rg_system_timer();
    int64_t this_turn_duration = this_turn - last_turn;
    if (this_turn_duration > RG_VS_FASTEST_CREDIBLE_TURN_US) {
        last_turn_duration = this_turn_duration;
        last_turn = this_turn;
    }
}

static void hall_init(void) {
    gpio_set_direction(RG_VS_HALL_GPIO, GPIO_MODE_INPUT);
    gpio_set_intr_type(RG_VS_HALL_GPIO, GPIO_INTR_NEGEDGE);
    esp_err_t ret = gpio_install_isr_service(0);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "gpio_install_isr_service failed.");
    ret = gpio_isr_handler_add(RG_VS_HALL_GPIO, hall_neg_sensed, (void *)RG_VS_HALL_GPIO);
    RG_ASSERT(ret == ESP_OK, "gpio_isr_handler_add failed.");
}

static void gpu_step(void) {
    int64_t now = rg_system_timer();
    uint32_t column = ((now - last_turn) * RG_VS_COLUMNS / last_turn_duration) % RG_VS_COLUMNS;
    if (column != last_column) {
        project_angle((column + RG_VS_COLUMNS / 2) % RG_VS_COLUMNS, extra_buf);
        for (int n = 0; n < RG_VS_PIXELS; n++) {
            pixels0[n] = extra_buf[RG_VS_PIXELS - 1 - n];
        }
        project_angle(column, pixels1);
        spi_write(spi_buf, buf_size);
        last_column = column;
    }
}

static void IRAM_ATTR vs_display_task(void *arg) {
    hall_init();
    spi_start_buses();
    spi_acquire();
    while (true) {
        gpu_step();
    }
}

bool rg_vs_pov_enabled(void) {
    return true;
}

void rg_vs_pov_init(int width, int height) {
    screen_width = width;
    screen_height = height;

    vs_data = rg_alloc((size_t)screen_width * (size_t)screen_height, MEM_FAST);
    vs_palette = rg_alloc(256 * sizeof(uint32_t), MEM_FAST);
    vs_projection_table = rg_alloc(RG_VS_COLUMNS * RG_VS_PIXELS * sizeof(uint16_t), MEM_FAST);
    RG_ASSERT(vs_data && vs_palette && vs_projection_table, "ventilastation POV alloc failed");

    memset(vs_data, 0, (size_t)screen_width * (size_t)screen_height);
    memset(vs_palette, 0, 256 * sizeof(uint32_t));
    memset(vs_projection_table, 0, RG_VS_COLUMNS * RG_VS_PIXELS * sizeof(uint16_t));

    vs_setup_projection_table();
    vsspi_init();
    rg_task_create("vs_display", &vs_display_task, NULL, 2 * 1024, RG_TASK_PRIORITY_6, 1);
}

void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count) {
    if (!vs_palette || !palette) {
        return;
    }
    if (count > 256) {
        count = 256;
    }
    memcpy(vs_palette, palette, count * sizeof(uint32_t));
    vs_tcp_send_palette();
}

void rg_vs_pov_submit_frame(const uint8_t *framebuffer, size_t length) {
    if (!vs_data || !framebuffer) {
        return;
    }
    memcpy(vs_data, framebuffer, length);
    vs_tcp_send_frame();
}

#else

bool rg_vs_pov_enabled(void) {
    return false;
}

void rg_vs_pov_init(int screen_width, int screen_height) {
    (void)screen_width;
    (void)screen_height;
}

void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count) {
    (void)palette;
    (void)count;
}

void rg_vs_pov_submit_frame(const uint8_t *framebuffer, size_t length) {
    (void)framebuffer;
    (void)length;
}

void rg_vs_pov_set_tcp_bridge(rg_vs_tcp_send_fn send_fn, rg_vs_tcp_connected_fn connected_fn) {
    (void)send_fn;
    (void)connected_fn;
}

#endif
