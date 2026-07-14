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
#include <nvs.h>
#include "color_pipeline.h"
#include "intensidades.h"
#include "rg_system.h"
#include "vs_board_config.h"

#define RG_VS_COLUMNS 256
#define RG_VS_PIXELS 54
#define RG_VS_FASTEST_CREDIBLE_TURN_US 10000
#define RG_VS_TAU 6.28318530717958647692
#define RG_VS_APA102_BLACK 0x000000e0
#define RG_VS_EXIT_PRESENT_TIMEOUT_US 150000

// Base rotation offset (columns, 256 = one full turn): compensates for the
// quarter-turn difference between the board's angle-0 ray and the screen mapping.
// The runtime value vs_angle_offset adds the user-calibrated pov_column_offset
// (written to NVS by MicroPython before launching this app) on top of this.
#define RG_VS_ANGLE_OFFSET 64

static int vs_angle_offset = RG_VS_ANGLE_OFFSET;
static vs_board_config_t vs_board;

// Indexed (palette) framebuffer — 1 byte per pixel, updated by rg_vs_pov_submit_surface().
static uint8_t  *vs_data     = NULL;
// Direct-RGB framebuffer — 0x00RRGGBB per pixel, used when surface format is not indexed.
static uint32_t *vs_data_rgb = NULL;
// Current surface pixel format (rg_pixel_format_t). Determines which buffer project_angle() reads.
static int       vs_format   = 0;

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

// The exit transition runs on the game task while the LED task keeps scanning
// the last copied surface on core 1.  The atomic flag rejects any later
// surface submission, so the visible image remains frozen during the sweep.
static bool vs_exit_fade_active = false;
static uint8_t vs_exit_black_outer_leds = 0;
static uint32_t vs_exit_fade_generation = 0;
static uint32_t vs_exit_presented_generation = 0;

// True while framebuffers are being (re)allocated or the projection table is being rebuilt.
// gpu_step() skips rendering while this is set. Initially true so the display task waits
// for the first rg_vs_pov_submit_surface() call before touching vs_data.
static volatile bool rebuilding = true;

static volatile int64_t last_turn = 0;
static volatile int64_t last_turn_duration = 102400;

static void vs_load_color_profile(void) {
    uint8_t profile[COLOR_PIPELINE_PROFILE_BYTES];
    size_t length = sizeof(profile);
    nvs_handle_t nvs;
    if (nvs_open("voom_pov", NVS_READONLY, &nvs) != ESP_OK) {
        RG_LOGW("vs_pov: no POV colour profile; using legacy transfer tables\n");
        return;
    }
    esp_err_t result = nvs_get_blob(nvs, "color_v1", profile, &length);
    nvs_close(nvs);
    if (result != ESP_OK || !color_pipeline_apply(profile, length)) {
        RG_LOGW("vs_pov: invalid POV colour profile; using legacy transfer tables\n");
        return;
    }
    RG_LOGI("vs_pov: loaded calibrated POV colour profile\n");
}

static void vs_setup_projection_table(void) {
    int center_x = screen_width / 2;
    int center_y = screen_height / 2;
    int radius = RG_MIN(center_x, center_y) - 2;

    for (int angle = 0; angle < RG_VS_COLUMNS; angle++) {
        double a = (angle + vs_angle_offset) * RG_VS_TAU / RG_VS_COLUMNS;
        for (int led = 0; led < RG_VS_PIXELS; led++) {
            int x = 128 + (int)(radius * (led + 1) / RG_VS_PIXELS * cos(a));
            int y = 128 + (int)(radius * (led + 1) / RG_VS_PIXELS * sin(a));
            vs_projection_table[angle * RG_VS_PIXELS + led] = (x << 8) + y;
        }
    }
}

