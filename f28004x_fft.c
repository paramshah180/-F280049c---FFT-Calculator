// the includes
//*****************************************************************************
#include <string.h>



//#include <fpu.h>

#include "dsp.h"
#include "settings.h"
#include "math.h"

//*****************************************************************************
// the definess
//*****************************************************************************
#define SAMPLING_FREQ   (1000000.0f)       // 1000 kHz
#define half            (FFT_SIZE/2)
//*****************************************************************************
// the globals
//*****************************************************************************

float bin_width = SAMPLING_FREQ / (FFT_SIZE);
float peak_freq_results;
extern float RFFT_f32_twiddleFactors[];
int signal_bin; 






#ifdef __cplusplus
#pragma DATA_SECTION("FFT_buffer_2")
#else
#pragma DATA_SECTION(test_output, "FFT_buffer_2")
#pragma DATA_ALIGN(test_output, FFT_SIZE * 2); 
float test_output[FFT_SIZE];

float amplitude;
float phase_final;
float phase[FFT_SIZE/2];

// Define handle and memory
//CFFT_F32_STRUCT cfft;
// Handle to the CFFT_F32_STRUCT object
//CFFT_F32_STRUCT_Handle hnd_cfft = &cfft;

#endif


//*****************************************************************************
// the function definitions
//*****************************************************************************
void runFFT(float* pData)
{
    
    float max_mag = 0.0f;
    uint16_t max_idx = 0;
    int i;
    

    // Reset pointers
    hnd_rfft->InBuf  = pData;        // Pointer to ADC data (float)
    hnd_rfft->OutBuf = test_output;  // Pointer to FFT result buffer
    hnd_rfft->MagBuf = pData;        // writes magnitudes over pData (ADC Data)
    hnd_rfft->PhaseBuf = phase;      //writes phase data to phase buffer
    // 2. Execute Real FFT
    // This takes N real points and produces N/2 + 1 complex points 
    // packed into the 'test_output' array.
    RFFT_f32(hnd_rfft);

    // 3. Phase Calculation

    RFFT_f32_phase_TMU0(hnd_rfft);

    // 4. Magnitude Calculation
    // RFFT uses a specialized magnitude function for its packed format
    RFFT_f32_mag_TMU0(hnd_rfft);
    

    /*
    // 5. Peak Find (Only from 0 to FFT_Size/2 because cant sense frequency over Nyquist)
    // Index 0 is DC, so we start at 1
    for(i = 1; i < (FFT_SIZE / 2); i++)
    {
        if(pData[i] > max_mag)
        {
            max_mag = pData[i];
            max_idx = i;
        }
    }
    */
    
    //peak_freq_results = (float)max_idx * bin_width;
    amplitude = (2 * pData[max_idx] / FFT_SIZE) * ((3.3 * 3/2)/4095) + .025;  // Calculate the Amplitude of the signal, had to calibrate
    
    signal_bin = roundf(roundf(Signal_Freq / bin_width)/2);
    phase_final = phase[signal_bin];
    peak_freq_results = signal_bin * bin_width;

    }




