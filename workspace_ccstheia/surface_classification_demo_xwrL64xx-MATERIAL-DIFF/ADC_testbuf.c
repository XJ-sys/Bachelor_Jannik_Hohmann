/*
 * Copyright (C) 2022-24 Texas Instruments Incorporated
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
/* MCU Plus Include Files. */
#include <kernel/dpl/SemaphoreP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/AddrTranslateP.h>
#include "FreeRTOS.h"
#include "task.h"
/* mmwave SDK files */
#include <control/mmwave/mmwave.h>
#include "source/mmw_cli.h"
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "ti_board_config.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <utils/mathutils/mathutils.h>

#include "source/motion_detect.h"
#include "ADC_testbuf.h"
#include "source/dpc/dpc.h"

#define MAX_NUM_TX_ANTENNA          (2U)
#define MAX_NUM_RANGEBIN            (64U)
#define MAX_NUM_CHIRPS_PERFRAME     (64U)
#define READ_LINE_BUFSIZE   256
//Uncomment this for Low power mode verification - bit-matching with uninterrupted power mode
//#define LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION 

extern MmwDemo_MSS_MCB gMmwMssMCB;

int16_t *gPreStoredAdcTestBuff;
int32_t gPreStoredAdcTestBuffInd = 0;
int32_t gPreStoredAdcTestBuffRdInd = 0;

typedef struct rangeProcTestConfig_t_ {
    uint32_t numTxAntennas;
    uint32_t numRxAntennas;
    uint32_t numVirtualAntennas;
    uint32_t numAdcSamples;
    uint32_t numRangeBins;
    uint32_t numChirpsPerFrame;
    uint32_t numChirpsPerFrameRef;
    uint32_t numFrames;
} rangeProcTestConfig_t;

uint32_t  localRead(uint8_t * adcTestBuff, uint32_t sizeOfSamp,  uint32_t numSamp)
{
    memcpy((uint8_t *)adcTestBuff, (uint8_t *)&gPreStoredAdcTestBuff[gPreStoredAdcTestBuffInd], sizeOfSamp * numSamp);
    gPreStoredAdcTestBuffInd += numSamp;
    return numSamp;
}

#if defined(LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION)

uint32_t *gStoredHeatMap;
uint32_t gStoredHeatMapInd = 0;

void localWrite(uint32_t *detMatrixData, uint32_t sizeOfSamp, uint32_t numSamp, FILE *fileIdDetMatData)
{
    memcpy((uint8_t *)&gStoredHeatMap[gStoredHeatMapInd], (uint8_t *) detMatrixData, sizeOfSamp * numSamp);
    gStoredHeatMapInd += numSamp;
}
#endif