static void project_angle(int angle, uint32_t row[RG_VS_PIXELS]) {
    uint8_t black_outer_leds = __atomic_load_n(
        &vs_exit_black_outer_leds, __ATOMIC_ACQUIRE);
    for (int led = 0; led < RG_VS_PIXELS; led++) {
        // LED zero is the centre of the projected frame, so blackening the
        // highest indexes first visibly moves from the outside toward it.
        if (led >= RG_VS_PIXELS - black_outer_leds) {
            row[led] = RG_VS_APA102_BLACK;
            continue;
        }
        uint16_t pos = vs_projection_table[angle * RG_VS_PIXELS + led];
        int x = ((pos >> 8) & 0xff) - 128 + screen_width / 2;
        int y = (pos & 0xff) - 128 + screen_height / 2;

        uint32_t doom_color;
        if (vs_format & RG_PIXEL_PALETTE) {
            doom_color = vs_palette[vs_data[y * screen_width + x]];
        } else {
            doom_color = vs_data_rgb[y * screen_width + x];
        }

        if (color_pipeline_is_active()) {
            row[led] = color_pipeline_encode_rgb(
                led,
                (doom_color & 0xff0000) >> 16,
                (doom_color & 0x00ff00) >> 8,
                doom_color & 0x0000ff
            );
        } else {
            int level = intensidades_por_led[led];
            row[led] = (brillos[led] & 0x1f) | 0xe0 |
                intensidades[level][(doom_color & 0xff0000) >> 16] << 24 |
                intensidades[level][(doom_color & 0x00ff00) >> 8] << 16 |
                intensidades[level][doom_color & 0x0000ff] << 8;
        }
    }
}

static void spi_start_buses(void) {
    esp_err_t ret;
    const spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = vs_board.led_mosi,
        .sclk_io_num = vs_board.led_clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ret = spi_bus_initialize((spi_host_device_t)vs_board.led_spi_host, &buscfg, SPI_DMA_CH_AUTO);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "spi_bus_initialize failed.");

    // The configured CS frames each LED burst for the hardware workbench SPI
    // slave. APA102 strips ignore it, so -1 is also valid for a board without CS.
    const spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_20M,
        .mode = 0,
        .spics_io_num = vs_board.led_cs,
        .queue_size = 2,
    };
    ret = spi_bus_add_device((spi_host_device_t)vs_board.led_spi_host, &devcfg, &spi_handle);
    RG_ASSERT(ret == ESP_OK, "spi_bus_add_device failed.");
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
    gpio_set_direction(vs_board.hall_gpio, GPIO_MODE_INPUT);
    gpio_set_intr_type(vs_board.hall_gpio, GPIO_INTR_NEGEDGE);
    esp_err_t ret = gpio_install_isr_service(0);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "gpio_install_isr_service failed.");
    ret = gpio_isr_handler_add(vs_board.hall_gpio, hall_neg_sensed, (void *)vs_board.hall_gpio);
    RG_ASSERT(ret == ESP_OK, "gpio_isr_handler_add failed.");
}

static void gpu_step(void) {
    if (rebuilding)
        return;
    int64_t now = rg_system_timer();
    uint32_t column = ((now - last_turn) * RG_VS_COLUMNS / last_turn_duration) % RG_VS_COLUMNS;
    if (column != last_column) {
        uint32_t fade_generation = __atomic_load_n(
            &vs_exit_fade_generation, __ATOMIC_ACQUIRE);
        project_angle((column + RG_VS_COLUMNS / 2) % RG_VS_COLUMNS, extra_buf);
        for (int n = 0; n < RG_VS_PIXELS; n++) {
            pixels0[n] = extra_buf[RG_VS_PIXELS - 1 - n];
        }
        project_angle(column, pixels1);
        spi_write(spi_buf, buf_size);
        __atomic_store_n(&vs_exit_presented_generation,
                         fade_generation,
                         __ATOMIC_RELEASE);
        last_column = column;
    }
}

static void IRAM_ATTR vs_display_task(void *arg) {
    hall_init();
    // Acquire the bus once and keep driving the LED strip.
    spi_start_buses();
    ESP_ERROR_CHECK(spi_device_acquire_bus(spi_handle, portMAX_DELAY));
    while (true) {
        gpu_step();
    }
}

