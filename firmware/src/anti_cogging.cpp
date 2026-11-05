#include "anti_cogging.h"

// 512-point calibration LUT storing feedforward anti-cogging torque (Vq correction)
static float cogging_lut[3][COGGING_LUT_SIZE];

void init_anti_cogging() {
    // Pre-seed with sinusoidal harmonic model (order 6 and 12 stator slot harmonics)
    for (int i = 0; i < COGGING_LUT_SIZE; i++) {
        float theta = (float)i * 2.0f * PI / (float)COGGING_LUT_SIZE;
        // Pan axis harmonics
        cogging_lut[0][i] = 0.08f * sin(6.0f * theta) + 0.03f * sin(12.0f * theta);
        // Roll axis harmonics
        cogging_lut[1][i] = 0.09f * sin(6.0f * theta) + 0.04f * sin(12.0f * theta);
        // Pitch axis harmonics
        cogging_lut[2][i] = 0.07f * sin(6.0f * theta) + 0.02f * sin(12.0f * theta);
    }
}

float get_anti_cogging_torque(uint8_t axis, float electrical_angle) {
    if (axis > 2) return 0.0f;
    float norm_ang = fmod(electrical_angle, 2.0f * PI);
    if (norm_ang < 0.0f) norm_ang += 2.0f * PI;
    int index = (int)(norm_ang * (float)COGGING_LUT_SIZE / (2.0f * PI)) % COGGING_LUT_SIZE;
    return cogging_lut[axis][index];
}
