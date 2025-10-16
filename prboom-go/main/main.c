/*
 * This file is part of doom-ng-odroid-go.
 * Copyright (c) 2019 ducalex.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <sys/dirent.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <doomtype.h>
#include <doomstat.h>
#include <doomdef.h>
#include <d_main.h>
#include <g_game.h>
#include <i_system.h>
#include <i_video.h>
#include <i_sound.h>
#include <i_main.h>
#include <m_argv.h>
#include <m_fixed.h>
#include <m_misc.h>
#include <r_draw.h>
#include <r_fps.h>
#include <s_sound.h>
#include <st_stuff.h>
#include <mus2mid.h>
#include <midifile.h>
#include <oplplayer.h>
#include <rg_system.h>
#include <math.h>

#include <driver/gpio.h>

#include <hal/spi_types.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <intensidades.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include "main.h"
#endif

// For the Ventilastation POV display
#define GPIO_HALL_B     GPIO_NUM_5
#define LEDS_SPI_HOST   SPI2_HOST
#define PIN_NUM_MOSI    16
#define PIN_NUM_CLK     15

#define ESP_INTR_FLAG_DEFAULT 0
#define COLUMNS 256
#define PIXELS 54
#define FASTEST_CREDIBLE_TURN 10000 // if the fan is going over 100 FPS, then I don't believe it, and discard the reading

static uint8_t *vs_data = NULL;
static uint32_t *vs_palette = NULL;
static uint16_t *vs_projection_table = NULL;

static rg_task_t *vs_task_queue;
char* spi_buf;
uint32_t* extra_buf;
uint32_t* pixels0;
uint32_t* pixels1;
int buf_size;
extern uint8_t brillos[PIXELS];
extern uint8_t intensidades_por_led[PIXELS];

volatile int64_t last_turn = 0;
volatile int64_t last_turn_duration = 102400;

// end ventilastation stuff

#define AUDIO_SAMPLE_RATE 22050

#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / TICRATE + 1)
#define NUM_MIX_CHANNELS 8

static rg_surface_t *update;
static rg_app_t *app;

static const char *doom_argv[10];


// Expected variables by doom
int snd_card = 1, mus_card = 1;
int snd_samplerate = AUDIO_SAMPLE_RATE;
int current_palette = 0;

typedef struct {
    uint16_t unused1;
    uint16_t samplerate;
    uint16_t length;
    uint16_t unused2;
    byte samples[];
} doom_sfx_t;

typedef struct {
    const doom_sfx_t *sfx;
    size_t pos;
    float factor;
    int starttic;
} channel_t;

static channel_t channels[NUM_MIX_CHANNELS];
static const doom_sfx_t *sfx[NUMSFX];
static rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH];
static const music_player_t *music_player = &opl_synth_player;
static bool musicPlaying = false;

// TO DO: Detect when menu is open so we can send better keys.

static const struct {int mask; int *key;} keymap[] = {
    {RG_KEY_UP, &key_up},
    {RG_KEY_DOWN, &key_down},
    {RG_KEY_LEFT, &key_left},
    {RG_KEY_RIGHT, &key_right},
    {RG_KEY_A, &key_fire},
    {RG_KEY_A, &key_enter},
    {RG_KEY_B, &key_speed},
    {RG_KEY_B, &key_strafe},
    {RG_KEY_B, &key_backspace},
    {RG_KEY_MENU, &key_escape},
    {RG_KEY_OPTION, &key_map},
    {RG_KEY_START, &key_use},
    {RG_KEY_SELECT, &key_weapontoggle},
};

static const char *SETTING_GAMMA = "Gamma";


static rg_gui_event_t gamma_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int gamma = usegamma;
    int max = 9;

    if (event == RG_DIALOG_PREV)
        gamma = gamma > 0 ? gamma - 1 : max;

    if (event == RG_DIALOG_NEXT)
        gamma = gamma < max ? gamma + 1 : 0;

    if (gamma != usegamma)
    {
        usegamma = gamma;
        rg_settings_set_number(NS_APP, SETTING_GAMMA, gamma);
        I_SetPalette(current_palette);
        return RG_DIALOG_REDRAW;
    }

    sprintf(option->value, "%d/%d", gamma, max);

    return RG_DIALOG_VOID;
}


void I_StartFrame(void)
{
    //
}

void I_UpdateNoBlit(void)
{
    //
}

void I_FinishUpdate(void)
{
    // static int framecount = 0;
    // está por acá la cosa...
    // rg_display_submit(update, 0);
    // rg_display_sync(true); // Wait for update->buffer to be released
    memcpy(vs_data, update->data, SCREENWIDTH * SCREENHEIGHT);
    // framecount++;
    // if (framecount == 60)
    // {
    //     framecount = 0;
    //     //dump_vs_data(dumpbuf);
    //     dump_vs_projected();
    // }
}




void dump_vs_projected()
{ 
    char dumpbuf[73];
    RG_LOGI("Dumping Ventilastation Projected Data:\n");

    for (int angle = 0; angle < 256; angle++)
    {
        uint32_t row[54];
        project_angle(angle, row);

        for (int n = 0; n < 54; n++)
        {
            int v = (n % 9) * 8;
            sprintf(&dumpbuf[v], "%08lX ", row[n]);
            if (v == 64)
            {
                dumpbuf[72] = '\0';
                RG_LOGI("%s", dumpbuf);
            }
        }
    }

}

void project_angle(int angle, uint32_t row[54])
{
    for (int led = 0; led < 54; led++)
    {
        uint16_t pos = vs_projection_table[angle * 54 + led];
        int x = ((pos >> 8) & 0xFF) - 128 + SCREENWIDTH / 2;
        int y = (pos & 0xFF) - 128 + SCREENHEIGHT / 2;
        uint8_t px = vs_data[y * SCREENWIDTH + x];
        uint32_t doom_color = vs_palette[px];
        int alt_n = intensidades_por_led[led];
        uint32_t color = (brillos[led] & 0x1f) | 0xe0 |
                intensidades[alt_n][(doom_color & 0xff0000) >> 16] << 24 |
                intensidades[alt_n][(doom_color & 0x00ff00) >> 8] << 16 |
                intensidades[alt_n][(doom_color & 0x0000ff)] << 8;
        row[led] = color;
    }
}

void dump_vs_data(char dumpbuf[81])
{
    RG_LOGI("Dumping Ventilastation Screen Data...");
    for (int h = 0; h < SCREENHEIGHT; h++)
    {
        for (int w = 0; w < SCREENWIDTH; w++)
        {
            int v = (w % 40) * 2;
            sprintf(&dumpbuf[v], "%02X ", vs_data[h * SCREENWIDTH + w]);
            if (v == 78)
            {
                dumpbuf[80] = '\0';
                RG_LOGI("%s", dumpbuf);
            }
            // int color = ((uint16_t *)vs_data)[h * SCREENWIDTH + w];
            // vs_palette[color & 0xFF] = ((color & 0xF800) << 8) | ((color & 0x07E0) << 5) | ((color & 0x001F) << 3);
        }
    }
    RG_LOGI("Dumping Ventilastation Palette:\n");
    for (int i = 0; i < 256; i++)
    {
        int v = (i % 8) * 8;
        sprintf(&dumpbuf[v], "%08lX ", vs_palette[i]);
        if (v == 56)
        {
            dumpbuf[64] = '\0';
            RG_LOGI("%s", dumpbuf);
        }
    }
}

bool I_StartDisplay(void)
{
    return true;
}

void I_EndDisplay(void)
{
    //
}

void I_SetPalette(int pal)
{
    uint16_t *palette = V_BuildPalette(pal, 16);
    for (int i = 0; i < 256; i++)
        update->palette[i] = palette[i] << 8 | palette[i] >> 8;
    Z_Free(palette);
    current_palette = pal;

    uint32_t *pal32 = V_BuildPalette(pal, 32);
    memcpy(vs_palette, pal32, 256 * 4);
    Z_Free(pal32);
}

void I_InitGraphics(void)
{
    // set first three to standard values
    for (int i = 0; i < 3; i++)
    {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENWIDTH;
    }

    // Main screen uses internal ram for speed
    screens[0].data = update->data;
    screens[0].not_on_heap = true;

    // statusbar
    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENWIDTH;
}

int I_GetTimeMS(void)
{
    return rg_system_timer() / 1000;
}

int I_GetTime(void)
{
    return I_GetTimeMS() * TICRATE * realtic_clock_rate / 100000;
}

void I_uSleep(unsigned long usecs)
{
    rg_usleep(usecs);
}

void I_SafeExit(int rc)
{
    rg_system_exit();
}

const char *I_DoomExeDir(void)
{
    return RG_BASE_PATH_ROMS "/doom";
}

void I_UpdateSoundParams(int handle, int volume, int seperation, int pitch)
{
}

int I_StartSound(int sfxid, int channel, int vol, int sep, int pitch, int priority)
{

    // TODO: esto hay que reemplazarlo por algo que envie los ids de sonidos a la base
    int oldest = gametic;
    int slot = 0;

    // Unknown sound
    if (!sfx[sfxid])
        return -1;

    // These sound are played only once at a time. Stop any running ones.
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful
        || sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        for (int i = 0; i < NUM_MIX_CHANNELS; i++)
        {
            if (channels[i].sfx == sfx[sfxid])
                channels[i].sfx = NULL;
        }
    }

    // Find available channel or steal the oldest
    for (int i = 0; i < NUM_MIX_CHANNELS; i++)
    {
        if (channels[i].sfx == NULL)
        {
            slot = i;
            break;
        }
        else if (channels[i].starttic < oldest)
        {
            slot = i;
            oldest = channels[i].starttic;
        }
    }

    channel_t *chan = &channels[slot];
    chan->sfx = sfx[sfxid];
    chan->factor = (float)chan->sfx->samplerate / snd_samplerate;
    chan->pos = 0;

    return slot;
}

void I_StopSound(int handle)
{
    // TODO: hay que permitir apagar un sonido
    if (handle < NUM_MIX_CHANNELS)
        channels[handle].sfx = NULL;
}

bool I_SoundIsPlaying(int handle)
{
    // return (handle < NUM_MIX_CHANNELS && channels[handle].sfx);
    return false;
}

bool I_AnySoundStillPlaying(void)
{
    for (int i = 0; i < NUM_MIX_CHANNELS; i++)
        if (channels[i].sfx)
            return true;
    return false;
}

static void soundTask(void *arg)
{
    while (1)
    {
        bool haveMusic = snd_MusicVolume > 0 && musicPlaying;
        bool haveSFX = snd_SfxVolume > 0 && I_AnySoundStillPlaying();

        if (haveMusic)
        {
            music_player->render(mixbuffer, AUDIO_BUFFER_LENGTH);
        }

        if (haveSFX)
        {
            int16_t *audioBuffer = (int16_t *)mixbuffer;
            int16_t *audioBufferEnd = audioBuffer + AUDIO_BUFFER_LENGTH * 2;
            while (audioBuffer < audioBufferEnd)
            {
                int totalSample = 0;
                int totalSources = 0;
                int sample;

                for (int i = 0; i < NUM_MIX_CHANNELS; i++)
                {
                    channel_t *chan = &channels[i];
                    if (!chan->sfx)
                        continue;

                    size_t pos = (size_t)(chan->pos++ * chan->factor);

                    if (pos >= chan->sfx->length)
                    {
                        chan->sfx = NULL;
                    }
                    else if ((sample = chan->sfx->samples[pos]))
                    {
                        totalSample += sample - 127;
                        totalSources++;
                    }
                }

                totalSample <<= 7;
                totalSample /= (16 - snd_SfxVolume);

                if (haveMusic)
                {
                    totalSample += *audioBuffer;
                    totalSources += (totalSources == 0);
                }

                if (totalSources > 0)
                    totalSample /= totalSources;

                if (totalSample > 32767)
                    totalSample = 32767;
                else if (totalSample < -32768)
                    totalSample = -32768;

                *audioBuffer++ = totalSample;
                *audioBuffer++ = totalSample;
            }
        }

        if (!haveMusic && !haveSFX)
        {
            memset(mixbuffer, 0, sizeof(mixbuffer));
        }

        rg_audio_submit(mixbuffer, AUDIO_BUFFER_LENGTH);
    }
}

void I_InitSound(void)
{
    for (int i = 1; i < NUMSFX; i++)
    {
        if (S_sfx[i].lumpnum != -1)
            sfx[i] = W_CacheLumpNum(S_sfx[i].lumpnum);
    }

    music_player->init(snd_samplerate);
    music_player->setvolume(snd_MusicVolume);

    rg_task_create("doom_sound", &soundTask, NULL, 2048, RG_TASK_PRIORITY_2, 1);
}

void I_ShutdownSound(void)
{
    music_player->shutdown();
}

void I_PlaySong(int handle, int looping)
{
    music_player->play((void *)handle, looping);
    musicPlaying = true;
}

void I_PauseSong(int handle)
{
    music_player->pause();
    musicPlaying = false;
}

void I_ResumeSong(int handle)
{
    music_player->resume();
    musicPlaying = true;
}

void I_StopSong(int handle)
{
    music_player->stop();
    musicPlaying = false;
}

void I_UnRegisterSong(int handle)
{
    music_player->unregistersong((void *)handle);
}

int I_RegisterSong(const void *data, size_t len)
{
    uint8_t *mid = NULL;
    size_t midlen;
    int handle = 0;

    if (mus2mid(data, len, &mid, &midlen, 64) == 0)
        handle = (int)music_player->registersong(mid, midlen);
    else
        handle = (int)music_player->registersong(data, len);

    free(mid);

    return handle;
}

void I_SetMusicVolume(int volume)
{
    music_player->setvolume(volume);
}

void I_StartTic(void)
{
    static int64_t last_time = 0;
    static int32_t prev_joystick = 0x0000;
    static int32_t rg_menu_delay = 0;
    uint32_t joystick = rg_input_read_gamepad();
    uint32_t changed = prev_joystick ^ joystick;
    event_t event = {0};

    // Long press on menu will open retro-go's menu if needed, instead of DOOM's.
    // This is still needed to quit (DOOM 2) and for the debug menu. We'll unify that mess soon...
    if (joystick & (RG_KEY_MENU|RG_KEY_OPTION))
    {
        if (joystick & RG_KEY_OPTION)
        {
            Z_FreeTags(PU_CACHE, PU_CACHE); // At this point the heap is usually full. Let's reclaim some!
            rg_gui_options_menu();
            changed = 0;
        }
        else if (rg_menu_delay++ == TICRATE / 2)
        {
            Z_FreeTags(PU_CACHE, PU_CACHE); // At this point the heap is usually full. Let's reclaim some!
            rg_gui_game_menu();
        }
        realtic_clock_rate = app->speed * 100;
        R_InitInterpolation();
    }
    else
    {
        rg_menu_delay = 0;
    }

    if (changed)
    {
        for (int i = 0; i < RG_COUNT(keymap); i++)
        {
            if (changed & keymap[i].mask)
            {
                event.type = (joystick & keymap[i].mask) ? ev_keydown : ev_keyup;
                event.data1 = *keymap[i].key;
                D_PostEvent(&event);
            }
        }
    }

    rg_system_tick(rg_system_timer() - last_time);
    last_time = rg_system_timer();
    prev_joystick = joystick;
}

void I_Init(void)
{
    snd_channels = NUM_MIX_CHANNELS;
    snd_samplerate = AUDIO_SAMPLE_RATE;
    snd_MusicVolume = 15;
    snd_SfxVolume = 15;
    usegamma = rg_settings_get_number(NS_APP, SETTING_GAMMA, 0);
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    Z_FreeTags(PU_CACHE, PU_CACHE); // At this point the heap is usually full. Let's reclaim some!
	return rg_surface_save_image_file(update, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    rg_gui_alert("Not implemented", "Please use the in-game menu");
    return false;
}

static bool load_state_handler(const char *filename)
{
    rg_gui_alert("Not implemented", "Please use the in-game menu");
    return false;
}

static bool reset_handler(bool hard)
{
    return false;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_SHUTDOWN)
    {
        // DOOM fully fills the internal heap and this causes some shutdown
        // steps to fail so we try to free everything!
        Z_FreeTags(0, PU_MAX);
        rg_audio_set_mute(true);
    }
    else if (event == RG_EVENT_REDRAW)
    {
        // rg_display_submit(update, 0);
    }
}

bool is_iwad(const char *path)
{
    char header[16] = {0};
    void *data = &header;
    size_t data_len = 16;
    if (rg_extension_match(path, "zip"))
        rg_storage_unzip_file(path, NULL, &data, &data_len, RG_FILE_USER_BUFFER);
    else
        rg_storage_read_file(path, &data, &data_len, RG_FILE_USER_BUFFER);
    return header[0] == 'I' && header[1] == 'W';
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Gamma Boost"), "-", RG_DIALOG_FLAG_NORMAL, &gamma_update_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

void app_main()
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);
    rg_system_set_tick_rate(TICRATE);

    SCREENWIDTH = RG_MIN(rg_display_get_width(), MAX_SCREENWIDTH);
    SCREENHEIGHT = RG_MIN(rg_display_get_height(), MAX_SCREENHEIGHT);

    update = rg_surface_create(SCREENWIDTH, SCREENHEIGHT, RG_PIXEL_PAL565_BE, MEM_SLOW);

    ventilastation_init();

    const char *iwad = NULL;
    const char *pwad = NULL;

    if (is_iwad(app->romPath))
        iwad = app->romPath;
    else
        pwad = app->romPath;

    if (!iwad)
    {
        iwad = rg_gui_file_picker("Select IWAD file", I_DoomExeDir(), is_iwad, false) ?: "";
        rg_gui_draw_hourglass(); // Redraw hourglass to indicate loading...
    }

    myargv = doom_argv;
    myargc = pwad ? 7 : 5;
    doom_argv[0] = "doom";
    doom_argv[1] = "-save";
    doom_argv[2] = RG_BASE_PATH_SAVES "/doom";
    doom_argv[3] = "-iwad";
    doom_argv[4] = iwad;
    doom_argv[5] = "-file";
    doom_argv[6] = pwad;
    doom_argv[myargc] = 0;

#ifdef ESP_PLATFORM
    // Some things might be nice to place in internal RAM, but I do not have time to find such
    // structures. So for now, prefer external RAM for most things except the framebuffer which
    // is allocated above.
    heap_caps_malloc_extmem_enable(0);
#endif

    Z_Init();
    D_DoomMain();
}


// Ventilastation POV display support
static spi_device_handle_t spi_handle;


void spiStartBuses(uint32_t freq) {
    //printf("Initializing bus SPI%d...\n", LEDS_SPI_HOST+1);

    esp_err_t ret;
    RG_LOGI("init spi bus");
    const spi_bus_config_t buscfg={
            .miso_io_num = -1,
            .mosi_io_num = PIN_NUM_MOSI,
            .sclk_io_num = PIN_NUM_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 32,
    };
    ret = spi_bus_initialize(LEDS_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    RG_ASSERT(ret == ESP_OK || ret == ESP_ERR_INVALID_STATE, "spi_bus_initialize failed.");
        //const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
        //vTaskDelay( xDelay );

    RG_LOGI("adding spi device");
    const spi_device_interface_config_t devcfg = {
            .clock_speed_hz = SPI_MASTER_FREQ_20M,     //Clock out at 20 MHz
            .mode = 0,                              //SPI mode 0
            .spics_io_num = -1,             //CS pin
            .queue_size = 2,
    };
    ret = spi_bus_add_device(LEDS_SPI_HOST, &devcfg, &spi_handle);
    RG_ASSERT(ret == ESP_OK, "spi_bus_add_device failed.");
    //printf("adding device returned... %d\n", ret);
    RG_LOGI("spi bus ready");
}

void spiAcquire() {
    esp_err_t ret;
    ret = spi_device_acquire_bus(spi_handle, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
}

char* init_buffers(int num_pixels) {
    RG_LOGI("alloc spi buffer");
    spi_buf=heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    memset(spi_buf, 0xff, buf_size);
    RG_LOGI("alloc extra buffer");
    extra_buf=heap_caps_malloc(buf_size/2, MALLOC_CAP_DEFAULT);
    RG_LOGI("cleaning up buffers");
    memset(extra_buf, 0x01, buf_size/2);
    ((uint32_t*)spi_buf)[0]=0;
    pixels0 = (uint32_t*)(spi_buf+4);
    pixels1 = (uint32_t*)(spi_buf+num_pixels*4);
    for(int n=0; n<num_pixels; n++) {
        pixels0[n] = 0x010000Ff;
        pixels1[n] = 0x000100Ff;
    }
    RG_LOGI("buffers initialized");
    return spi_buf;
}

void vsspi_init(int num_pixels) {
    buf_size = 4 + num_pixels * 4 * 2 + 8;
    RG_LOGI("Initializing buffers");
    init_buffers(num_pixels);
}

void spiWriteNL(int device, const void * data_in, size_t len){
    esp_err_t ret;
    spi_transaction_t transaction = {
        .length=len*8,
            .tx_buffer=data_in,
            //.flags=SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL
    };
    ret = spi_device_polling_transmit(spi_handle, &transaction);
    ESP_ERROR_CHECK(ret);
}

volatile int turns = 0;

static void IRAM_ATTR hall_neg_sensed(void* arg)
{
    int64_t this_turn = rg_system_timer();
    int64_t this_turn_duration = this_turn - last_turn;
    if (this_turn_duration > FASTEST_CREDIBLE_TURN) {
        last_turn_duration = this_turn_duration;
        last_turn = this_turn;
    }
    turns++;
}

void hall_init(int gpio_hall) {
    gpio_set_direction(gpio_hall, GPIO_MODE_INPUT);
    gpio_set_intr_type(gpio_hall, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(gpio_hall, hall_neg_sensed, (void*) gpio_hall);
}

int last_column = 0;
void gpu_step() {
    int64_t now = rg_system_timer();
    uint32_t column = ((now - last_turn) * COLUMNS / last_turn_duration) % COLUMNS;
    if (column != last_column) {
        project_angle((column + COLUMNS/2) % COLUMNS, extra_buf);
        for(int n=0; n<54; n++) {
            pixels0[n] = extra_buf[53-n];
        }
        project_angle(column, pixels1);
        spiWriteNL(2, spi_buf, buf_size);
        last_column = column;
        // if (column == 0) {
        //     RG_LOGI("turns: %d (level = %d)", turns, gpio_get_level(GPIO_HALL_B));
        // }
    }
}


IRAM_ATTR
static void vs_display_task(void *arg)
{
    RG_LOGI("Hall Sensor init");
    hall_init(GPIO_HALL_B);

    const long freq = 20000000;
    RG_LOGI("Starting Buses");
    spiStartBuses(freq);
    RG_LOGI("Buses Started");

    spiAcquire();
    while (true) {
        gpu_step();
    }
    vTaskDelete(NULL);
}

void ventilastation_init()
{
    // Initialize ventilastation
    RG_LOGI("Ventilastation allocs");

    vs_data = rg_alloc(SCREENWIDTH * SCREENHEIGHT, MEM_FAST);
    vs_palette = rg_alloc(256 * sizeof(uint32_t), MEM_FAST);
    vs_projection_table = rg_alloc(256 * 54 * sizeof(uint16_t), MEM_FAST);

    memset(vs_data, 0, SCREENWIDTH * SCREENHEIGHT);
    memset(vs_palette, 0, 256 * sizeof(uint32_t));
    memset(vs_projection_table, 0, 256 * 54 * sizeof(uint16_t));

    vs_setup_projection_table();

    RG_LOGI("Ventilastation init");
    vsspi_init(PIXELS);
    
    RG_LOGI("Ventilastation start task");
    vs_task_queue = rg_task_create("vs_display", &vs_display_task, NULL, 2 * 1024, RG_TASK_PRIORITY_6, 1);
}

void dump_vs_projection_table()
{
    char dumpbuf[65];
    RG_LOGI("Dumping Ventilastation Projection Map:\n");
    for (int n = 0; n < 54; n++)
    {
        for (int m = 0; m < 256; m++)
        {
            int v = (m % 16) * 4;
            sprintf(&dumpbuf[v], "%04X ", vs_projection_table[n * 256 + m]);
            if (v == 60)
            {
                dumpbuf[64] = '\0';
                RG_LOGI("%s", dumpbuf);
            }
        }
        RG_LOGI("\n");
    }
}

void vs_setup_projection_table()
{
    int n_led = 54;
    int n_ang = 256;
    int center_x = SCREENWIDTH / 2;
    int center_y = SCREENHEIGHT / 2;
    int radius = RG_MIN(center_x, center_y) - 2;

    for (int m = 0; m < n_ang; m++)
    {
        for (int n = 0; n < n_led; n++)
        {
            int x = 128 + (int)(radius * (n+1) / n_led * cos(m * 2* M_PI/n_ang));
            int y = 128 + (int)(radius * (n+1) / n_led * sin(m * 2* M_PI/n_ang));
            vs_projection_table[m * n_led + n] = (x << 8) + y;
        }
    }
    // dump_vs_projection_table();
}