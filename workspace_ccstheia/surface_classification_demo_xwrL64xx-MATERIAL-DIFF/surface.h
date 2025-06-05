/*
 * surface.h
 *
 */

#ifndef SURFACE_H_
#define SURFACE_H_

/* MMWAVE library Include Files */
#include <control/mmwave/mmwave.h>
#include <common/sys_common_xwrLx4xx.h>
#include <drivers/uart/v0/uart_sci.h>
#include "motion_detect.h"
#include <tvmgen_default.h>
#define NO_NN_RESULT       2.0
#define CLASS_THRESHOLD     0.5

// define the format of the range profile data that we are feeding to the model (used in CreateFeatureVector())
#define NUM_FRAMES_OF_DATA 5
#define NUM_RANGEBINS_TO_ENQUEUE 11     // The number of range bins to include as input to the neural network model
#define RANGEBIN_START 3
#define RANGEBIN_END (RANGEBIN_START + NUM_RANGEBINS_TO_ENQUEUE - 1)

// define some program control conditions
//#define benchTime   // benchmark the code.

// Define the TLV header index. 
#define MMWDEMO_OUTPUT_MSG_SURFACE_CLASSIFICATION_PROBABILITY 1031

// Define the type of classification 
#define SURFACE_TYPE_WET_DRY 		1
#define SURFACE_TYPE_GRASS_NOTGRASS 2
#define SURFACE_TYPE_MATERIAL_DIFF 	3

// Material-Diff class indices
#define MATERIAL_CLASS_IRON      0
#define MATERIAL_CLASS_ALU       1
#define MATERIAL_CLASS_GLASS     2
#define MATERIAL_CLASS_WOOD      3
#define MATERIAL_CLASS_CONCRETE  4

// Structure holds classification result to be sent
typedef struct MmwDemo_output_message_surface_t
{
    /*! @brief   Surface classification type */
    uint32_t surfaceType;

    /*! @brief   Surface classification result (Closer to 1 indicates higher confidence in WET) */
    int classificationResult;
} MmwDemo_output_message_surface;

// Prototypes
void CreateFeatureVector();	// formulating the model input.
/**
 * @brief Runs the neural network inference and returns the predicted material class index.
 * @return int Predicted class index (0=iron, 1=alu, 2=glass, 3=wood, 4=concrete)
 */
//int Inference();			// main model function
typedef struct {
    int surfaceIndex;
    float probabilities[5];
} InferenceResult;

InferenceResult Inference(void);
#endif /* SURFACE_H_ */
