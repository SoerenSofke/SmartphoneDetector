#pragma GCC optimize("O3")

#include <atomic>
#include <cstdarg>

#include <ESP_I2S.h>
#include "UsbMidi.h"
#include "AtomicQueue.h"
#include "pdr.h"

// ── Configuration ──────────────────────────────────────────
namespace Config
{
    // 16 contiguous MIDI notes mapped to voice indices.
    constexpr uint8_t NOTE_TO_VOICE[16] = {
        6,
        5,
        4,
        1,
        6,
        5,
        4,
        1,
        9,
        2,
        3,
        0,
        8,
        7,
        10,
        11};

    // I2S pin assignment (adjust to your board)
    constexpr int8_t PIN_BCLK = 5;
    constexpr int8_t PIN_WSEL = 6;
    constexpr int8_t PIN_DOUT = 7;

    // MIDI queue parameters
    constexpr size_t QUEUE_LENGTH = 16;

    // Audio parameters
    constexpr uint32_t SAMPLE_RATE = 44100;
    constexpr uint16_t FRAMES_PER_BLOCK = 128;

    // Derived constants (computed at compile time)
    constexpr size_t SAMPLES_PER_BLOCK = FRAMES_PER_BLOCK * 2; // stereo

    // Voices parameters, used to trigger PDR
    constexpr int8_t VOICES = 12;

    // ── Task configuration ──────────────────────────────────
    //
    // Three explicit FreeRTOS tasks, pinned to separate cores.
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
    //   Core 0 (PRO_CPU) — Stats task, low priority
    //     Periodically prints the peak CPU load published by the
    //     audio task. Low priority (1) ensures it never preempts
    //     MIDI, and its Serial output stays off the audio core
    //     entirely so UART I/O can never delay real-time work.
    //
    // The strict priority gap (Audio 10 > MIDI 5 > Stats 1)
    // guarantees that even if tasks ended up on the same core,
    // audio always preempts MIDI, MIDI always preempts Stats.
    constexpr UBaseType_t AUDIO_TASK_PRIORITY = 10;
    constexpr uint32_t AUDIO_TASK_STACK = 8192;
    constexpr BaseType_t AUDIO_TASK_CORE = 1;

    constexpr UBaseType_t MIDI_TASK_PRIORITY = 5;
    constexpr uint32_t MIDI_TASK_STACK = 4096;
    constexpr BaseType_t MIDI_TASK_CORE = 0;

