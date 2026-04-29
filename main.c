
//
// Included Files
//
#include "f28x_project.h"
#include "settings.h"
#include <stdio.h>



//
// Globals
//
uint16_t adcAResults[TOTAL_BUFFER_SIZE];     // Buffer for results
uint16_t index;                              // Index into result buffer
volatile uint16_t bufferFull;                // Flag to indicate buffer is full

// Define handle and memory
RFFT_F32_STRUCT rfft;
// Handle to the RFFT_F32_STRUCT object
RFFT_F32_STRUCT_Handle hnd_rfft = &rfft;

//
// Function Prototypes
//
void initADC(void);
void initEPWM(void);
void initDMA(void);

__interrupt void dmaCh1ISR(void);

extern void runFFT(float* pData); 


#pragma DATA_SECTION(adcAResults, "ramgs0"); //Place adcAResults in memory block ramgs0
#pragma DATA_ALIGN(adcAResults, FFT_SIZE * 2); //forces compiler to place variable at memory address that is multiple 
                                       // of second value 

volatile uint16_t pingPongFlag = 0; // 0 = First half full, 1 = Second half full
volatile uint16_t readyForFFT = 0;
float fftBuffer[FFT_SIZE];
uint16_t *currentFFTBufferPtr;



//
// Main
//
void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    InitSysCtrl();

    DisableDog(); //turn off watchdog timer

    //
    // Initialize GPIO
    //
    InitGpio();

    //
    // Disable CPU interrupts
    //
    DINT;

    //
    // Initialize the PIE control registers to their default state.
    // The default state is all PIE interrupts disabled and flags
    // are cleared. The Peripheral Interrupt Expansion module allows for peripherals 
    // to interrupt the CPU. The init disables previous interrupts and clears old flags
    //
    InitPieCtrl();

    //
    // Disable CPU interrupts and clear all CPU interrupt flags:
    //
    IER = 0x0000;
    IFR = 0x0000;

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    // This is a list of memory addresses to point the CPU to a specfic function when an interrupt occurs. 
    //Init fills it with default addresses
    InitPieVectTable();

    //
    // Map ISR functions
    // Explecitly map the CPU to dma function during interrupt
    EALLOW;
    PieVectTable.DMA_CH1_INT = &dmaCh1ISR; // Map DMA Channel 1 interrupt
    EDIS;

    //
    // Configure the ADC and power it up
    //
    initADC();

    //
    // Configure the ePWM
    //
    initEPWM();

    //Configure the DMA
    initDMA();


    //
    // Enable global Interrupts and higher priority real-time debug events:
    //
    IER |= M_INT1;  // Enable group 1 interrupts
    IER |= M_INT7;  // Enable Group 7 interrupts(DMA)

    // Enable specific DMA Channel 1 interrupt within group 7 channel
    PieCtrlRegs.PIEIER7.bit.INTx1 = 1; 

    EINT;           // Enable Global interrupt INTM
    ERTM;           // Enable Global realtime interrupt DBGM

    //
    // Initialize results buffer
    //
    for(index = 0; index < TOTAL_BUFFER_SIZE; index++)
    {
        adcAResults[index] = 0;
    }

    index = 0;
    bufferFull = 0;

    // Initial Configuration (Only need to do this once)
    RFFT_f32_setTwiddlesPtr(hnd_rfft, RFFT_f32_twiddleFactors);  //creates a lookup table of sin and cos values to refrence later
    RFFT_f32_setStages(hnd_rfft, FFT_STAGES); //Sets how many stages the fft needs to do the calculations 
    RFFT_f32_setFFTSize(hnd_rfft, FFT_SIZE); //fft size is FFT_Size, fft only needs FFT_size values to get all values


    //
    // Enable PIE interrupt
    //
    //PieCtrlRegs.PIEIER1.bit.INTx1 = 1;

    //
    // Sync ePWM
    //
    EALLOW;
    CpuSysRegs.PCLKCR0.bit.TBCLKSYNC = 1;

    
    // Start the ePWM clock and the SOC generation
    EPwm1Regs.ETSEL.bit.SOCAEN = 1;    // Enable the ePWM1 ADC SOCA pulse
    EPwm1Regs.TBCTL.bit.CTRMODE = 0;   // Unfreeze, and enter up count mode

    EALLOW;
    DmaRegs.CH1.CONTROL.bit.RUN = 1; 
    EDIS;
   

    while(1)
    {
        //1. STALL here until the DMA Interrupt changes the flag, stays here while readyforFFT = 0 proceeds once readyforFFT = 1
        while(readyForFFT == 0); 

         //2. STOP THE DMA: Disable the channel so it doesn't overwrite data while CPU works
        //EALLOW;
        //DmaRegs.CH1.CONTROL.bit.RUN = 0; 
        //DmaRegs.CH1.CONTROL.bit.HALT = 1; 
        //EDIS;

        // 3. Capture which half is ready into a local variable
        uint16_t currentBank = pingPongFlag;

        // 4. Reset the global flag so the DMA can set it again later
        readyForFFT = 0; 

        uint16_t *rawSource; 

        if(currentBank == 1) {
            rawSource = &adcAResults[0];        // First Half
        } else {
            rawSource = &adcAResults[FFT_SIZE]; // Second Half
        }

        // 5. CONVERSION STEP: 16-bit INT to 32-bit FLOAT
        for(index = 0; index < FFT_SIZE; index++) {
            //rawSource[index] = rawSource[index];
            fftBuffer[index] = rawSource[index];
            fftBuffer[index] = fftBuffer[index]  - (4095 * 1.5/3.3);

            //printf("Value: %f\n", fftBuffer[index]);
        }


            // 6. Run the FFT
            runFFT(fftBuffer);

            

             // 7. RESTART THE DMA: Now that CPU is done, let it fill the next buffer
            //EALLOW;
            //DmaRegs.CH1.CONTROL.bit.PERINTCLR = 1; // Clear any pending sync triggers
            //DmaRegs.CH1.CONTROL.bit.ERRCLR = 1;    // Clear any sync error flags
            //DmaRegs.CH1.CONTROL.bit.RUN = 1;       // Re-enable
            //EDIS;
    }

}
//
// initADC - Function to configure and power up ADCA.
//
void initADC(void)
{
    //
    // Setup VREF as internal
    //
    SetVREF(ADC_ADCA, ADC_INTERNAL, ADC_VREF3P3);

    EALLOW;

    AdcaRegs.ADCSOC0CTL.bit.TRIGSEL = 5; //ADC is being triggered by ePWM1,ADCSOCA
    AdcaRegs.ADCSOC0CTL.bit.CHSEL = 0; //Using ADCINO
    AdcaRegs.ADCSOC0CTL.bit.ACQPS = 14; //Sample and Hold time of 14 system clock cycles (150 ns)

    AdcaRegs.ADCINTSEL1N2.bit.INT1SEL = 0; // End of SOC0 will set INT1 flag
    AdcaRegs.ADCINTSEL1N2.bit.INT1E = 1;   // Enable INT1 flag
    AdcaRegs.ADCINTFLGCLR.bit.ADCINT1 = 1; // Make sure INT1 flag is cleared

    AdcaRegs.ADCINTSEL1N2.bit.INT1CONT = 1; // Allow ADC to trigger DMA without CPU intervention

    //
    // Set ADCCLK divider to /4
    //
    AdcaRegs.ADCCTL2.bit.PRESCALE = 0; //ADC Clock and System Clock Synchronized (100 MHz)

    //
    // Set pulse positions to late
    //
    AdcaRegs.ADCCTL1.bit.INTPULSEPOS = 1;

    //
    // Power up the ADC and then delay for 1 ms
    //
    AdcaRegs.ADCCTL1.bit.ADCPWDNZ = 1; 
    
    EDIS;

    DELAY_US(1000);
}