bool rg_vs_pov_enabled(void) {
    return true;
}

void rg_vs_pov_init(void) {
    if (!vs_board_config_load(&vs_board)) {
        RG_LOGE("vs_pov: board configuration missing; run make configure-board\n");
        return;
    }
    // POV calibration is independent of the board wiring and is adjusted from
    // the MicroPython settings scene.
    nvs_handle_t nvs;
    if (nvs_open("voom_pov", NVS_READONLY, &nvs) == ESP_OK) {
        int32_t offset;
        if (nvs_get_i32(nvs, "col_offset", &offset) == ESP_OK)
            vs_angle_offset = ((int)RG_VS_ANGLE_OFFSET + (int)offset + RG_VS_COLUMNS) % RG_VS_COLUMNS;
        nvs_close(nvs);
    }
    vs_load_color_profile();

    vs_palette          = rg_alloc(256 * sizeof(uint32_t), MEM_FAST);
    vs_projection_table = rg_alloc(RG_VS_COLUMNS * RG_VS_PIXELS * sizeof(uint16_t), MEM_FAST);
    RG_ASSERT(vs_palette && vs_projection_table, "ventilastation POV alloc failed");

    memset(vs_palette, 0, 256 * sizeof(uint32_t));
    memset(vs_projection_table, 0, RG_VS_COLUMNS * RG_VS_PIXELS * sizeof(uint16_t));

    // vs_data / vs_data_rgb allocated lazily in rg_vs_pov_submit_surface().
    // rebuilding stays true until then so gpu_step() doesn't touch NULL pointers.

    vsspi_init();
    rg_task_create("vs_display", &vs_display_task, NULL, 2 * 1024, RG_TASK_PRIORITY_6, 1);
}

void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count)
{
    if (!vs_palette || !palette)
        return;
    if (count > 256)
        count = 256;
    memcpy(vs_palette, palette, count * sizeof(uint32_t));
}

void rg_vs_pov_submit_surface(const rg_surface_t *surface)
{
    if (__atomic_load_n(&vs_exit_fade_active, __ATOMIC_ACQUIRE))
        return;
    if (!surface || !surface->data)
        return;
    // (Re)allocate framebuffers and rebuild projection table when dimensions change.
    if (screen_width != surface->width || screen_height != surface->height)
    {
        rebuilding = true;
        free(vs_data);     vs_data     = NULL;
        free(vs_data_rgb); vs_data_rgb = NULL;

        screen_width  = surface->width;
        screen_height = surface->height;
        size_t n = (size_t)screen_width * screen_height;

        vs_data     = rg_alloc(n,                    MEM_FAST);
        vs_data_rgb = rg_alloc(n * sizeof(uint32_t), MEM_FAST);
        RG_ASSERT(vs_data && vs_data_rgb, "ventilastation POV surface alloc failed");

        memset(vs_data,     0, n);
        memset(vs_data_rgb, 0, n * sizeof(uint32_t));
        vs_setup_projection_table();
        rebuilding = false;
    }

    vs_format = surface->format;

    if (surface->format & RG_PIXEL_PALETTE)
    {
        // Indexed surface: 1 byte per pixel.
        const uint8_t *src = (const uint8_t *)surface->data;
        int src_stride = surface->stride ? surface->stride : surface->width;
        if (src_stride == surface->width)
        {
            memcpy(vs_data, src, (size_t)surface->width * surface->height);
        }
        else
        {
            for (int y = 0; y < surface->height; y++)
                memcpy(vs_data + y * surface->width, src + y * src_stride, surface->width);
        }

        // Derive vs_palette from the surface's RGB565 palette (big-endian for PAL565_BE).
        if (surface->palette)
        {
            bool be = (surface->format & ~RG_PIXEL_PALETTE) == RG_PIXEL_565_BE;
            for (int i = 0; i < 256; i++)
            {
                uint16_t p = surface->palette[i];
                if (be) p = __builtin_bswap16(p);
                uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
                uint32_t g = ((p >> 5)  & 0x3F) * 255 / 63;
                uint32_t b = ( p        & 0x1F) * 255 / 31;
                vs_palette[i] = (r << 16) | (g << 8) | b;
            }
        }
    }
    else
    {
        // Direct RGB surface: convert to 0x00RRGGBB and store in vs_data_rgb.
        int w = surface->width, h = surface->height;

        if (surface->format == RG_PIXEL_565_LE || surface->format == RG_PIXEL_565_BE)
        {
            const uint16_t *src = (const uint16_t *)surface->data;
            int src_stride_px = surface->stride ? (surface->stride / 2) : w;
            bool be = (surface->format == RG_PIXEL_565_BE);
            for (int y = 0; y < h; y++)
            {
                const uint16_t *row = src + y * src_stride_px;
                uint32_t *dst = vs_data_rgb + y * w;
                for (int x = 0; x < w; x++)
                {
                    uint16_t p = row[x];
                    if (be) p = __builtin_bswap16(p);
                    uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
                    uint32_t g = ((p >> 5)  & 0x3F) * 255 / 63;
                    uint32_t b = ( p        & 0x1F) * 255 / 31;
                    dst[x] = (r << 16) | (g << 8) | b;
                }
            }
        }
        else if (surface->format == RG_PIXEL_888)
        {
            const uint8_t *src = (const uint8_t *)surface->data;
            int src_stride_b = surface->stride ? surface->stride : (w * 3);
            for (int y = 0; y < h; y++)
            {
                const uint8_t *row = src + y * src_stride_b;
                uint32_t *dst = vs_data_rgb + y * w;
                for (int x = 0; x < w; x++)
                    dst[x] = ((uint32_t)row[x*3] << 16) | ((uint32_t)row[x*3+1] << 8) | row[x*3+2];
            }
        }
    }
}

