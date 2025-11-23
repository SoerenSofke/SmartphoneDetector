#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "opl3.h"

#include <functional>
#include <cstdint>
#include <algorithm>

#define SAMPLE_RATE 49716
#define DURATION_SECONDS 2
#define NUM_SAMPLES (SAMPLE_RATE * DURATION_SECONDS)

enum class Operator {
    AMPLITUDE_MODULATION,
    VIBRATO,
    ENVELOP_GENERATOR_TYPE,
    KEY_SCALING_RATE,
    MULTIPLICATION_FACTOR,
    KEY_SCALE_LEVEL,
    TOLTAL_LEVEL,
    ATTACK_RATE,
    DECAY_RATE,
    SUSTAIN_LEVEL,
    RELEASE_RATE,
    WAVEFORM_SELECT,
};

std::function<void(Operator, uint8_t)> createChipOpl3() {
    struct OperatorState {
        uint8_t amplitudeModulation = 0;
        uint8_t vibrato = 0;
        uint8_t envelopGeneratorType = 0;
        uint8_t keyScalingRate = 0;
        uint8_t multiplicationFactor = 0;
        uint8_t keyScaleLevel = 0;
        uint8_t totalLevel = 0;
        uint8_t attackRate = 0;
        uint8_t decayRate = 0;
        uint8_t sustainLevel = 0;
        uint8_t releaseRate = 0;
        uint8_t waveformSelect = 0;
    };
    OperatorState state;

    auto write_am_vib_egt_ksr_mult = [](const OperatorState& state) {
        uint8_t register_value = 0;

        register_value |= (state.amplitudeModulation & 0x01) << 7;
        register_value |= (state.vibrato & 0x01) << 6;
        register_value |= (state.envelopGeneratorType & 0x01) << 5;
        register_value |= (state.keyScalingRate & 0x01) << 4;
        register_value |= (state.multiplicationFactor & 0x0F) << 0;    
    };

    auto write_ksl_tl = [](const OperatorState& state) {
        uint8_t register_value = 0;
        
        register_value |= (state.keyScaleLevel & 0x03) << 6;
        register_value |= (state.totalLevel & 0x3F) << 0;
    };

    auto write_ar_dr = [](const OperatorState& state) {
        uint8_t register_value = 0;
        
        register_value |= (state.attackRate & 0x0F) << 4;
        register_value |= (state.decayRate & 0x0F) << 0;
    };

    auto write_sl_rr = [](const OperatorState& state) {
        uint8_t register_value = 0;
        
        register_value |= (state.sustainLevel & 0x0F) << 4;
        register_value |= (state.releaseRate & 0x0F) << 0;
    };

    auto write_ws = [](const OperatorState& state) {
        uint8_t register_value = 0;
        
        register_value |= (state.waveformSelect & 0x07) << 0;
    };    
    
    return [state, write_am_vib_egt_ksr_mult, write_ksl_tl, write_ar_dr, write_sl_rr, write_ws]
        (Operator op, uint8_t value) mutable {
            
        switch (op) {
            case Operator::AMPLITUDE_MODULATION:
                state.amplitudeModulation = 
                    std::clamp<uint8_t>(value, 0, 1);
                write_am_vib_egt_ksr_mult(state);
                break;

            case Operator::VIBRATO:
                state.vibrato = 
                    std::clamp<uint8_t>(value, 0, 1);
                write_am_vib_egt_ksr_mult(state);
                break;

            case Operator::ENVELOP_GENERATOR_TYPE:
                state.envelopGeneratorType =
                    std::clamp<uint8_t>(value, 0, 1);
                write_am_vib_egt_ksr_mult(state);
                break;                

            case Operator::KEY_SCALING_RATE:
                state.keyScalingRate =
                    std::clamp<uint8_t>(value, 0, 1);
                write_am_vib_egt_ksr_mult(state);                
                break;

            case Operator::MULTIPLICATION_FACTOR:
                state.multiplicationFactor =
                    std::clamp<uint8_t>(value, 0, 15);
                write_am_vib_egt_ksr_mult(state);
                break;

            case Operator::KEY_SCALE_LEVEL:
                state.keyScaleLevel = 
                    std::clamp<uint8_t>(value, 0, 3);
                write_ksl_tl(state);
                break;

            case Operator::TOLTAL_LEVEL:
                state.totalLevel =
                    std::clamp<uint8_t>(value, 0, 63);
                write_ksl_tl(state);
                break;

            case Operator::ATTACK_RATE:
                state.attackRate =
                    std::clamp<uint8_t>(value, 0, 15);
                write_ar_dr(state);
                break;

            case Operator::DECAY_RATE:
                state.decayRate = 
                    std::clamp<uint8_t>(value, 0, 15);
                write_ar_dr(state);
                break;

            case Operator::SUSTAIN_LEVEL:
                state.sustainLevel = 
                    std::clamp<uint8_t>(value, 0, 15);
                write_sl_rr(state);
                break;

            case Operator::RELEASE_RATE:
                state.releaseRate = 
                    std::clamp<uint8_t>(value, 0, 15);
                write_sl_rr(state);
                break;

            case Operator::WAVEFORM_SELECT:
                state.waveformSelect = 
                    std::clamp<uint8_t>(value, 0, 7);
                break;
        }
    };
}



// Helper function to write OPL3 register
void write_register(opl3_chip *chip, uint16_t reg, uint8_t value) {
    OPL3_WriteRegBuffered(chip, reg, value);
}

