#include <ESP_I2S.h>
#include <math.h>
#include "UsbMidi.h"

// ── Configuration ──────────────────────────────────────────
namespace Config
{
    // Audio parameters
    constexpr uint32_t SAMPLE_RATE = 44100;
    constexpr float TONE_HZ = 440.0f;
    constexpr float AMPLITUDE = 0.5f; // 0.0 – 1.0
    constexpr uint16_t FRAMES_PER_BLOCK = 128;

    // I2S pin assignment (adjust to your board)
    constexpr int8_t PIN_BCLK = 5;
    constexpr int8_t PIN_WSEL = 6;
    constexpr int8_t PIN_DOUT = 7;

    // Derived constants (computed at compile time)
    constexpr float PHASE_INC = 2.0f * M_PI * TONE_HZ / SAMPLE_RATE;
    constexpr int16_t AMPLITUDE_I16 = static_cast<int16_t>(AMPLITUDE * 32767.0f);
    constexpr size_t SAMPLES_PER_BLOCK = FRAMES_PER_BLOCK * 2; // stereo
    constexpr size_t BUFFER_BYTES = SAMPLES_PER_BLOCK * sizeof(int16_t);
}

// ── Global state ───────────────────────────────────────────
static I2SClass i2s;
static float phase = 0.0f;
static int16_t audio_buffer[Config::SAMPLES_PER_BLOCK];

static UsbMidi usbMidi;

// ── Helper functions ───────────────────────────────────────

/// Fills the audio buffer with a sine tone.
/// @param buf    destination buffer (stereo, interleaved L/R)
/// @param frames number of stereo frames to generate
/// @return       number of bytes written to the buffer
static size_t fill_sine_block(int16_t *buf, uint16_t frames)
{
    for (uint16_t i = 0; i < frames; ++i)
    {
        const int16_t sample =
            static_cast<int16_t>(sinf(phase) * Config::AMPLITUDE_I16);

        buf[i * 2] = sample;     // left
        buf[i * 2 + 1] = sample; // right

        phase += Config::PHASE_INC;
    }

    // Keep phase within [0, 2π) to prevent
    // precision loss during long runtime
    phase = fmodf(phase, 2.0f * M_PI);

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
    const size_t bytes = fill_sine_block(audio_buffer, Config::FRAMES_PER_BLOCK);
    const size_t written = i2s.write(reinterpret_cast<uint8_t *>(audio_buffer), bytes);

    usbMidi.update();
}