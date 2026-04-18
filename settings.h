#ifndef SETTINGS_H
#define SETTINGS_H
#include "math.h"
#include "fpu32/fpu_rfft.h" // This provides the definition for CFFT_f32_Struct



// Sampling & FFT Parameters
#define FFT_SIZE           1024
#define TOTAL_BUFFER_SIZE  (FFT_SIZE * 2)
#define FFT_STAGES      (int)log2(FFT_SIZE)

// Global External Variables
extern uint16_t adcAResults[TOTAL_BUFFER_SIZE];
extern volatile uint16_t pingPongFlag;
// Define handle and memory
extern RFFT_F32_STRUCT rfft;
// Handle to the CFFT_F32_STRUCT object
extern RFFT_F32_STRUCT_Handle  hnd_rfft;

#endif 

