// Target definition
#define RG_TARGET_NAME             "VENTILASTATION"

// Return to MicroPython (factory partition) from prboom-go menu
#define RG_APP_LAUNCHER            "factory"
#define RG_APP_FACTORY             "factory"

// Storage: no SD card — use the LittleFS VFS partition already mounted at /vfs by prboom-go
#define RG_STORAGE_ROOT             "/vfs"
// #define RG_STORAGE_SDSPI_HOST       SPI2_HOST
// #define RG_STORAGE_SDSPI_SPEED      SDMMC_FREQ_DEFAULT
// #define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
// #define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT
// #define RG_STORAGE_FLASH_PARTITION  "vfs"

// Audio
#define RG_AUDIO_USE_INT_DAC        0   // 0 = Disable, 1 = GPIO25, 2 = GPIO26, 3 = Both
#define RG_AUDIO_USE_EXT_DAC        0   // 0 = Disable, 1 = Enable

// Video
// No LCD: the POV LED strip is the display and owns SPI2 (RG_VS_LED_* share the
// same bus/pins as the LCD would). Use the dummy driver so the retro-go display
// task never touches SPI — otherwise, in hardware POV mode, vs_display_task holds
// the bus and the LCD task panics in spi_take_buffer.
#define RG_SCREEN_DRIVER            2   // 0 = ILI9341/ST7789, 99 = SDL2, other = dummy (no LCD)
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_40M // SPI_MASTER_FREQ_80M
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_SCREEN_INIT()                                                                                         \
    ILI9341_CMD(0xCF, 0x00, 0xc3, 0x30);                                                                         \
    ILI9341_CMD(0xED, 0x64, 0x03, 0x12, 0x81);                                                                   \
    ILI9341_CMD(0xE8, 0x85, 0x00, 0x78);                                                                         \
    ILI9341_CMD(0xCB, 0x39, 0x2c, 0x00, 0x34, 0x02);                                                             \
    ILI9341_CMD(0xF7, 0x20);                                                                                     \
    ILI9341_CMD(0xEA, 0x00, 0x00);                                                                               \
    ILI9341_CMD(0xC0, 0x1B);                 /* Power control   //VRH[5:0] */                                    \
    ILI9341_CMD(0xC1, 0x12);                 /* Power control   //SAP[2:0];BT[3:0] */                            \
    ILI9341_CMD(0xC5, 0x32, 0x3C);           /* VCM control */                                                   \
    ILI9341_CMD(0xC7, 0x91);                 /* VCM control2 */                                                  \
    ILI9341_CMD(0x36, 0x68);                 /* Memory Access Control  (MX|MV|BGR) */                            \
    ILI9341_CMD(0xB1, 0x00, 0x10);           /* Frame Rate Control (1B=70, 1F=61, 10=119) */                     \
    ILI9341_CMD(0xB6, 0x0A, 0xA2);           /* Display Function Control */                                      \
    ILI9341_CMD(0xF6, 0x01, 0x30);                                                                               \
    ILI9341_CMD(0xF2, 0x00);                 /* 3Gamma Function Disable */                                       \
    ILI9341_CMD(0x26, 0x01);                 /* Gamma curve selected */                                          \
    ILI9341_CMD(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00); \
    ILI9341_CMD(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);


// Input: no physical buttons — all input comes from the host bridge
// (UART on hardware, TCP in emulator mode).
// Refer to rg_input.h to see all available RG_KEY_* and RG_GAMEPAD_*_MAP types

// # ADC_1_5 = GPIO6 (joystick Y — not populated)
// # ADC_1_6 = GPIO7 (joystick X — not populated)
// Floating ADC pins produce garbage readings, so the ADC map is disabled.

