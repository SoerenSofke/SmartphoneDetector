#include <freertos/queue.h>
#include <ESP_I2S.h>
#include "UsbMidi.h"

#include "pdr.h"

// ── Configuration ──────────────────────────────────────────
namespace Config
{
    // Queue parameters
    constexpr size_t QUEUE_LENGTH = 16;

    // Audio parameters
    constexpr uint32_t SAMPLE_RATE = 48000;
    constexpr uint16_t FRAMES_PER_BLOCK = 128;

    // I2S pin assignment (adjust to your board)
    constexpr int8_t PIN_BCLK = 5;
    constexpr int8_t PIN_WSEL = 6;
    constexpr int8_t PIN_DOUT = 7;

    // Derived constants (computed at compile time)
    constexpr size_t SAMPLES_PER_BLOCK = FRAMES_PER_BLOCK * 2; // stereo
}

// ── Global state ───────────────────────────────────────────

static QueueHandle_t trigger_queue = nullptr;

static I2SClass i2s;
static int16_t audio_buffer[Config::SAMPLES_PER_BLOCK];

static UsbMidi usbMidi;

static uint8_t voice_index = 0;

// ── Helper functions ───────────────────────────────────────

/// Fills the audio buffer.
/// @param buf    destination buffer (stereo, interleaved L/R)
/// @param frames number of stereo frames to generate
/// @return       number of bytes written to the buffer
static size_t fill_audio_block(int16_t *buf, uint16_t frames)
{
    for (uint16_t i = 0; i < frames; ++i)
    {
        bool trigger = false;
        xQueueReceive(trigger_queue, &trigger, 0);

        if (trigger)
        {
            voice_index = (voice_index + 1) % 4;
        }

        const int16_t sample = static_cast<int16_t>(lrintf(32768.0f * pdr_play(0, trigger ? voice_index+1 : 0, 0)));
        buf[i * 2] = sample;     // left
        buf[i * 2 + 1] = sample; // right
    }

    return static_cast<size_t>(frames) * 2 * sizeof(int16_t);
}

/// Initializes the I2S peripheral.
/// @return true on success
static bool init_i2s()
{
    i2s.setPins(Config::PIN_BCLK, Config::PIN_WSEL, Config::PIN_DOUT);

    return i2s.begin(I2S_MODE_STD,
                     Config::SAMPLE_RATE,
                     I2S_DATA_BIT_WIDTH_16BIT,
                     I2S_SLOT_MODE_STEREO);
}

void tsprint(const char *msg)
{
    unsigned long ms = millis();
    unsigned long s = ms / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    Serial.printf("[%02lu:%02lu:%02lu.%03lu] %s\r\n",
                  h % 100, m % 60, s % 60, ms % 1000, msg);
}

// ── MIDI callbacks ───────────────────────────────────

void onMidiMessage(const uint8_t (&data)[4])
{
    uint8_t status = data[1] & 0xF0;
    uint8_t velocity = data[3];

    if (status == 0x90 && velocity > 0)
    {
        bool trigger = true;
        xQueueSendToBack(trigger_queue, &trigger, 0);
    }

    tsprint("MIDI Message Received");
}

void onDeviceConnect()
{
    tsprint("MIDI Device Connected");
}

void onDeviceDisconnected()
{
    tsprint("MIDI Device Disconnected");
}

// ── Arduino entry points ───────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(2000);

    trigger_queue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(bool));

    if (!init_i2s())
    {
        Serial.println("[ERROR] I2S initialization failed.");
        Serial.println("        Check pin assignment and board selection.");
        while (true)
        {
            delay(1000);
        }
    }

    usbMidi.onMidiMessage(onMidiMessage);
    usbMidi.onDeviceConnected(onDeviceConnect);
    usbMidi.onDeviceDisconnected(onDeviceDisconnected);
    usbMidi.begin();
}

void loop()
{
    const size_t bytes = fill_audio_block(audio_buffer, Config::FRAMES_PER_BLOCK);
    i2s.write(reinterpret_cast<uint8_t *>(audio_buffer), bytes);

    usbMidi.update();
}