void rg_vs_pov_fade_last_frame_to_black(uint32_t duration_ms)
{
    if (rebuilding || (!vs_data && !vs_data_rgb))
        return;

    __atomic_store_n(&vs_exit_fade_active, true, __ATOMIC_RELEASE);
    __atomic_store_n(&vs_exit_black_outer_leds, 0, __ATOMIC_RELEASE);

    int64_t started = rg_system_timer();
    int64_t duration_us = (int64_t)duration_ms * 1000;
    while (duration_us > 0) {
        int64_t elapsed = rg_system_timer() - started;
        if (elapsed >= duration_us)
            break;
        uint32_t black_leds = (uint32_t)(elapsed * RG_VS_PIXELS / duration_us);
        __atomic_store_n(&vs_exit_black_outer_leds, black_leds, __ATOMIC_RELEASE);
        // Yield to the core-1 SPI task while keeping the game loop stopped.
        rg_task_delay(5);
    }

    __atomic_store_n(&vs_exit_black_outer_leds, RG_VS_PIXELS, __ATOMIC_RELEASE);
    uint32_t generation = __atomic_add_fetch(
        &vs_exit_fade_generation, 1, __ATOMIC_ACQ_REL);
    int64_t deadline = rg_system_timer() + RG_VS_EXIT_PRESENT_TIMEOUT_US;
    // Do not reset until the display task has sent the black centre LED at
    // least once.  A stopped rotor must not make the exit path hang forever.
    while (__atomic_load_n(&vs_exit_presented_generation, __ATOMIC_ACQUIRE) != generation
           && rg_system_timer() < deadline) {
        rg_task_delay(1);
    }
}

#else

bool rg_vs_pov_enabled(void) {
    return false;
}

void rg_vs_pov_init(void) {
}

void rg_vs_pov_set_palette32(const uint32_t *palette, size_t count) {
    (void)palette;
    (void)count;
}

void rg_vs_pov_submit_surface(const rg_surface_t *surface) {
    (void)surface;
}

void rg_vs_pov_fade_last_frame_to_black(uint32_t duration_ms) {
    (void)duration_ms;
}

#endif
