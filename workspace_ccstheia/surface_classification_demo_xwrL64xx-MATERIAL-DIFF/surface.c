/*
 * gesture.c
 *
 */
/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <control/mmwave/mmwave.h>
#include "surface.h"
#include "tvmgen_default.h"

extern MmwDemo_MSS_MCB gMmwMssMCB;
extern uint32_t Cycleprofiler_getTimeStamp(void);
extern uint32_t gpioBaseAddrLed, pinNumLed;

float inputVectorFloat[NUM_RANGEBINS_TO_ENQUEUE * NUM_FRAMES_OF_DATA];
float inputVectorFloatTemp[NUM_RANGEBINS_TO_ENQUEUE * NUM_FRAMES_OF_DATA];

float input[NUM_FRAMES_OF_DATA * NUM_RANGEBINS_TO_ENQUEUE] = {0.0};
float output = -1.0;

struct tvmgen_default_inputs inputs = { (void *) &input };
struct tvmgen_default_outputs outputs = { (void *) &output };

//deb_gp
#define FRAME_REF_TIMER_CLOCK_MHZ  40

/**
 *  @b Description
 *  @n
 *      Build feature vector used for nn model inference.
 */
void CreateFeatureVector()
{
    uint32_t i = 0;
    uint32_t *detMatrixData = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;

    // Open up the head of the circular buffer for the new frame of data.
    for (i = 0; i < NUM_FRAMES_OF_DATA * (NUM_RANGEBINS_TO_ENQUEUE - 1); i++) {
        inputVectorFloatTemp[i] = inputVectorFloatTemp[i + NUM_RANGEBINS_TO_ENQUEUE];
    }

    // Now add in the latest frame of data
    for (i = 0; i < NUM_RANGEBINS_TO_ENQUEUE; i++) {
        inputVectorFloatTemp[NUM_FRAMES_OF_DATA * NUM_RANGEBINS_TO_ENQUEUE - NUM_RANGEBINS_TO_ENQUEUE + i] = (float) detMatrixData[i];
    }

    // Now interleave the frames
    uint32_t j = 0;
    uint32_t k = 0;
    for (i = 0; i < NUM_FRAMES_OF_DATA * NUM_RANGEBINS_TO_ENQUEUE; i++) {
        inputVectorFloat[i] = inputVectorFloatTemp[k * NUM_RANGEBINS_TO_ENQUEUE + j];
        if (k == NUM_FRAMES_OF_DATA-1) {
            k = 0;
            j++;
        }
        else
            k++;
    }
    // copy to pointer for input into model
    memcpy(inputs.input_1, inputVectorFloat, sizeof(inputVectorFloat));
}

//deb_gp
InferenceResult Inference()
{
#ifdef benchTime
    int end_time, start_time;
    start_time = Cycleprofiler_getTimeStamp();
#endif

    CreateFeatureVector();                      // create the formatted input for the model

#ifdef benchTime
    end_time = Cycleprofiler_getTimeStamp();
    // benchmark the model calculation. approx 600uS for model execution
    DebugP_log("CreateFeatureVector(us): %.2f \n", (float)(end_time-start_time)/FRAME_REF_TIMER_CLOCK_MHZ);
#endif
    // call the model for inference output.
#ifdef benchTime
    GPIO_pinWriteHigh(gpioBaseAddrLed, pinNumLed);
    start_time = Cycleprofiler_getTimeStamp();
#endif

    tvmgen_default_run(&inputs, &outputs);      //run the model
    //output zeigt auf float array mit 5 Logits

    // Argmax -> zeigt Klasse mit dem höchsten Wert an
    float* out = (float*)outputs.output;
    int maxIndex = 0;
    float maxValue = out[0];
    for (int i = 1; i < 5; i++) {
        if (out[i] > maxValue) {
            maxValue = out[i];
            maxIndex = i;
        }
    }

    //maxIndex ist die Klasse mit dem höchsten Wert
    DebugP_log("MaxIndex: %d, MaxValue: %f\n", maxIndex, maxValue);

    //Softmax -> gibt die Wahrscheinlichkeit für jede Klasse zurück
    float sum = 0.0f;
    float probabilities[5];
    for (int i = 0; i < 5; i++) {
        probabilities[i] = exp(out[i]);
        sum += probabilities[i];
    }
    for (int i = 0; i < 5; i++) {
        probabilities[i] /= sum;
    }
    DebugP_log("Probabilities: %f, %f, %f, %f, %f\n", probabilities[0], probabilities[1], probabilities[2], probabilities[3], probabilities[4]);
#ifdef benchTime
    end_time = Cycleprofiler_getTimeStamp();
    GPIO_pinWriteLow(gpioBaseAddrLed, pinNumLed);
    // benchmark the model calculation. approx 600uS for model execution
    DebugP_log("Model time(us): %.2f \n\n", (float)(end_time-start_time)/FRAME_REF_TIMER_CLOCK_MHZ);
#endif
    InferenceResult result;
    result.surfaceIndex = maxIndex;
    for (int i = 0; i < 5; i++) {
        result.probabilities[i] = probabilities[i];
    }

    return result;
}
