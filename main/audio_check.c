#include "audio_check.h"

#include <math.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    SAMPLE_RATE = 16000,
    DMA_FRAMES = 128,
    DMA_BLOCKS = 4,
    IO_TIMEOUT_MS = 250,
    TONE_MS = 280,
    TONE_PEAK = 768, // Maximum amplitude in signed PCM16 units.
};

static const char *TAG = "audio_check";
// A stereo I2S frame carries one signed 32-bit word per slot.
static int32_t block[DMA_FRAMES][2];

static esp_err_t start_audio(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_frame_num = DMA_FRAMES;
    channel.dma_desc_num = DMA_BLOCKS;
    channel.auto_clear_after_cb = true;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_1,  // D0, shared BCLK/SCK
            .ws = GPIO_NUM_2,    // D1, shared LRC/WS
            .dout = GPIO_NUM_43, // D6, MAX98357A DIN
            .din = GPIO_NUM_44,  // D7, INMP441 SD
        },
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, tx, rx), TAG, "allocate I2S channels");
    // Set up TX first so it generates both shared clocks.
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*tx, &config), TAG, "init TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*rx, &config), TAG, "init RX");
    return i2s_channel_enable(*tx);
}

static void set_stereo_sample(size_t frame, int32_t pcm16)
{
    // Duplicate mono into both slots, regardless of the amplifier channel select.
    block[frame][0] = block[frame][1] = pcm16 * 65536;
}

static esp_err_t write_frames(i2s_chan_handle_t tx, size_t count)
{
    size_t written = 0;
    const size_t bytes = count * sizeof(block[0]);
    ESP_RETURN_ON_ERROR(i2s_channel_write(tx, block, bytes, &written, IO_TIMEOUT_MS), TAG, "write I2S");
    return written == bytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t play_tone(i2s_chan_handle_t tx, unsigned hz)
{
    const unsigned samples = SAMPLE_RATE * TONE_MS / 1000;
    const unsigned fade = SAMPLE_RATE / 100;
    const float phase_step = 6.28318530718f * hz / SAMPLE_RATE;
    for (unsigned offset = 0; offset < samples;) {
        unsigned count = samples - offset;
        if (count > DMA_FRAMES) count = DMA_FRAMES;
        for (unsigned i = 0; i < count; ++i) {
            unsigned n = offset + i;
            unsigned edge = n < samples - 1 - n ? n : samples - 1 - n;
            float envelope = edge < fade ? (float)edge / fade : 1.0f;
            set_stereo_sample(i, (int32_t)(TONE_PEAK * envelope * sinf(phase_step * n)));
        }
        ESP_RETURN_ON_ERROR(write_frames(tx, count), TAG, "play tone");
        offset += count;
    }
    // Blocking writes finish when data is queued, before the last DMA block plays.
    vTaskDelay(pdMS_TO_TICKS(DMA_BLOCKS * DMA_FRAMES * 1000 / SAMPLE_RATE + 20));
    return ESP_OK;
}

void audio_check_run(void)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    ESP_LOGI(TAG, "I2S0/1 Philips 16 kHz, 2 x 32-bit, BCLK=1 WS=2 DOUT=43 DIN=44");
    esp_err_t result = start_audio(&tx, &rx);
    if (result == ESP_OK) {
        // Clock and amplifier settle before the low-volume test tone.
        vTaskDelay(pdMS_TO_TICKS(350));
        ESP_LOGI(TAG, "Speaker test: 750 Hz, PCM peak %d/32768", TONE_PEAK);
        result = play_tone(tx, 750);
        esp_err_t stop = i2s_channel_disable(tx);
        if (result == ESP_OK) result = stop;
    }
    if (rx) i2s_del_channel(rx);
    if (tx) i2s_del_channel(tx);
    if (result == ESP_OK) ESP_LOGI(TAG, "Speaker test frames sent");
    else ESP_LOGW(TAG, "Audio check failed: %s", esp_err_to_name(result));
}
