#include "audio_check.h"

#include <math.h>
#include <stdlib.h>
#include "driver/gpio.h"
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
    RECORD_SECONDS = 3,
    RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS,
    REPLAY_PEAK = 2048,
    GAIN_SCALE = 256,
    MIC_DATA_PIN = GPIO_NUM_44,
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
            .din = MIC_DATA_PIN, // D7, INMP441 SD
        },
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, tx, rx), TAG, "allocate I2S channels");
    // Set up TX first so it generates both shared clocks.
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*tx, &config), TAG, "init TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*rx, &config), TAG, "init RX");
    // The microphone releases SD outside its own 24-bit word.
    ESP_RETURN_ON_ERROR(gpio_pulldown_en(MIC_DATA_PIN), TAG, "bias microphone data input");
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

static void wait_for_tx_tail(void)
{
    // Blocking writes finish when data is queued, before the last DMA block plays.
    vTaskDelay(pdMS_TO_TICKS(DMA_BLOCKS * DMA_FRAMES * 1000 / SAMPLE_RATE + 20));
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
    wait_for_tx_tail();
    return ESP_OK;
}

static esp_err_t capture_left(i2s_chan_handle_t rx, int16_t *pcm, size_t samples)
{
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "enable microphone RX");
    esp_err_t result = ESP_OK;
    for (size_t offset = 0; offset < samples;) {
        size_t count = samples - offset;
        if (count > DMA_FRAMES) count = DMA_FRAMES;
        const size_t bytes = count * sizeof(block[0]);
        size_t received = 0;
        result = i2s_channel_read(rx, block, bytes, &received, IO_TIMEOUT_MS);
        if (result != ESP_OK) break;
        if (received != bytes) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        for (size_t i = 0; i < count; ++i) {
            // L/R is grounded. The left slot contains signed 24-bit I2S
            // microphone data, aligned at bit 31. Keep the top 16 bits.
            pcm[offset + i] = (int16_t)(block[i][0] >> 16);
        }
        offset += count;
    }
    esp_err_t stop = i2s_channel_disable(rx);
    return result == ESP_OK ? stop : result;
}

static esp_err_t replay_capture(i2s_chan_handle_t tx, const int16_t *pcm, size_t samples)
{
    int64_t sum = 0;
    for (size_t i = 0; i < samples; ++i) sum += pcm[i];
    const int32_t dc = (int32_t)(sum / (int64_t)samples);
    int32_t peak = 0;
    int64_t energy = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t sample = pcm[i] - dc;
        int32_t magnitude = abs(sample);
        if (magnitude > peak) peak = magnitude;
        energy += (int64_t)sample * sample;
    }
    ESP_LOGI(TAG, "Mic captured %u samples, DC=%ld AC peak=%ld RMS=%.1f (PCM16)",
             (unsigned)samples, (long)dc, (long)peak, sqrt((double)energy / samples));
    if (peak == 0) {
        ESP_LOGW(TAG, "No variation in left slot; check microphone power, SD and L/R");
        return ESP_OK;
    }

    // Bound playback even for a loud capture, and limit gain for a quiet room.
    int32_t gain = REPLAY_PEAK * GAIN_SCALE / peak;
    if (gain > 16 * GAIN_SCALE) gain = 16 * GAIN_SCALE;
    ESP_LOGI(TAG, "Replaying recording with peak capped at %d/32768", REPLAY_PEAK);
    for (size_t offset = 0; offset < samples;) {
        size_t count = samples - offset;
        if (count > DMA_FRAMES) count = DMA_FRAMES;
        for (size_t i = 0; i < count; ++i) {
            set_stereo_sample(i, (pcm[offset + i] - dc) * gain / GAIN_SCALE);
        }
        ESP_RETURN_ON_ERROR(write_frames(tx, count), TAG, "play captured audio");
        offset += count;
    }
    wait_for_tx_tail();
    return ESP_OK;
}

static esp_err_t check_microphone(i2s_chan_handle_t tx, i2s_chan_handle_t rx)
{
    int16_t *pcm = malloc(RECORD_SAMPLES * sizeof(*pcm));
    if (!pcm) return ESP_ERR_NO_MEM;
    // Start capturing after the cue and its echo have died down. TX keeps
    // the clocks running and sends silence while the microphone is active.
    vTaskDelay(pdMS_TO_TICKS(350));
    ESP_LOGI(TAG, "Recording the next %d seconds (left slot)", RECORD_SECONDS);
    esp_err_t result = capture_left(rx, pcm, RECORD_SAMPLES);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        result = replay_capture(tx, pcm, RECORD_SAMPLES);
    }
    free(pcm);
    return result;
}

void audio_check_run(void)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    ESP_LOGI(TAG, "I2S Philips 16 kHz, 2 x 32-bit, BCLK=1 WS=2 DOUT=43 DIN=44");
    esp_err_t result = start_audio(&tx, &rx);
    if (result == ESP_OK) {
        // Clock and amplifier settle before the low-volume test tone.
        vTaskDelay(pdMS_TO_TICKS(350));
        ESP_LOGI(TAG, "Speaker test: 750 Hz, PCM peak %d/32768", TONE_PEAK);
        result = play_tone(tx, 750);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Speaker test frames sent");
            result = check_microphone(tx, rx);
        }
        esp_err_t stop = i2s_channel_disable(tx);
        if (result == ESP_OK) result = stop;
    }
    if (rx) i2s_del_channel(rx);
    if (tx) i2s_del_channel(tx);
    if (result == ESP_OK) ESP_LOGI(TAG, "Audio check finished");
    else ESP_LOGW(TAG, "Audio check failed: %s", esp_err_to_name(result));
}