void begin(opl3_chip *chip, uint32_t sample_rate) {
    // 1. Chip initialisieren (setzt ALLE Register auf 0x00)
    OPL3_Reset(chip, sample_rate);
    
    // 2. OPL3-Modus aktivieren
    OPL3_WriteReg(chip, 0x105, 0x01);

    // 3. Percussion-Modus aktivieren (Register 0xBD, Bit 5)
    struct BD {
        uint8_t DAM : 1;
        uint8_t DVB : 1;
        uint8_t RYT : 1;
        uint8_t BD : 1;
        uint8_t SD : 1;
        uint8_t TOM : 1;
        uint8_t TC : 1;
        uint8_t HH : 1;
    };

    struct BD bd = {.DAM = 0, .DVB = 0, .RYT = 1, .BD = 0, .SD = 0, .TOM = 0, .TC = 0, .HH = 0};
    OPL3_WriteReg(chip, 0xBD, *(uint8_t*)&bd);

    auto chipOpl3 = createChipOpl3();
    chipOpl3(Operator::AMPLITUDE_MODULATION, 5);
}

int main(void) {
    int16_t *stream_buffer = (int16_t*) malloc(NUM_SAMPLES * 2 * sizeof(int16_t));

    FILE *output;

    opl3_chip chip;

    begin(&chip, SAMPLE_RATE);

    /*
    // -------- Sine Wave ------------
        
    // Configure channel 0, operator 1 (modulator)
    // Register 0x20: Tremolo=0, Vibrato=0, Sustain=1, KSR=0, Mult=1
    write_register(&chip, 0x20, 0x21);
    
    // Register 0x40: Key Scale Level=0, Output Level=0x00 (maximum)
    write_register(&chip, 0x40, 0x00);
    
    // Register 0x60: Attack=0xF (fastest), Decay=0x5
    write_register(&chip, 0x60, 0xF5);
    
    // Register 0x80: Sustain=0x3, Release=0x7
    write_register(&chip, 0x80, 0x37);
    
    // Register 0xE0: Waveform=0 (sine wave)
    write_register(&chip, 0xE0, 0x00);


    
    // Configure channel 0, operator 2 (carrier)
    // Register 0x23: Tremolo=0, Vibrato=0, Sustain=1, KSR=0, Mult=1
    write_register(&chip, 0x23, 0x21);
    
    // Register 0x43: Key Scale Level=0, Output Level=0x00 (maximum)
    write_register(&chip, 0x43, 0x00);
    
    // Register 0x63: Attack=0xF (fastest), Decay=0x5
    write_register(&chip, 0x63, 0xF5);
    
    // Register 0x83: Sustain=0x3, Release=0x7
    write_register(&chip, 0x83, 0x37);
    
    // Register 0xE3: Waveform=0 (sine wave)
    write_register(&chip, 0xE3, 0x00);

    
    // Configure channel 0 connection
    // Register 0xC0: Right=1, Left=1, Feedback=0, FM synthesis
    write_register(&chip, 0xC0, 0x30);
    
    // Set frequency for channel 0 (A4 = 440 Hz)
    // F-Number calculation: F = freq * 2^(20-block) / (49716 Hz)
    // For A4 at block 4: F-number ≈ 0x16B (363 decimal)
    write_register(&chip, 0xA0, 0x6B);  // F-number low 8 bits
    
    // Register 0xB0: Key-On=1, Block=4, F-number high 2 bits=1
    write_register(&chip, 0xB0, 0x31);  // Turn note ON
    */
    

    // --- Snare Drum ---- 

    
    
    // === Operator 16 (Snare) - mit längerem Sustain ===
    write_register(&chip, 0x34, 0x0E);   // MULT=14
    write_register(&chip, 0x54, 0x00);   // TL=0 (Maximum Lautstärke!)
    
    // KRITISCH: Attack + Decay + Sustain + Release müssen zusammenpassen!
    write_register(&chip, 0x74, 0xF6);   // AR=15 (instant), DR=0 (kurz)
    write_register(&chip, 0x94, 0xFF);   // SL=0 (stays at max), RR=15 (kurz nach Key-Off)
    
    write_register(&chip, 0xF4, 0x00);   // Waveform 2
    
    // === Kanal 7 ===
    write_register(&chip, 0xA7, 0x80);
    write_register(&chip, 0xB7, 0x15);
    write_register(&chip, 0xC7, 0x33);   // Stereo + Feedback
    
    // === JETZT: Snare KEY-ON ===
    write_register(&chip, 0xBD, 0x28);   // Snare AN!

    // --- start generation ---



    
    // Open output file
    output = fopen("tone_example.raw", "wb");
    if (!output) {
        fprintf(stderr, "Failed to open output file\n");
        return 1;
    }
    
    // Generate audio samples & write to file
    OPL3_GenerateStream(&chip, stream_buffer, NUM_SAMPLES);
    fwrite(stream_buffer, sizeof(int16_t), NUM_SAMPLES * 2, output);
    
    free(stream_buffer);
    fclose(output);
    
    printf("Generated %d samples at %d Hz\n", NUM_SAMPLES, SAMPLE_RATE);
    printf("Output written to tone_example.raw\n");
    printf("Play with: ffplay -f s16le -ar %d -ac 2 tone_example.raw\n", SAMPLE_RATE);
    
    return 0;
}