//
// initEPWM - Function to configure ePWM1 to generate the SOC.
//
void initEPWM(void)
{
    EALLOW;

// Time-Base Setup

//EPwm1Regs.TBPRD = 50;
//EPwm1Regs.TBCTL.bit.CTRMODE = 1; // Up Down Mode

EPwm1Regs.TBPRD = 99;                       // Set period for 1000 kHz in Up mode
EPwm1Regs.TBCTL.bit.CTRMODE = 0; // Up Mode
EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0; // Ensure 100MHz clock to PWM
EPwm1Regs.TBCTL.bit.CLKDIV = 0;

// Event-Trigger Setup
EPwm1Regs.ETSEL.bit.SOCAEN = 1;             // Enable SOCA
EPwm1Regs.ETSEL.bit.SOCASEL = 1;          // Trigger ADC when counter is 0
EPwm1Regs.ETPS.bit.SOCAPRD = 1;            // Trigger on every event


    EDIS;
}



void initDMA(void)
{
    EALLOW;
    // 1. Trigger Source: ADCINT1 (Trigger number varies by device, usually 1 for ADCA1)
    DmaRegs.CH1.MODE.bit.PERINTSEL = 1; 
    DmaRegs.CH1.MODE.bit.PERINTE = 1;      // Peripheral interrupt trigger enabled

    // 2. Burst Settings: 1 word per trigger
    DmaRegs.CH1.BURST_SIZE.all = 0;        // 0 means 1 word per burst (1 sample)
    DmaRegs.CH1.SRC_BURST_STEP = 0;        // Don't move source addr during burst
    DmaRegs.CH1.DST_BURST_STEP = 0;        // Don't move dest addr during burst

    // 3. Transfer Settings: 
    DmaRegs.CH1.TRANSFER_SIZE = FFT_SIZE - 1;  //How many bursts to fill the buffer (512) and intiate CPU interrupt
    DmaRegs.CH1.SRC_TRANSFER_STEP = 0;     // Always read from the same ADC register
    DmaRegs.CH1.DST_TRANSFER_STEP = 1;     // Move to next array index after each sample

    // 4. Wrap Settings ("Ping-Pong")
    DmaRegs.CH1.DST_WRAP_SIZE = TOTAL_BUFFER_SIZE - 1; // Wrap every 1024 bursts (one full FFT's worth of data) resets destination pointer address after FFT_Size samples
    DmaRegs.CH1.DST_WRAP_STEP = 0; // Reset pointer back to the start of the buffer

    // 5. Addresses
    DmaRegs.CH1.SRC_ADDR_SHADOW = (uint32_t)&AdcaResultRegs.ADCRESULT0;  //Reads from the ADCresult0 register
    DmaRegs.CH1.DST_ADDR_SHADOW = (uint32_t)&adcAResults[0];  //Destination address pointer to adcAResults that starts at index 0 and increases

    // 6. Wrap around and Continuous
    DmaRegs.CH1.MODE.bit.CONTINUOUS = 1;   // Keep going after buffer is full
    DmaRegs.CH1.MODE.bit.CHINTE = 1;       // Enable CPU interrupt at end of transfer
    DmaRegs.CH1.MODE.bit.CHINTMODE = 1;    // Generate CPU interrupt at end of transfer


    // 7. Clear any existing error flags and initialize active registers from shadow
    DmaRegs.CH1.CONTROL.bit.ERRCLR = 1;      // Clear sync error flags
    DmaRegs.CH1.CONTROL.bit.PERINTCLR = 1;   // Clear any pending peripheral interrupts
    
    // 8. Start the channel
    DmaRegs.CH1.CONTROL.bit.RUN = 1;     


    

    DmaClaSrcSelRegs.DMACHSRCSEL1.bit.CH1 = DMA_ADCAINT1;
    EDIS;


}

    // This interrupt occurs every time a transfer is complete. Transfer size is set in initDMA. 