#if (CLI_REMOVAL == 0)
/**
*  @b Description
*  @n
*        Function to read ADC data from file. For testing purpose only.
*        When FeatureLiteBuild is enabled offline adc data injection cannot be used.
*/
void mmwDemo_adcFileReadTask(){

    uint32_t baseAddr, regionId, numAdcSamplesPerEvt, numReadSamples;
    int32_t  errorCode = 0;
    int32_t status = SystemP_SUCCESS;
    uint16_t frameCnt, i, j;
    bool endOfFile = false;
    FILE * fileIdAdcData;
    FILE * fileIdDetMatData;

    //FILE * fileIdDetMatDataRef; 

    FILE * fileIdPointCloudIndData;
    rangeProcTestConfig_t testConfig;
    char fileOutName[DPC_ADC_FILENAME_MAX_LEN];
    char *ptr;
    uint8_t testCfgStr[READ_LINE_BUFSIZE];
    uint32_t *detMatrixData;
#ifndef LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION
    DPC_ObjectDetection_ExecuteResult *result = &gMmwMssMCB.dpcResult;
#endif
    if (gMmwMssMCB.enableMajorMotion)
    {
        detMatrixData = (uint32_t *) gMmwMssMCB.detMatrix[0].data;
    }
    else
    {
        detMatrixData = (uint32_t *) gMmwMssMCB.detMatrix[1].data;
    }

    baseAddr = EDMA_getBaseAddr(gEdmaHandle[0]);
    DebugP_assert(baseAddr != 0);

    regionId = EDMA_getRegionId(gEdmaHandle[0]);
    DebugP_assert(regionId < SOC_EDMA_NUM_REGIONS);

    /* start the test */
    if (gMmwMssMCB.adcDataSourceCfg.source == 1)
    {
        fileIdAdcData = fopen(gMmwMssMCB.adcDataSourceCfg.fileName, "rb");
        if (fileIdAdcData == NULL)
        {
            printf("Error:  Cannot open ADC file !\n");
            exit(0);
        }
/*For verifying CFAR*/
#if 0

    /* Open output file for detection matrix target */
    strcpy(fileOutName, gMmwMssMCB.adcDataSourceCfg.fileName);
    ptr = strrchr(fileOutName, '/');
    if (ptr == NULL)
    {
        strcpy(fileOutName, "detMatRef.bin");
    }
    else
    {
        strcpy(&ptr[1], "detMatRef.bin");
    }
    fileIdDetMatDataRef = fopen(fileOutName, "rb");
#endif


        /* Open output file for detection matrix target */
        strcpy(fileOutName, gMmwMssMCB.adcDataSourceCfg.fileName);
        ptr = strrchr(fileOutName, '/');
        if (ptr == NULL)
        {
            strcpy(fileOutName, "detMatTarget.bin");
        }
        else
        {
            strcpy(&ptr[1], "detMatTarget.bin");
        }
        fileIdDetMatData = fopen(fileOutName, "wb");
        if (fileIdDetMatData == NULL)
        {
            printf("Error:  Cannot open Detection Matrix file !\n");
            exit(0);
        }

        /* Open output file for point cloud list: range/azimuth/elevation/Doppler indices  */
        strcpy(fileOutName, gMmwMssMCB.adcDataSourceCfg.fileName);
        ptr = strrchr(fileOutName, '/');
        if (ptr == NULL)
        {
            strcpy(fileOutName, "pcloudIndTarget.bin");
        }
        else
        {
            strcpy(&ptr[1], "pcloudIndTarget.bin");
        }
        fileIdPointCloudIndData = fopen(fileOutName, "wb");
        if (fileIdPointCloudIndData == NULL)
        {
            printf("Error:  Cannot open Point Cloud file !\n");
            exit(0);
        }
    }


    /* read in test config */
    testConfig.numRxAntennas = gMmwMssMCB.numRxAntennas;

    if (gMmwMssMCB.adcDataSourceCfg.source == 1)
    {
        fread(&testConfig.numAdcSamples, sizeof(uint32_t),1,fileIdAdcData);
        fread(&testConfig.numVirtualAntennas, sizeof(uint32_t),1,fileIdAdcData);
        fread(&testConfig.numChirpsPerFrame, sizeof(uint32_t),1,fileIdAdcData);
        fread(&testConfig.numFrames, sizeof(uint32_t),1,fileIdAdcData);
    }

    else
    {
        memset ((void *)&testCfgStr[0], 0, sizeof(testCfgStr));
        status = CLI_readLine(gUartHandle[0], (char*)&testCfgStr[0], READ_LINE_BUFSIZE);
        if(status != SystemP_SUCCESS)
        {
            CLI_write("Error reading input config\n");
            DebugP_assert(0);
        }
        testConfig.numAdcSamples = atoi ((char*)&testCfgStr[0]);
        
        memset ((void *)&testCfgStr[0], 0, sizeof(testCfgStr));
        status = CLI_readLine(gUartHandle[0], (char*)&testCfgStr[0], READ_LINE_BUFSIZE);
        if(status != SystemP_SUCCESS)
        {
            CLI_write("Error reading input config\n");
            DebugP_assert(0);
        }
        testConfig.numVirtualAntennas = atoi ((char*)&testCfgStr[0]);
        
        memset ((void *)&testCfgStr[0], 0, sizeof(testCfgStr));
        status = CLI_readLine(gUartHandle[0], (char*)&testCfgStr[0], READ_LINE_BUFSIZE);
        if(status != SystemP_SUCCESS)
        {
            CLI_write("Error reading input config\n");
            DebugP_assert(0);
        }
        testConfig.numChirpsPerFrame = atoi ((char*)&testCfgStr[0]);

        memset ((void *)&testCfgStr[0], 0, sizeof(testCfgStr));
        status = CLI_readLine(gUartHandle[0], (char*)&testCfgStr[0], READ_LINE_BUFSIZE);
        if(status != SystemP_SUCCESS)
        {
            CLI_write("Error reading input config\n");
            DebugP_assert(0);
        }
        testConfig.numFrames = atoi ((char*)&testCfgStr[0]);

    }

    if ((testConfig.numAdcSamples >= 2U) && (testConfig.numAdcSamples <= 2048U))
    {
        testConfig.numRangeBins = mathUtils_pow2roundup(testConfig.numAdcSamples)/2; //real only input
    }
    else
    {
        CLI_write("Error: Wrong test configurations \n");
        DebugP_log("numAdcSamples = %d\n", testConfig.numAdcSamples);
        exit(0);
    }
    
    testConfig.numTxAntennas = testConfig.numVirtualAntennas/testConfig.numRxAntennas;
    testConfig.numChirpsPerFrameRef = testConfig.numChirpsPerFrame;

    numAdcSamplesPerEvt = (testConfig.numAdcSamples * testConfig.numRxAntennas);

    if ((testConfig.numTxAntennas > MAX_NUM_TX_ANTENNA) || (testConfig.numRangeBins > MAX_NUM_RANGEBIN) || (testConfig.numChirpsPerFrame > MAX_NUM_CHIRPS_PERFRAME))
    {
        CLI_write("Error: Wrong test configurations \n");
        exit(0);
    }

    if ((testConfig.numFrames > 0) && (testConfig.numFrames < 65536))
    {
        //ToDo check that 4 params from ADC file match CLI configuration
        DebugP_log("numTxAntennas = %d\r", testConfig.numTxAntennas);
        DebugP_log("numRangeBins = %d\r", testConfig.numRangeBins);
        DebugP_log("numChirpsPerFrame = %d\n", testConfig.numChirpsPerFrame);
        DebugP_log("numFrames = %d\n", testConfig.numFrames);
    }
    else
    {
        CLI_write("Error: Wrong test configurations \n");
        DebugP_log("numFrames = %d\n", testConfig.numFrames);
        exit(0);
    }

#ifdef LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION
    testConfig.numFrames = 8; //Do only 8 frames
    gPreStoredAdcTestBuff  = (int16_t *) DPC_ObjDet_MemPoolAlloc(&gMmwMssMCB.L3RamObj,
                                                                 testConfig.numAdcSamples *
                                                                 testConfig.numRxAntennas *
                                                                 testConfig.numChirpsPerFrame *
                                                                 testConfig.numTxAntennas *
                                                                 testConfig.numFrames *
                                                                 sizeof(uint16_t),
                                                                 sizeof(uint16_t));
    gStoredHeatMap   = (uint32_t *) DPC_ObjDet_MemPoolAlloc(&gMmwMssMCB.L3RamObj,
                                                           testConfig.numRangeBins *
                                                           gMmwMssMCB.sigProcChainCfg.azimuthFftSize *
                                                           testConfig.numFrames *
                                                           sizeof(uint32_t),
                                                           sizeof(uint32_t));
    numReadSamples = testConfig.numAdcSamples * testConfig.numRxAntennas * testConfig.numChirpsPerFrame * testConfig.numTxAntennas * testConfig.numFrames * sizeof(uint16_t);

    if (gPreStoredAdcTestBuff == NULL)
    {
        printf("Error: Memory not allocated to store test ADC data !\n");
        exit(0);
    }
    else
    {
        //Read numFrames into buffer:
        numReadSamples = fread(gPreStoredAdcTestBuff, sizeof(uint16_t), numReadSamples, fileIdAdcData);
    }

#else
    if (gMmwMssMCB.adcDataSourceCfg.source == 2)
    {
        char localAdcTestBuff[READ_LINE_BUFSIZE];
        testConfig.numFrames = gMmwMssMCB.frameCfg.h_NumOfFrames; 
        if((testConfig.numFrames == 0) || (testConfig.numFrames > 8)) //Do a max of 8 frames
        {
            testConfig.numFrames = 8;
        }
        numReadSamples = testConfig.numAdcSamples * testConfig.numRxAntennas * testConfig.numChirpsPerFrame * testConfig.numTxAntennas * testConfig.numFrames * sizeof(uint16_t);

        gPreStoredAdcTestBuff  = (int16_t *) DPC_ObjDet_MemPoolAlloc(&gMmwMssMCB.L3RamObj,
                                                                    testConfig.numAdcSamples *
                                                                    testConfig.numRxAntennas *
                                                                    testConfig.numChirpsPerFrame *
                                                                    testConfig.numTxAntennas *
                                                                    testConfig.numFrames *
                                                                    sizeof(uint16_t),
                                                                    sizeof(uint16_t));

        if (gPreStoredAdcTestBuff == NULL)
        {
            printf("Error: Memory not allocated to store test ADC data !\n");
            exit(0);
        }

        //Read numFrames into buffer:
        //CLI_write("\n\nReading adc samples for all chirps and frames\n\n");
        for(frameCnt = 0; frameCnt < testConfig.numFrames; frameCnt++)
        {
            /* Read chirps from the file */
            for(i = 0; i < (testConfig.numChirpsPerFrame * testConfig.numTxAntennas); i++)
            {
                for (j = 0; j < numAdcSamplesPerEvt; j++)
                {
                    status = CLI_readLine(gUartHandle[0], (char*)localAdcTestBuff, READ_LINE_BUFSIZE);
                    if(status != SystemP_SUCCESS)
                    {
                        CLI_write("Error reading input data\n");
                        DebugP_assert(0);
                    }
                    gPreStoredAdcTestBuff[gPreStoredAdcTestBuffRdInd] = (int16_t) atoi ((char*)localAdcTestBuff);
                    gPreStoredAdcTestBuffRdInd++;
                }
            }
        }
    }
#endif

    for(frameCnt = 0; frameCnt < testConfig.numFrames; frameCnt++)
    {
/*To test cfar dpu*/
#if 0
        if(frameCnt >=3)
        {
            fread(detMatrixData, sizeof(uint32_t), 32*16, fileIdDetMatDataRef);
        }
#endif
        /* Read chirps from the file */
        for(i = 0; i < (testConfig.numChirpsPerFrame * testConfig.numTxAntennas); i++)
        {
            if (!endOfFile && gMmwMssMCB.adcDataSourceCfg.source != 0)
            {
                /* Read one chirp of ADC samples and to put data in ADC test buffer */
#ifndef LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION
                if (gMmwMssMCB.adcDataSourceCfg.source == 1)
                {
                numReadSamples = fread(gMmwMssMCB.adcTestBuff, sizeof(uint16_t),  numAdcSamplesPerEvt, fileIdAdcData);
                }
                else
                {
                    numReadSamples = localRead(gMmwMssMCB.adcTestBuff, sizeof(uint16_t),  numAdcSamplesPerEvt);
                }   

#else
                numReadSamples = localRead(gMmwMssMCB.adcTestBuff, sizeof(uint16_t),  numAdcSamplesPerEvt);
#endif
                if (numReadSamples != numAdcSamplesPerEvt)
                {
                    endOfFile = true;
                }
            }

            /* Manual trigger to simulate chirp avail irq */
            errorCode = EDMAEnableTransferRegion(
                            baseAddr, regionId, EDMA_APPSS_TPCC_B_EVT_CHIRP_AVAIL_IRQ, EDMA_TRIG_MODE_MANUAL); //EDMA_TRIG_MODE_EVENT
            if (errorCode != 1)
            {
                CLI_write("Error: EDMA start Transfer returned %d\n",errorCode);
                return;
            }

            if (gMmwMssMCB.adcDataSourceCfg.source == 1)
            {
            ClockP_usleep(1000); //1ms sleep
            }

        } /* end of chirp loop */
        SemaphoreP_pend(&gMmwMssMCB.adcFileTaskSemHandle, SystemP_WAIT_FOREVER);

#ifndef LOW_POWER_DEEP_SLEEP_MODE_VERIFICATION
        
        if (gMmwMssMCB.adcDataSourceCfg.source == 1)
        {
            /* Write out Detection Matrix */
            fwrite(detMatrixData, sizeof(uint32_t), gMmwMssMCB.sigProcChainCfg.azimuthFftSize * testConfig.numRangeBins, fileIdDetMatData);

            //Write out point cloud
            fwrite(&frameCnt, sizeof(uint16_t), 1, fileIdPointCloudIndData);
            fwrite(&result->numObjOut, sizeof(uint16_t), 1, fileIdPointCloudIndData);
            if(result->numObjOut > 0)
            {
                int objInd;
                for (objInd = 0; objInd < result->numObjOut; objInd++)
                {
                    fwrite(&gMmwMssMCB.dpcObjIndOut[objInd], sizeof(DPIF_PointCloudRngAzimElevDopInd), 1, fileIdPointCloudIndData);
                    fwrite(&gMmwMssMCB.cfarDetObjOut[objInd].snr, sizeof(uint16_t), 1, fileIdPointCloudIndData);
                    fwrite(&gMmwMssMCB.cfarDetObjOut[objInd].noise, sizeof(uint16_t), 1, fileIdPointCloudIndData);
                }
            }
        }

        printf("ADC file read task: Processed frame number %d\n", frameCnt);
#else
        if (gMmwMssMCB.adcDataSourceCfg.source == 1)
        {
            /* Write out Detection Matrix to L3 memory*/
            localWrite(detMatrixData, sizeof(uint32_t), gMmwMssMCB.sigProcChainCfg.azimuthFftSize * testConfig.numRangeBins, fileIdDetMatData);
        }
#endif


    } /* end of frame loop */

    if (gMmwMssMCB.adcDataSourceCfg.source == 1)
    {
        fclose(fileIdAdcData);
        fclose(fileIdDetMatData);
        fclose(fileIdPointCloudIndData);
    }

    /* check the result */
    DebugP_log("Test finished!\n\r");
    DebugP_log("\n... DPC Finished, Check Output data ....  : \n\n");

}
#endif