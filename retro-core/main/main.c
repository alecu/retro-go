#include "shared.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_littlefs.h>
#include "drivers/display/ventilastation_pov.h"


void app_main(void)
{
    rg_app_t *app = rg_system_init(AUDIO_SAMPLE_RATE, NULL, NULL);

    // Ventilastation: mount the shared LittleFS data partition at /vfs (the
    // default rg_storage init mounts FAT, which this partition is not), so ROMs
    // under RG_STORAGE_ROOT can be opened. Mirrors gwenesis / prboom-go.
    {
        esp_vfs_littlefs_conf_t lfs_conf = {
            .base_path = "/vfs",
            .partition_label = "vfs",
            .format_if_mount_failed = false,
            .read_only = false,
        };
        esp_err_t lfs_err = esp_vfs_littlefs_register(&lfs_conf);
        if (lfs_err != ESP_OK)
            RG_LOGW("VFS LittleFS mount failed (%d)\n", lfs_err);
    }

    // Start the POV display task in hardware LED mode (drives the spinning strip
    // over SPI). Without this the task blocks and no LEDs light up.
    rg_vs_pov_set_tcp_bridge(NULL, NULL);

    // The retro-go launcher is disabled at the POV display's resolution, so we
    // are booted directly from MicroPython with no bootArgs. Read which system
    // to run (-> configNs) and the ROM path (-> romPath) from NVS namespace
    // "voom_emu" (written by native_apps.py just before launch).
    {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if (err == ESP_OK) {
            nvs_handle_t h;
            if (nvs_open("voom_emu", NVS_READONLY, &h) == ESP_OK) {
                static char vs_system[16];
                static char vs_rom[256];
                size_t len = sizeof(vs_system) - 1;
                if (nvs_get_blob(h, "system", vs_system, &len) == ESP_OK) {
                    vs_system[len] = '\0';
                    app->configNs = vs_system;
                }
                len = sizeof(vs_rom) - 1;
                if (nvs_get_blob(h, "rom", vs_rom, &len) == ESP_OK) {
                    vs_rom[len] = '\0';
                    app->romPath = vs_rom;
                }
                nvs_close(h);
            }
        }
    }

    RG_LOGI("configNs=%s rom=%s", app->configNs, app->romPath ? app->romPath : "");

    if (strcmp(app->configNs, "gbc") == 0 || strcmp(app->configNs, "gb") == 0)
        gbc_main();
    else if (strcmp(app->configNs, "nes") == 0)
        nes_main();
    else if (strcmp(app->configNs, "pce") == 0)
        pce_main();
    else if (strcmp(app->configNs, "sms") == 0)
        sms_main();
    else if (strcmp(app->configNs, "gg") == 0)
        sms_main();
    else if (strcmp(app->configNs, "col") == 0)
        sms_main();
    else if (strcmp(app->configNs, "gw") == 0)
        gw_main();
    else if (strcmp(app->configNs, "snes") == 0)
        snes_main();
#ifndef __TINYC__
    else if (strcmp(app->configNs, "lnx") == 0)
        lynx_main();
#endif
    else
        launcher_main();

    RG_PANIC("Never reached");
}