__interrupt void dmaCh1ISR(void)
{
    EALLOW;
    // Check where the DMA is currently writing to determine which half is "safe" to read
    // 1. Process based on the flag we set PREVIOUSLY
    if (pingPongFlag == 0) 
    {
        // DMA just finished filling indices 0 to 1023 (First Half)
        // Set up the NEXT run to fill indices 1024 to 2047
        DmaRegs.CH1.DST_ADDR_SHADOW = (uint32_t)&adcAResults[FFT_SIZE];
        
        // This tells your main loop: "The first half is ready for FFT"
        readyForFFT = 1; 
        currentFFTBufferPtr = adcAResults; 
        
        pingPongFlag = 1; // Toggle for NEXT time
    } 
    else 
    {
        // DMA just finished filling indices 1024 to 2047 (Second Half)
        // Set up the NEXT run to fill indices 0 to 1023
        DmaRegs.CH1.DST_ADDR_SHADOW = (uint32_t)&adcAResults;
        
        // This tells your main loop: "The second half is ready for FFT"
        readyForFFT = 1;
        currentFFTBufferPtr = &adcAResults[FFT_SIZE];
        
        pingPongFlag = 0; // Toggle for NEXT time
    }

    
    // 2. RE-ARM THE DMA: Clear the peripheral trigger flag
    // This is the "latch" that prevents the next transfer from starting
    DmaRegs.CH1.CONTROL.bit.PERINTCLR = 1; 

    

    PieCtrlRegs.PIEACK.all = PIEACK_GROUP7; // Acknowledge DMA interrupt
    EDIS;
}

//
// End of File
//