    constexpr UBaseType_t STATS_TASK_PRIORITY = 1;
    constexpr uint32_t STATS_TASK_STACK = 4096;
    constexpr BaseType_t STATS_TASK_CORE = 0;
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

// Audio-task peak CPU load, published to the stats task on Core 0.
// Writer: audio_task (single producer). Reader: stats_task (exchange(0)
// to read-and-reset). The writer's load-modify-store is not atomic as
// a whole, but the only race is with the reader's exchange: a peak
// spike that loses this race ends up in the next reporting window
// (deferred, never lost). Relaxed ordering suffices — this atomic
// doesn't guard any other data.
namespace stats
{
    static std::atomic<uint32_t> peak_us{0};
}

// ── Helper functions ───────────────────────────────────────

// Serial output with a [HH:MM:SS.mmm] timestamp prefix, printf-style.
// All Serial output in this sketch goes through here so the log is
// uniformly timestamped and single-sourced.
__attribute__((format(printf, 1, 2))) static void tsprint(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const unsigned long ms = millis();
    const unsigned long s = ms / 1000;
    const unsigned long m = s / 60;
    const unsigned long h = m / 60;
    Serial.printf("[%02lu:%02lu:%02lu.%03lu] %s\r\n",
                  h % 100, m % 60, s % 60, ms % 1000, buf);
}

uint8_t nextVoiceVariant(uint8_t voice, uint8_t N)
{
    static uint32_t rng_state = 0x9E3779B9;
    // Initialize "last" to an out-of-range sentinel (0) so the first call
    // for each voice can return any variant in [1, N] without bias.
    static uint8_t last[Config::VOICES] = {0};

    // Trivial cases:
    //   N == 0 -> no variants, return 0 as "skip" indicator
    //   N == 1 -> only one variant, always return 1
    // Storing N into last[voice] preserves the sentinel semantics:
    //   - N=0 keeps last[voice]=0 (still "first call" for next time)
    //   - N=1 sets last[voice]=1 (valid value)
    if (N <= 1)
    {
        last[voice] = N;
        return N;
    }

    // xorshift32
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;

    // Pick the next variant.
    //   - First call for this voice (sentinel 0): uniform over [1, N].
    //   - Subsequent calls: uniform over [1, N] \ {last[voice]} via
    //     Lemire reduction on (N-1) plus the standard "skip last" trick.
    // r and r+1 are provably <= N <= 255, so uint8_t arithmetic is safe.
    uint8_t result;
    if (last[voice] == 0)
    {
        result = (((uint64_t)x * N) >> 32) + 1; // [1, N]
    }
    else
    {
        uint8_t r = (((uint64_t)x * (N - 1)) >> 32) + 1; // [1, N-1]
        result = (r < last[voice]) ? r : r + 1;          // [1, N] \ {last}
    }

    last[voice] = result;
    return result;
}

// Fills the audio buffer.
//   buf    destination buffer (stereo, interleaved L/R)
//   frames number of stereo frames to generate
// Returns the number of bytes written to the buffer.
//
// IRAM_ATTR: places this function (and, via always_inline, the entire
// inlined pdr_play body that lives inside it) in internal IRAM rather
// than flash. Removes flash cache-miss latency from the hot loop —
// relevant mainly after cold starts and on any cache eviction event.
static IRAM_ATTR size_t fill_audio_block(int16_t *buf, uint16_t frames)
{
    constexpr uint8_t NO_TRIG = 0xFF;
    constexpr uint8_t TOGGLE = 0;

    for (uint16_t i = 0; i < frames; ++i)
    {
        // Poll MIDI queue once per sample; map note to voice (note 24 = voice 0).
        MidiEvent event;
        const uint8_t trig_voice =
            midi_queue.pop(event)
                ? static_cast<uint8_t>(Config::NOTE_TO_VOICE[event.note - 24])
                : NO_TRIG;

        // Advance every voice and mix; only the triggered voice gets trig=1.
        int32_t mix = 0;
        for (uint8_t voice = 0; voice < Config::VOICES; ++voice)
            mix += pdr_play(voice, (voice == trig_voice) ? nextVoiceVariant(voice, 5) : 0, TOGGLE);

        // Divide by 4 for headroom; clamp as a last-resort safety net.
        mix = constrain(mix >> 2, INT16_MIN, INT16_MAX);

        // Duplicate mono mix to both stereo channels.
        buf[2 * i] = buf[2 * i + 1] = static_cast<int16_t>(mix);
    }

    return static_cast<size_t>(frames) * 2 * sizeof(int16_t);
}

// Initializes the I2S peripheral. Returns true on success.
static bool init_i2s()
{
    i2s.setPins(Config::PIN_BCLK, Config::PIN_WSEL, Config::PIN_DOUT);

    return i2s.begin(I2S_MODE_STD,
                     Config::SAMPLE_RATE,
                     I2S_DATA_BIT_WIDTH_16BIT,
                     I2S_SLOT_MODE_STEREO);
}

// ── MIDI callbacks ─────────────────────────────────────────
//
// Invoked from the MIDI task context during usbMidi.update().
// Because only that one task ever calls update(), the single-producer
// invariant of AtomicQueue holds by construction.

void onMidiMessage(const uint8_t (&data)[4])
{
    const uint8_t status = data[1] & 0xF0;
    const uint8_t channel = data[1] & 0x0F;
    const uint8_t note = data[2];
    const uint8_t velocity = data[3];

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
    tsprint("[info] MIDI device connected");
}

void onDeviceDisconnected()
{
    tsprint("[info] MIDI device disconnected");
}

// ── Task bodies ────────────────────────────────────────────

// Audio task — drains the MIDI queue and feeds I2S.
// Pacing is provided implicitly by i2s.write(), which blocks until the
// DMA has room. That blocking yields back to the scheduler, so the task
// watchdog stays happy without a manual vTaskDelay.
//
// CPU-load metric:
//   A single block has a fixed wall-clock budget of FRAMES / SAMPLE_RATE
//   seconds (here: 128/48000 = 2667 µs). We measure how much of that
//   budget is spent in fill_audio_block() — the rest is slack time
//   waiting in i2s.write(). The running peak is published to the stats
//   namespace; the stats_task handles formatting and Serial output so
//   that UART I/O can never delay this task.
static IRAM_ATTR void audio_task(void * /*pv*/)
{
    for (;;)
    {
        const uint32_t t0 = micros();
        const size_t bytes = fill_audio_block(audio_buffer,
                                              Config::FRAMES_PER_BLOCK);
        const uint32_t compute_us = micros() - t0;

        // Single-writer atomic update. The load-modify-store is not
        // atomic as a whole, but the only race is with stats_task's
        // exchange(0), which reads and resets in one step — a peak
        // spike that loses this race ends up in the next reporting
        // window (deferred, never lost). Relaxed ordering suffices.
        const uint32_t cur_peak = stats::peak_us.load(std::memory_order_relaxed);
        if (compute_us > cur_peak)
            stats::peak_us.store(compute_us, std::memory_order_relaxed);

        i2s.write(reinterpret_cast<uint8_t *>(audio_buffer), bytes);
    }
}

// MIDI task — polls the USB stack and forwards events into the queue.
// A 1 ms yield matches the USB full-speed frame rate, keeps Core 0
// available for USB's own internal work, and satisfies the task
// watchdog.
static void midi_task(void * /*pv*/)
{
    for (;;)
    {
        usbMidi.update();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Stats task — prints the audio-task peak CPU load once per second.
// Runs on Core 0 at low priority, so neither MIDI nor Audio can be
// delayed by Serial output.
static void stats_task(void * /*pv*/)
{
    constexpr uint32_t BLOCK_US =
        static_cast<uint32_t>(Config::FRAMES_PER_BLOCK) * 1000000UL /
        Config::SAMPLE_RATE;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Atomic read-and-reset in a single operation.
        const uint32_t peak = stats::peak_us.exchange(0, std::memory_order_relaxed);

        if (peak == 0)
            continue; // audio task hasn't produced any blocks yet

        const float peak_pct = 100.0f * peak / BLOCK_US;
        tsprint("[audio] load: peak %5.1f%%", peak_pct);
    }
}

// ── Arduino entry points ───────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(2000);

    if (!init_i2s())
    {
        tsprint("[ERROR] I2S initialization failed. "
                "Check pin assignment and board selection.");
        for (;;)
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

    const BaseType_t stats_ok = xTaskCreatePinnedToCore(
        stats_task, "stats",
        Config::STATS_TASK_STACK, nullptr,
        Config::STATS_TASK_PRIORITY, nullptr,
        Config::STATS_TASK_CORE);

    if (audio_ok != pdPASS || midi_ok != pdPASS || stats_ok != pdPASS)
    {
        tsprint("[ERROR] Failed to create real-time tasks.");
        while (true)
        {
            delay(1000);
        }
    }

    tsprint("[info] Tasks started: audio@Core1(prio 10), midi@Core0(prio 5), "
            "stats@Core0(prio 1)");

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