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
    OP_00, OP_01, OP_02, OP_03, OP_04, OP_05,
    OP_06, OP_07, OP_08, OP_09, OP_10, OP_11,
    OP_12, OP_13, OP_14, OP_15, OP_16, OP_17,

    OP_18, OP_19, OP_20, OP_21, OP_22, OP_23,
    OP_24, OP_25, OP_26, OP_27, OP_28, OP_29,
    OP_30, OP_31, OP_32, OP_33, OP_34, OP_35
};

constexpr uint16_t getRegisterOffset(Operator op) {
    constexpr uint16_t offsets[36] = {        
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,

        0x100, 0x101, 0x102, 0x103, 0x104, 0x105,
        0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D,
        0x110, 0x111, 0x112, 0x113, 0x114, 0x115
    };
    return offsets[static_cast<int>(op)];
}


enum class Parameter {
    AMPLITUDE_MODULATION = 0,
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

std::function<void(Operator, Parameter, uint8_t)> createChipOpl3() {
    enum class Register {
        AM_VIB_EGT_MRS_MULT,
        KSL_TL,
        AR_DR,
        SL_RR,
        WS
    };

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
    OperatorState state[36];

    auto writeRegister = [](const Operator& op, const Register& reg, const OperatorState& state) {
        uint8_t register_value = 0;
        
        switch (reg) {
            case Register::AM_VIB_EGT_MRS_MULT:
                register_value |= (state.amplitudeModulation & 0x01) << 7;
                register_value |= (state.vibrato & 0x01) << 6;
                register_value |= (state.envelopGeneratorType & 0x01) << 5;
                register_value |= (state.keyScalingRate & 0x01) << 4;
                register_value |= (state.multiplicationFactor & 0x0F) << 0;
                break;

            case Register::KSL_TL:
                register_value |= (state.keyScaleLevel & 0x03) << 6;
                register_value |= (state.totalLevel & 0x3F) << 0;
                break;

            case Register::AR_DR:
                register_value |= (state.attackRate & 0x0F) << 4;
                register_value |= (state.decayRate & 0x0F) << 0;
                break;

            case Register::SL_RR:
                register_value |= (state.sustainLevel & 0x0F) << 4;
                register_value |= (state.releaseRate & 0x0F) << 0;
                break;

            case Register::WS:
                register_value |= (state.waveformSelect & 0x07) << 0;
                break;
        }
    };
    
    return [state, writeRegister]
        (const Operator op, const Parameter par, uint8_t value) mutable {

        int idx = static_cast<int>(op);
        switch (par) {
            case Parameter::AMPLITUDE_MODULATION:
                state[idx].amplitudeModulation = 
                    std::clamp<uint8_t>(value, 0, 1);
                writeRegister(op, Register::AM_VIB_EGT_MRS_MULT, state[idx]);
                break;

            case Parameter::VIBRATO:
                state[idx].vibrato = 
                    std::clamp<uint8_t>(value, 0, 1);
                writeRegister(op, Register::AM_VIB_EGT_MRS_MULT, state[idx]);
                break;

            case Parameter::ENVELOP_GENERATOR_TYPE:
                state[idx].envelopGeneratorType =
                    std::clamp<uint8_t>(value, 0, 1);
                writeRegister(op, Register::AM_VIB_EGT_MRS_MULT, state[idx]);
                break;                

            case Parameter::KEY_SCALING_RATE:
                state[idx].keyScalingRate =
                    std::clamp<uint8_t>(value, 0, 1);
                writeRegister(op, Register::AM_VIB_EGT_MRS_MULT, state[idx]);
                break;

            case Parameter::MULTIPLICATION_FACTOR:
                state[idx].multiplicationFactor =
                    std::clamp<uint8_t>(value, 0, 15);
                writeRegister(op, Register::AM_VIB_EGT_MRS_MULT, state[idx]);
                break;

            case Parameter::KEY_SCALE_LEVEL:
                state[idx].keyScaleLevel = 
                    std::clamp<uint8_t>(value, 0, 3);
                writeRegister(op, Register::KSL_TL, state[idx]);
                break;

            case Parameter::TOLTAL_LEVEL:
                state[idx].totalLevel =
                    std::clamp<uint8_t>(value, 0, 63);
                writeRegister(op, Register::KSL_TL, state[idx]);
                break;

            case Parameter::ATTACK_RATE:
                state[idx].attackRate =
                    std::clamp<uint8_t>(value, 0, 15);
                writeRegister(op, Register::AR_DR, state[idx]);
                break;

            case Parameter::DECAY_RATE:
                state[idx].decayRate = 
                    std::clamp<uint8_t>(value, 0, 15);
                writeRegister(op, Register::AR_DR, state[idx]);
                break;

            case Parameter::SUSTAIN_LEVEL:
                state[idx].sustainLevel = 
                    std::clamp<uint8_t>(value, 0, 15);
                writeRegister(op, Register::SL_RR, state[idx]);
                break;

            case Parameter::RELEASE_RATE:
                state[idx].releaseRate = 
                    std::clamp<uint8_t>(value, 0, 15);
                writeRegister(op, Register::SL_RR, state[idx]);
                break;

            case Parameter::WAVEFORM_SELECT:
                state[idx].waveformSelect = 
                    std::clamp<uint8_t>(value, 0, 7);
                writeRegister(op, Register::WS, state[idx]);
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
    chipOpl3(Operator::OP_01, Parameter::AMPLITUDE_MODULATION, 5);
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