// Host byte bits match MicroPython's Director constants:
// left, right, up, down, A, B, C, D = 1,2,4,8,16,32,64,128.
// Map C/D to Select/Start so the standard Retro-Go console cores get the full
// 8-way + 4-button layout they expect, then recover Menu/Option with combos.
// Bit 7 (D/Start) is not a live wire bit under input protocol v2 -- it's
// mirrored by rg_input.c from "extra" bit 0 of the joystick frame (see
// docs/input-protocol-v2.md) so this map is unchanged.
#define RG_GAMEPAD_HOST_MAP {\
    {RG_KEY_LEFT,   .mask = (1 << 0)},\
    {RG_KEY_RIGHT,  .mask = (1 << 1)},\
    {RG_KEY_UP,     .mask = (1 << 2)},\
    {RG_KEY_DOWN,   .mask = (1 << 3)},\
    {RG_KEY_A,      .mask = (1 << 4)},\
    {RG_KEY_B,      .mask = (1 << 5)},\
    {RG_KEY_SELECT, .mask = (1 << 6)},\
    {RG_KEY_START,  .mask = (1 << 7)},\
}
#define RG_GAMEPAD_VIRT_MAP {\
    {RG_KEY_MENU,   .src = RG_KEY_START | RG_KEY_SELECT},\
    {RG_KEY_OPTION, .src = RG_KEY_SELECT | RG_KEY_A    },\
}


// Battery
// # ADC_1_3 = GPIO4
#define RG_BATTERY_DRIVER           1
#define RG_BATTERY_ADC_UNIT         ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL      ADC_CHANNEL_3
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)


// Status LED
#define RG_GPIO_LED                 GPIO_NUM_48

// Ventilastation POV display
#define RG_VS_ENABLE_POV_DISPLAY    1
#define RG_VS_HALL_GPIO             GPIO_NUM_7
#define RG_VS_LED_SPI_HOST          SPI2_HOST
#define RG_VS_LED_MOSI              GPIO_NUM_13
#define RG_VS_LED_CLK               GPIO_NUM_12
// CS driven low for each 444-byte burst so the hardware workbench SPI slave
// can frame transactions. APA102 strips ignore CS; GPIO17 is free in this config.
#define RG_VS_LED_CS                GPIO_NUM_14

// POV output mode: 0 = drive the spinning LED strip over SPI (real hardware),
// 1 = stream frames to the desktop pyglet emulator over TCP/WiFi (development).
// Override at build time without editing this file: -DRG_VS_ENABLE_TCP_BRIDGE=1
#ifndef RG_VS_ENABLE_TCP_BRIDGE
#define RG_VS_ENABLE_TCP_BRIDGE     0
#endif

// Hardware-mode host link (RG_VS_ENABLE_TCP_BRIDGE=0): the spinning board talks
// to the base-station host over UART2 — sound/music triggers out, input bytes in
// — matching the MicroPython serialcomms.py pins/baud (tx=GPIO10, rx=GPIO9,
// 115200 8N1, the machine.UART default). Unused in TCP/emulator mode.
#define RG_VS_SERIAL_UART_NUM       2
#define RG_VS_SERIAL_TX             GPIO_NUM_5
#define RG_VS_SERIAL_RX             GPIO_NUM_6
#define RG_VS_SERIAL_BAUD           115200

// SPI Display (back up working)
#define RG_GPIO_LCD_MISO            GPIO_NUM_17
#define RG_GPIO_LCD_MOSI            GPIO_NUM_18
#define RG_GPIO_LCD_CLK             GPIO_NUM_16
#define RG_GPIO_LCD_CS              GPIO_NUM_8
#define RG_GPIO_LCD_DC              GPIO_NUM_15
#define RG_GPIO_LCD_BCKL            GPIO_NUM_NC
#define RG_GPIO_LCD_RST             GPIO_NUM_NC
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_13
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_11
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_12
#define RG_GPIO_SDSPI_CS            GPIO_NUM_14


// #define RG_GPIO_SDSPI_CLK           GPIO_NUM_12
// #define RG_GPIO_SDSPI_CMD           GPIO_NUM_11
// #define RG_GPIO_SDSPI_D0            GPIO_NUM_13
// #define RG_GPIO_SDSPI_CS            GPIO_NUM_14


// // External I2S DAC
// #define RG_GPIO_SND_I2S_BCK         41
// #define RG_GPIO_SND_I2S_WS          42
// #define RG_GPIO_SND_I2S_DATA        40
// // #define RG_GPIO_SND_AMP_ENABLE      18
