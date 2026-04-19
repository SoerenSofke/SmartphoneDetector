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

// ── Types ──────────────────────────────────────────────────
struct MidiEvent
{
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
};

// ── Global state ───────────────────────────────────────────

static QueueHandle_t midi_queue = nullptr;

static I2SClass i2s;
static int16_t audio_buffer[Config::SAMPLES_PER_BLOCK];

static UsbMidi usbMidi;

// ── Helper functions ───────────────────────────────────────

/// Fills the audio buffer.
/// @param buf    destination buffer (stereo, interleaved L/R)
/// @param frames number of stereo frames to generate
/// @return       number of bytes written to the buffer
static size_t fill_audio_block(int16_t *buf, uint16_t frames)
{
    constexpr uint8_t NUM_VOICES = 3;
    constexpr uint8_t NO_TRIG = 0xFF;

    for (uint16_t i = 0; i < frames; ++i)
    {
        // Poll MIDI queue once per sample; map note to voice (note 24 = voice 0)
        MidiEvent event;
        const uint8_t trig_voice =
            (xQueueReceive(midi_queue, &event, 0) == pdTRUE)
                ? static_cast<uint8_t>(event.note - 24)
                : NO_TRIG;

        // Advance every voice and mix; only the triggered voice gets trig=1
        int32_t mix = 0;
        for (uint8_t v = 0; v < NUM_VOICES; ++v)
            mix += pdr_play(v, v == trig_voice, 0);

        // Divide by 2 for headroom; clamp as a last-resort safety net
        mix = constrain(mix >> 1, INT16_MIN, INT16_MAX);

        // Duplicate mono mix to both stereo channels
        buf[2*i] = buf[2*i + 1] = static_cast<int16_t>(mix);
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
    uint8_t channel = data[1] & 0x0F;
    uint8_t note = data[2];
    uint8_t velocity = data[3];

    if (status == 0x90 && velocity > 0)
    {
        MidiEvent event = {channel, note, velocity};
        xQueueSendToBack(midi_queue, &event, 0);
    }
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

    midi_queue = xQueueCreate(Config::QUEUE_LENGTH, sizeof(MidiEvent));

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