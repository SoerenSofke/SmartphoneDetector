#include <ESP_I2S.h>
#include "UsbMidi.h"
#include "AtomicQueue.h"
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

    // ── Task configuration ──────────────────────────────────
    //
    // Two explicit FreeRTOS tasks, pinned to separate cores.
    //
    //   Core 1 (APP_CPU) — Audio task, high priority
    //     On ESP32-S3 Core 1 is traditionally free of WiFi/BT/USB
    //     system work, which makes it the cleanest place for hard
    //     real-time DSP. Priority 10 sits above ordinary user work
    //     (Arduino loopTask = 1) but well below kernel-critical
    //     tasks (20+), so the scheduler can still service timers
    //     and driver callbacks without being blocked by audio.
    //
    //   Core 0 (PRO_CPU) — MIDI task, moderate priority
    //     Shares the core with TinyUSB's internal tasks and other
    //     system work. A few ms of MIDI latency is imperceptible,
    //     so priority 5 keeps the task responsive without ever
    //     threatening the audio task should affinity slip.
    //
    // The strict priority gap (10 vs 5) guarantees that even if
    // both tasks ended up on the same core for any reason, audio
    // would always preempt MIDI, never the other way round.
    constexpr UBaseType_t AUDIO_TASK_PRIORITY = 10;
    constexpr uint32_t    AUDIO_TASK_STACK    = 8192;
    constexpr BaseType_t  AUDIO_TASK_CORE     = 1;

    constexpr UBaseType_t MIDI_TASK_PRIORITY  = 5;
    constexpr uint32_t    MIDI_TASK_STACK     = 4096;
    constexpr BaseType_t  MIDI_TASK_CORE      = 0;
}

// ── Types ──────────────────────────────────────────────────
struct MidiEvent
{
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
};

// ── Global state ───────────────────────────────────────────

// Shared SPSC queue.
//   Producer: MIDI task (Core 0) via onMidiMessage callback.
//   Consumer: Audio task (Core 1) via fill_audio_block.
// Lives in internal SRAM — global/static storage on ESP32-S3
// is not PSRAM-backed by default, which is exactly what we need
// for the cross-core memory ordering guarantees.
static AtomicQueue<MidiEvent, Config::QUEUE_LENGTH> midi_queue;

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
    constexpr uint8_t NUM_VOICES = 12;
    constexpr uint8_t NO_TRIG = 0xFF;

    for (uint16_t i = 0; i < frames; ++i)
    {
        // Poll MIDI queue once per sample; map note to voice (note 24 = voice 0)
        MidiEvent event;
        const uint8_t trig_voice =
            midi_queue.pop(event)
                ? static_cast<uint8_t>(event.note - 24)
                : NO_TRIG;

        // Advance every voice and mix; only the triggered voice gets trig=1
        int32_t mix = 0;
        for (uint8_t v = 0; v < NUM_VOICES; ++v)
            mix += pdr_play(v, v == trig_voice, 0);

        // Divide by 4 for headroom; clamp as a last-resort safety net
        mix = constrain(mix >> 2, INT16_MIN, INT16_MAX);

        // Duplicate mono mix to both stereo channels
        buf[2 * i] = buf[2 * i + 1] = static_cast<int16_t>(mix);
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

// ── MIDI callbacks ─────────────────────────────────────────
//
// Invoked from the MIDI task context during usbMidi.update().
// Because only that one task ever calls update(), the single-producer
// invariant of AtomicQueue holds by construction.

void onMidiMessage(const uint8_t (&data)[4])
{
    uint8_t status = data[1] & 0xF0;
    uint8_t channel = data[1] & 0x0F;
    uint8_t note = data[2];
    uint8_t velocity = data[3];

    if (status == 0x90 && velocity > 0)
    {
        MidiEvent event = {channel, note, velocity};
        // Drop events if the audio loop hasn't drained the queue in time.
        // At audible MIDI rates and 48 kHz polling this should not happen;
        // if it does, raise Config::QUEUE_LENGTH.
        (void)midi_queue.push(event);
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

// ── Task bodies ────────────────────────────────────────────

/// Audio task — drains the MIDI queue and feeds I2S.
/// Pacing is provided implicitly by i2s.write(), which blocks until
/// the DMA has room. That blocking yields back to the scheduler, so
/// the task watchdog stays happy without a manual vTaskDelay.
///
/// CPU-load metric:
///   A single block has a fixed wall-clock budget of FRAMES / SAMPLE_RATE
///   seconds (here: 128/48000 = 2667 µs). We measure how much of that
///   budget is spent in fill_audio_block() — the rest is slack time
///   waiting in i2s.write(). Peak and average over each 1 s window are
///   printed to Serial. Peak is the number to watch: if it approaches
///   100 %, an audio glitch is imminent.
static void audio_task(void * /*pv*/)
{
    constexpr uint32_t BLOCK_US =
        static_cast<uint32_t>(Config::FRAMES_PER_BLOCK) * 1000000UL /
        Config::SAMPLE_RATE;

    uint32_t peak_us        = 0;
    uint32_t sum_us         = 0;
    uint32_t count          = 0;
    uint32_t last_report_ms = millis();

    for (;;)
    {
        const uint32_t t0 = micros();
        const size_t bytes = fill_audio_block(audio_buffer,
                                              Config::FRAMES_PER_BLOCK);
        const uint32_t compute_us = micros() - t0;

        if (compute_us > peak_us) peak_us = compute_us;
        sum_us += compute_us;
        ++count;

        i2s.write(reinterpret_cast<uint8_t *>(audio_buffer), bytes);

        const uint32_t now_ms = millis();
        if (now_ms - last_report_ms >= 1000)
        {
            const float avg_pct  = 100.0f * sum_us  / (count * BLOCK_US);
            const float peak_pct = 100.0f * peak_us / BLOCK_US;
            Serial.printf("[audio] load: avg %5.1f%%  peak %5.1f%%\r\n",
                          avg_pct, peak_pct);
            peak_us = 0;
            sum_us  = 0;
            count   = 0;
            last_report_ms = now_ms;
        }
    }
}

/// MIDI task — polls the USB stack and forwards events into the queue.
/// A 1 ms yield matches the USB full-speed frame rate, keeps Core 0
/// available for USB's own internal work, and satisfies the task
/// watchdog.
static void midi_task(void * /*pv*/)
{
    for (;;)
    {
        usbMidi.update();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
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

    // Spawn the real-time tasks, each pinned to its designated core.
    const BaseType_t audio_ok = xTaskCreatePinnedToCore(
        audio_task, "audio",
        Config::AUDIO_TASK_STACK, nullptr,
        Config::AUDIO_TASK_PRIORITY, nullptr,
        Config::AUDIO_TASK_CORE);

    const BaseType_t midi_ok = xTaskCreatePinnedToCore(
        midi_task, "midi",
        Config::MIDI_TASK_STACK, nullptr,
        Config::MIDI_TASK_PRIORITY, nullptr,
        Config::MIDI_TASK_CORE);

    if (audio_ok != pdPASS || midi_ok != pdPASS)
    {
        Serial.println("[ERROR] Failed to create real-time tasks.");
        while (true)
        {
            delay(1000);
        }
    }

    tsprint("Tasks started: audio@Core1(prio 10), midi@Core0(prio 5)");

    // The Arduino loopTask (this context) has nothing left to do.
    // Delete it so we don't consume a scheduling slot on Core 1 for
    // an empty loop() — that slot belongs to the audio task now.
    vTaskDelete(nullptr);
}

void loop()
{
    // Unused — setup() deletes its own (loop) task after spawning
    // the real-time tasks, so loop() is never entered.
}