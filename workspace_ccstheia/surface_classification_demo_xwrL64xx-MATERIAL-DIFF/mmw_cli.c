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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <kernel/dpl/DebugP.h>
#include "FreeRTOS.h"
#include "task.h"

#include "mmw_cli.h"
#include <drivers/uart.h>
#include <drivers/prcm.h>
#include <drivers/pinmux.h>
#include <utils/mathutils/mathutils.h>
#include <common/syscommon.h>
#include "motion_detect.h"
#include <datapath/dpif/dpif_adcdata.h>
#include "mmw_res.h"
#include <datapath/dpu/cfarproc/v0/cfarprochwa.h>
#include <mmwavelink/include/rl_device.h>
#include "utils/tracker_utils.h"
#include <drivers/mcspi/v0/dma/mcspi_dma.h>
#include <drivers/mcspi/v0/dma/edma/mcspi_dma_edma.h>
#include <drivers/edma.h>
#include <drivers/mcspi.h>
#include <drivers/gpio.h>

#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "ti_board_config.h"

#include "calibrations/factory_cal.h"
#include "mmwave_control/monitors.h"

//ML_start
#include "surface.h"
//ML_end

#define CLI_TASK_STACK_SIZE  (4 *1024U)
#define CLI_CFAR_THRESHOLD_ENCODING_FACTOR (100.0)

/* Demo Flash Address offset on 1MB */
#define MMWDEMO_CALIB_FLASH_ADDR_1MB  (uint32_t)(0x100000U)

#define READ_LINE_BUFSIZE   256

#if (CLI_REMOVAL == 0)
//Enable this define, to allow dynamic swith to 1.25Mbps baud rate, then execute CLI cmd: baudRate 1250000
#define ENABLE_UART_HIGH_BAUD_RATE_DYNAMIC_CFG

/* Register setting to hold the FEC core in reset */
#define FEC_CORE_HALT 0x207ff
/* Mask for Setting Boot Vector to 0 */
#define PCR2_BOOTVEC_CLR 0xFE000000

/* Device string used to print in the CLI banner */
#if defined (SOC_XWRL64XX)
#define DEVICE_STRING "xWRL6432"
#else
#define DEVICE_STRING "xWRL1432"
#endif

extern uint32_t gpioBaseAddrLed;
extern uint32_t pinNumLed;
extern uint32_t sensorStop;

#if (CLI_REMOVAL == 0 && DYNAMIC_RECONFIG == 1)
extern uint8_t gUserCfgPresenceTrack;
extern char* trackCmdString[];
extern char* presenceCmdString[];
#ifdef SOC_XWRL64XX
extern char* trackCmdStringAOP[];
extern char* presenceCmdStringAOP[];
#endif
#endif


TaskHandle_t gCliTask;
StaticTask_t gCliTaskObj;
StackType_t  gCliTskStack[CLI_TASK_STACK_SIZE] __attribute__((aligned(32)));

TaskHandle_t gAdcFileTask;
StaticTask_t gAdcFileTaskObj;
StackType_t gAdcFileTaskStack[ADC_FILEREAD_TASK_STACK_SIZE] __attribute__((aligned(32)));

CLI_MCB     gCLI;
#endif

/*Standard ADC Sampling Rates*/
#define NUM_VALID_SAMP_RATE         (11U)
/*The following values are DigOutputSampRate values corresponding to ADC sampling rates in MSPS: {1.0, 1.25, 1.667, 2.0, 2.5, 4.0, 5.0, 6.667, 7.692, 10.0, 12.5}*/
const uint8_t ValidDigSampRates[NUM_VALID_SAMP_RATE] = {100,80,60,50,40,25,20,15,13,10,8};

float       gSocClk = 40000000; //Hz

TaskHandle_t gDpcTask;
StaticTask_t gDpcTaskObj;
StackType_t  gDpcTaskStack[DPC_TASK_STACK_SIZE] __attribute__((aligned(32)));

TaskHandle_t gTlvTask;
StaticTask_t gTlvTaskObj;
StackType_t gTlvTaskStack[TLV_TASK_STACK_SIZE] __attribute__((aligned(32)));

extern MmwDemo_MSS_MCB gMmwMssMCB;

// Indicate if Sensor is Started or not
uint8_t isSensorStarted = 0;

volatile unsigned long long demoStartTime = 0;
//cfar threshold in hwa = (cli_val * 100)/16

#if (SPI_ADC_DATA_STREAMING==1)
    /************ ADC data streaming via SPI **********************************************************************************/
    /* In Sysconfig, by default I2C is configured. To enable Raw ADC Data via SPI feature, manual configuration of MCSPI is required */

    /* MCSPI Driver handles */
    extern MCSPI_Handle gMcspiHandle[CONFIG_MCSPI_NUM_INSTANCES];

    /*
    * MCSPI Driver Advance Parameters - to be used only when Driver_open() and
    * Driver_close() is not used by the application
    */
    /* MCSPI Driver Open Parameters */
    MCSPI_OpenParams gMcspiOpenParams[CONFIG_MCSPI_NUM_INSTANCES];
    /* MCSPI objects - initialized by the driver */
    static MCSPI_Object gMcspiObjects[CONFIG_MCSPI_NUM_INSTANCES];
    /* MCSPI Driver Channel Configurations */
    MCSPI_ChConfig gConfigMcspi0ChCfg[CONFIG_MCSPI0_NUM_CH];
    MCSPI_DmaChConfig gConfigMcspi0DmaChCfg[CONFIG_MCSPI0_NUM_CH];
    uint32_t gMcspiConfigNum = CONFIG_MCSPI_NUM_INSTANCES;
    /* Buffer to store Raw ADC Data per frame */
    uint8_t adcbuffer[ADC_DATA_BUFF_MAX_SIZE] = {0};
    /* Number of bytes in every frame */
    uint32_t adcDataPerFrame;

    /* MCSPI Driver open/close - can be used by application when Driver_open() and
    * Driver_close() is not used directly and app wants to control the various driver
    * open/close sequences */
    void Drivers_mcspiOpen(void);
    void Drivers_mcspiClose(void);

    /* MCSPI Driver handles */
    MCSPI_Handle gMcspiHandle[CONFIG_MCSPI_NUM_INSTANCES];
    uint32_t gMcspiDmaConfigNum = CONFIG_MCSPI_NUM_DMA_INSTANCES;
    /* MCSPI Driver Open Parameters */
    MCSPI_OpenParams gMcspiOpenParams[CONFIG_MCSPI_NUM_INSTANCES] =
    {
        {
            .transferMode           = MCSPI_TRANSFER_MODE_BLOCKING,
            .transferTimeout        = SystemP_WAIT_FOREVER,
            .transferCallbackFxn    = NULL,
            .loopback               = MCSPI_LOOPBACK_DISABLE,
            .msMode                 = MCSPI_MS_MODE_PERIPHERAL,
            .mcspiDmaIndex = 0,
        },
    };
    /* MCSPI Driver Channel Configurations */
    MCSPI_ChConfig gConfigMcspi0ChCfg[CONFIG_MCSPI0_NUM_CH] =
    {
        {
            .chNum              = MCSPI_CHANNEL_0,
            .frameFormat        = MCSPI_FF_POL0_PHA0,
            .bitRate            = 15000000,
            .csPolarity         = MCSPI_CS_POL_LOW,
            .trMode             = MCSPI_TR_MODE_TX_RX,
            .inputSelect        = MCSPI_IS_D1,
            .dpe0               = MCSPI_DPE_ENABLE,
            .dpe1               = MCSPI_DPE_DISABLE,
            .slvCsSelect        = MCSPI_SLV_CS_SELECT_0,
            .startBitEnable     = FALSE,
            .startBitPolarity   = MCSPI_SB_POL_LOW,
            .csIdleTime         = MCSPI_TCS0_0_CLK,
            .defaultTxData      = 0x0U,
        },
    };

    MCSPI_DmaChConfig gConfigMcspi0DmaChCfg[CONFIG_MCSPI0_NUM_CH] =
    {
        {
            .edmaRxChId = 0,
            .edmaTxChId = 1,
        }
    };

    /* MCSPI atrributes */
    static MCSPI_Attrs gMcspiAttrs[CONFIG_MCSPI_NUM_INSTANCES] =
    {
        {
            .baseAddr           = CSL_MCU_MCSPI0_CFG_BASE,
            .inputClkFreq       = 40000000U,
            .intrNum            = 30,
            .operMode           = MCSPI_OPER_MODE_DMA,
            .intrPriority       = 4U,
            .chMode             = MCSPI_CH_MODE_SINGLE,
            .pinMode            = MCSPI_PINMODE_4PIN,
            .initDelay          = MCSPI_INITDLY_0,
        },
    };

    /* MCSPI driver configuration */
    MCSPI_Config gMcspiConfig[CONFIG_MCSPI_NUM_INSTANCES] =
    {
        {
            &gMcspiAttrs[CONFIG_MCSPI0],
            &gMcspiObjects[CONFIG_MCSPI0],
        },
    };

    McspiDma_EdmaArgs gMcspiEdmaArgs =
    {
        .drvHandle        = (void *)&gEdmaConfig[CONFIG_EDMA1],
    };

    MCSPI_DmaConfig gMcspiDmaConfig =
    {
        .fxns        = &gMcspiDmaEdmaFxns,
        .mcspiDmaArgs = (void *)&gMcspiEdmaArgs,
    };

    /* Pad Configuration for McSPI pins */
    static Pinmux_PerCfg_t gPinMuxMainDomainCfgSpi[] = {
                /* MCSPIA pin config */
        /* MCSPIA_CLK -> PAD_AG (D10) */
        {
            PIN_PAD_AG,
            ( PIN_MODE(0) | PIN_PULL_DISABLE )
        },
        /* MCSPIA_MOSI -> PAD_AI (B12) */
        {
            PIN_PAD_AI,
            ( PIN_MODE(0) | PIN_PULL_DISABLE )
        },
        /* MCSPIA_MISO -> PAD_AJ (C11) */
        {
            PIN_PAD_AJ,
            ( PIN_MODE(0) | PIN_PULL_DISABLE )
        },

                /* MCSPIA_CS0 pin config */
        /* MCSPIA_CS0 -> PAD_AH (D11) */
        {
            PIN_PAD_AH,
            ( PIN_MODE(0) | PIN_PULL_DISABLE )
        },

        {PINMUX_END, PINMUX_END}
    };

    void Drivers_mcspiOpen(void)
    {
        uint32_t instCnt, chCnt;
        int32_t  status = SystemP_SUCCESS;

        for(instCnt = 0U; instCnt < CONFIG_MCSPI_NUM_INSTANCES; instCnt++)
        {
            gMcspiHandle[instCnt] = NULL;   /* Init to NULL so that we can exit gracefully */
        }

        /* Open all instances */
        for(instCnt = 0U; instCnt < CONFIG_MCSPI_NUM_INSTANCES; instCnt++)
        {
            gMcspiHandle[instCnt] = MCSPI_open(instCnt, &gMcspiOpenParams[instCnt]);
            if(NULL == gMcspiHandle[instCnt])
            {
                DebugP_logError("MCSPI open failed for instance %d !!!\r\n", instCnt);
                status = SystemP_FAILURE;
                break;
            }
        }

        /* Channel configuration */
        for(chCnt = 0U; chCnt < CONFIG_MCSPI0_NUM_CH; chCnt++)
        {
            status = MCSPI_chConfig(
                        gMcspiHandle[CONFIG_MCSPI0],
                        &gConfigMcspi0ChCfg[chCnt]);
            if(status != SystemP_SUCCESS)
            {
                DebugP_logError("CONFIG_MCSPI0 channel %d config failed !!!\r\n", chCnt);
                break;
            }
            status = MCSPI_dmaChConfig(
                        gMcspiHandle[CONFIG_MCSPI0],
                        &gConfigMcspi0ChCfg[chCnt],
                        &gConfigMcspi0DmaChCfg[chCnt]);
            if(status != SystemP_SUCCESS)
            {
                DebugP_logError("CONFIG_MCSPI0 DMA channel %d config failed !!!\r\n", chCnt);
                break;
            }
        }

        if(SystemP_FAILURE == status)
        {
            Drivers_mcspiClose();   /* Exit gracefully */
        }

        return;
    }

    void Drivers_mcspiClose(void)
    {
        uint32_t instCnt;
        int32_t status, chCnt;
        for(chCnt = 0U; chCnt < CONFIG_MCSPI0_NUM_CH; chCnt++)
        {
            status = MCSPI_dmaClose(gMcspiHandle[CONFIG_MCSPI0],
                                    &gConfigMcspi0ChCfg[chCnt]);
            if(status != SystemP_SUCCESS)
            {
                DebugP_logError("CONFIG_MCSPI0 DMA close %d failed !!!\r\n", chCnt);
                break;
            }
        }
        /* Close all instances that are open */
        for(instCnt = 0U; instCnt < CONFIG_MCSPI_NUM_INSTANCES; instCnt++)
        {
            if(gMcspiHandle[instCnt] != NULL)
            {
                MCSPI_close(gMcspiHandle[instCnt]);
                gMcspiHandle[instCnt] = NULL;
            }
        }

        return;
    }

    void Module_clockEnable_spi()
    {
        int32_t status;
        SOC_clocksEnable();
        status = SOC_moduleClockEnable(SOC_RcmPeripheralId_APPSS_MCSPIA, 0);
        DebugP_assertNoLog(status == SystemP_SUCCESS);
    }



    void Module_clockSetFrequency_spi()
    {
        int32_t status;
    


        
            status = SOC_moduleSetClockFrequency(
                        SOC_RcmPeripheralId_APPSS_MCSPIA,
                        SOC_RcmPeripheralClockSource_OSC_CLK,
                        40000000
                        );
            DebugP_assertNoLog(status == SystemP_SUCCESS);
        
    }


    void clock_init()
    {   
        
        Module_clockEnable_spi();
        Module_clockSetFrequency_spi();
    }

#endif

#if (CLI_REMOVAL == 0)
static Pinmux_PerCfg_t gPinMuxMDOCfg[] = {

            /* MDO0 pin config */
    /* MDO_CLK -> PAD_AW (L11) */
    {
        PIN_PAD_AW,
        ( PIN_MODE(7) | PIN_PULL_DISABLE )
    },
    /* MDO_FRMCLK -> PAD_AX (M10) */
    {
        PIN_PAD_AX,
        ( PIN_MODE(9) | PIN_PULL_DISABLE )
    },
    /* MDO_D0 -> PAD_AL (H10) */
    {
        PIN_PAD_AL,
        ( PIN_MODE(7) | PIN_PULL_DISABLE )
    },
    /* MDO_D1 -> PAD_AM (J11) */
    {
        PIN_PAD_AM,
        ( PIN_MODE(7) | PIN_PULL_DISABLE )
    },
    /* MDO_D2 -> PAD_AN (L12) */
    {
        PIN_PAD_AN,
        ( PIN_MODE(7) | PIN_PULL_DISABLE )
    },
    /* MDO_D3 -> PAD_AU (K11) */
    {
        PIN_PAD_AU,
        ( PIN_MODE(7) | PIN_PULL_DISABLE )
    },

    {PINMUX_END, PINMUX_END}
};

//ML_start
char* radarCmdString[MAX_RADAR_CMD] =
{
 "sensorStop 0 \r\n",
 "chirpComnCfg 33 0 0 128 4 50 0 \r\n",
 "chirpTimingCfg 6 19 0 76 60 \r\n",
 "channelCfg 7 3 0 \r\n",
 "frameCfg 2 0 228 20 250 0 \r\n",
 "guiMonitor 0 1 0 0 0 0 0 0 0 0 0 \r\n",
 "sigProcChainCfg 32 2 1 0 4 4 0 15 \r\n",
 "cfarCfg 2 8 4 3 0 10.0 0 0.5 0 1 1 1 \r\n",
 "aoaFovCfg -30 30 -40 18 \r\n",
 "rangeSelCfg 0.1 5.0 \r\n",
 "clutterRemoval 1 \r\n",
 "compRangeBiasAndRxChanPhase 0.0 1.00000 0.00000 -1.00000 0.00000 1.00000 0.00000 -1.00000 0.00000 1.00000 0.00000 -1.00000 0.00000 \r\n",
 "adcDataSource 0 C:/ti/mmwave_lp_sdk/examples/datapath/common/testBench/gesture_recognition_adc_data/radar_data_6432_100framesS.bin \r\n",
 "presenceDetectCfg 0 2500 2000 \r\n",
 "adcLogging 0 \r\n",
 "lowPowerCfg 0 \r\n",
 "factoryCalibCfg 1 0 40 0 0x1ff000 \r\n",
 "mpdBoundaryBox 1 0 1.48 0 1.95 0 3 \r\n",
 "mpdBoundaryBox 2 0 1.48 1.95 3.9 0 3 \r\n",
 "mpdBoundaryBox 3 -1.48 0 0 1.95 0 3 \r\n",
 "mpdBoundaryBox 4 -1.48 0 1.95 3.9 0 3 \r\n",
 "sensorPosition 0 0 1.44 0 0 \r\n",
 "minorStateCfg 5 4 40 8 4 30 8 8 \r\n",
 "majorStateCfg 4 2 30 10 8 80 4 4 \r\n",
 "clusterCfg 1 0.5 2 \r\n",
 "sensorStart 0 0 0 0 \r\n"
};
//ML_end

#if defined (SOC_XWRL64XX)
char* radarCmdStringAOP[MAX_RADAR_CMD_AOP] =
{
"channelCfg 7 3 0 \r\n",
"chirpComnCfg 20 0 0 128 4 30 2 \r\n",
"chirpTimingCfg 6.00 28 0.00 90 59.75 \r\n",
"frameCfg 8 0 403 1 250.0 0 \r\n",
"guiMonitor 0 0 0 0 0 0 1 0 0 0 0 1 \r\n",
"sigProcChainCfg 64 8 2 0 4 4 0 .5 \r\n",
"cfarCfg 2 4 3 2 0 12.00 0 0.5 0 1 0 1 \r\n",
"aoaFovCfg -60.00 60.00 -40.00 40.00 \r\n",
"rangeSelCfg 0.07 0.6 \r\n",
"clutterRemoval 1 \r\n",
"antGeometryCfg 1 1 1 0 0 1 1 3 1 2 0 3 2.5 2.5 \r\n",
"compRangeBiasAndRxChanPhase 0.00 1.00 0.00 -1.00 0.00 1.00 0.00 -1.00 0.00 1.00 0.00 -1.00 0.00 \r\n",
"adcDataSource 0 adcData_1_000.bin \r\n",
"adcLogging 0 \r\n",
"lowPowerCfg 0 \r\n",
"factoryCalibCfg 1 0 38 3 0x1ff000 \r\n",
"mpdBoundaryBox 1 -0.10 0.10 0.07 0.6 -0.10 0.10 \r\n",
"sensorPosition 0.00 0.00 0.00 0.00 0.00 \r\n",
"minorStateCfg 5 4 40 8 4 30.00 4 4 \r\n",
"clusterCfg 1 0.50 2 \r\n",
"sensorStart 0 0 0 0 \r\n"
};
#endif

static int32_t CLI_help (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveExtensionHandler(int32_t argc, char* argv[]);
static void    CLI_MMWaveExtensionHelp(void);

int32_t CLI_ByPassApi(CLI_Cfg* ptrCLICfg, uint8_t profileSwitch);
static int32_t CLI_MMWaveVersion (int32_t argc, char* argv[]);
#endif

static int32_t CLI_MMWaveSensorStop (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveChannelCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveChirpCommonCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveChirpTimingCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveFrameCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveGuiMonSel(int32_t argc, char* argv[]);
static int32_t CLI_MMWaveSigProcChainCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveCfarCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveAoaCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveRangeSelCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveClutterRemoval (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveCompRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveAdcDataSourceCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveAdcLogging (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveSensorPositionCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveMpdBoundaryBox (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveClusterParamCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveMajorMotionStateCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveMinorMotionStateCfg (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveprofileSwitchCfg (int32_t argc, char* argv[]);
int32_t CLI_MMWaveSensorStart (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveFactoryCalConfig (int32_t argc, char* argv[]);
static int32_t CLI_MMWaveLowPwrModeEnable(int32_t argc, char* argv[]);
static int32_t CLI_MmwDemo_AntGeometryCfg (int32_t argc, char* argv[]);

int32_t CLI_MMWStart(void);

/**************************************************************************
 ************************** Extern Definitions ****************************
 **************************************************************************/
extern void Mmwave_populateDefaultOpenCfg (MMWave_OpenCfg* ptrOpenCfg);
extern void Mmwave_populateDefaultChirpControlCfg (MMWave_CtrlCfg* ptrCtrlCfg);
#if (ENABLE_MONITORS==1)
/*API to configure RF Monitors*/
void mmwDemo_MonitorConfig (void);
#endif

#if (CLI_REMOVAL == 0)
/**
 * @brief
 *  This is the mmWave extension table added to the CLI.
 */
CLI_CmdTableEntry gCLIMMWaveExtensionTable[] =
{
    {
        "version",
        "No arguments",
        CLI_MMWaveVersion
    },
    {
        NULL,
        NULL,
        NULL
    }
};

int32_t CLI_ByPassApi(CLI_Cfg* ptrCLICfg, uint8_t profileSwitch)
{
    //uint8_t                 cmdString[128];
    char*                   tokenizedArgs[CLI_MAX_ARGS];
    char*                   ptrCLICommand = NULL;
    char                    delimitter[] = " \r\n";
    uint32_t                argIndex;
    CLI_CmdTableEntry*      ptrCLICommandEntry;
    int32_t                 cliStatus;
    uint32_t                index, idx, maxIdx;
    uint16_t numCLICommands = 0U;

    /* Sanity Check: Validate the arguments */
    if (ptrCLICfg == NULL)
        return -1;

    /* Cycle through and determine the number of supported CLI commands: */
    for (index = 0; index < CLI_MAX_CMD; index++)
    {
        /* Do we have a valid entry? */
        if (ptrCLICfg->tableEntry[index].cmd == NULL)
        {
            /* NO: This is the last entry */
            break;
        }
        else
        {
            /* YES: Increment the number of CLI commands */
            numCLICommands = numCLICommands + 1;
        }
    }

    if(profileSwitch == 1)
    {
        maxIdx = MAX_TRACKER_CMD;
    }
    else if(profileSwitch == 2)
    {
        maxIdx = MAX_PRESENCE_CMD;
    }
    else
    {
        #ifdef SOC_XWRL64XX
        if(gMmwMssMCB.isDevAOP == 1u)
        {
            maxIdx = MAX_RADAR_CMD_AOP;
        }
        else
        {
            maxIdx = MAX_RADAR_CMD;
        }
        #endif
        #ifdef SOC_XWRL14XX
        maxIdx = MAX_RADAR_CMD;
        #endif
    }

    /* Execute All Radar Commands */
    for (idx = 0; idx < maxIdx; idx++)
    {
        /* Reset all the tokenized arguments: */
        memset ((void *)&tokenizedArgs, 0, sizeof(tokenizedArgs));
        argIndex      = 0;
        #if (CLI_REMOVAL == 0 && DYNAMIC_RECONFIG == 1)
        if(profileSwitch == 1)
        {
            #ifdef SOC_XWRL64XX
            if(gMmwMssMCB.isDevAOP == 1u)
            {
                ptrCLICommand = (char*)trackCmdStringAOP[idx];
            }
            else
            {
                ptrCLICommand = (char*)trackCmdString[idx];
            }
            #endif
            #ifdef SOC_XWRL14XX
            ptrCLICommand = (char*)trackCmdString[idx];
            #endif

        }
        else if(profileSwitch == 2)
        {
            #ifdef SOC_XWRL64XX
            if(gMmwMssMCB.isDevAOP == 1u)
            {
                ptrCLICommand = (char*)presenceCmdStringAOP[idx];
            }
            else
            {
                ptrCLICommand = (char*)presenceCmdString[idx];
            }
            #endif
            #ifdef SOC_XWRL14XX
            ptrCLICommand = (char*)presenceCmdString[idx];
            #endif
        }
        else
        {
            #ifdef SOC_XWRL64XX
            if(gMmwMssMCB.isDevAOP == 1u)
            {
                ptrCLICommand = (char*)radarCmdStringAOP[idx];
            }
            else
            {
                ptrCLICommand = (char*)radarCmdString[idx];
            }
            #endif
            #ifdef SOC_XWRL14XX
            ptrCLICommand = (char*)radarCmdString[idx];
            #endif
        }
        #else
        #ifdef SOC_XWRL64XX
            if(gMmwMssMCB.isDevAOP == 1u)
            {
                ptrCLICommand = (char*)radarCmdStringAOP[idx];
            }
            else
            {
                ptrCLICommand = (char*)radarCmdString[idx];
            }
            #endif
            #ifdef SOC_XWRL14XX
            ptrCLICommand = (char*)radarCmdString[idx];
            #endif
        #endif
        
        /* Set the CLI status: */
        cliStatus = -1;
       
        /* The command has been entered we now tokenize the command message */
        while (1)
        {
            /* Tokenize the arguments: */
            tokenizedArgs[argIndex] = strtok(ptrCLICommand, delimitter);
            if (tokenizedArgs[argIndex] == NULL)
                break;

            /* Increment the argument index: */
            argIndex++;
            if (argIndex >= CLI_MAX_ARGS)
                break;

            /* Reset the command string */
            ptrCLICommand = NULL;
        }
        
        /* Were we able to tokenize the CLI command? */
        if (argIndex == 0 || argIndex == 1)
            continue;

        /* Cycle through all the registered CLI commands: */
        for (index = 0; index < numCLICommands; index++)
        {
            ptrCLICommandEntry = &ptrCLICfg->tableEntry[index];

            /* Do we have a match? */
            if (strcmp(ptrCLICommandEntry->cmd, tokenizedArgs[0]) == 0)
            {
                /* YES: Pass this to the CLI registered function */
                cliStatus = ptrCLICommandEntry->cmdHandlerFxn (argIndex, tokenizedArgs);
                if (cliStatus != 0)
                {
                    CLI_write ("Error %d\r\n", cliStatus);
                }
                break;
            }
        }

        /* Did we get a matching CLI command? */
        if (index == numCLICommands)
        {
            /* NO matching command found. Is the mmWave extension enabled? */
            if (ptrCLICfg->enableMMWaveExtension == 1U)
            {
                /* Yes: Pass this to the mmWave extension handler */
                cliStatus = CLI_MMWaveExtensionHandler (argIndex, tokenizedArgs);
            }

            /* Was the CLI command found? */
            if (cliStatus == -1)
            {
                /* No: The command was still not found */
                CLI_write ("'%s' is not recognized as a CLI command\r\n", tokenizedArgs[0]);
            }
        }
    }
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the version command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  \ingroup CLI_UTIL_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveVersion (int32_t argc, char* argv[])
{
    int32_t       retVal = 0;
    T_RL_API_DFP_FW_VER_GET_RSP dfpVerapiResData;
    T_RL_API_SENSOR_DIEID_RSP   deviceDieId;

    if(gCLI.cfg.overridePlatform == false)
    {
#if defined (SOC_XWRL64XX)
        if(gMmwMssMCB.isDevAOP == 1u)
        {
            /* print the platform */
            CLI_write ("Platform                : XWRL6432AOP\r\n");
        }
        else
        {
            CLI_write ("Platform                : XWRL6432\r\n");
        }
#else
        /* print the platform */
        CLI_write ("Platform                : XWRL1432\r\n");
#endif
    }
    else
    {
        CLI_write ("Platform                : %s\r\n", gCLI.cfg.overridePlatformString);
    }

    /* Initialize API response Structures. */
    memset(&dfpVerapiResData,0,sizeof(T_RL_API_DFP_FW_VER_GET_RSP));
    memset(&deviceDieId,0,sizeof(T_RL_API_SENSOR_DIEID_RSP));

    /* Get the version string: */
    retVal = rl_mmWaveDfpVerGet(M_DFP_DEVICE_INDEX_0, &dfpVerapiResData);
    if (retVal < 0)
    {
        CLI_write ("Error: get DFP version [Error %d]\r\n", retVal);
        return -1;
    }

    CLI_write ("RFS Firmware Version    : %02d.%02d.%02d.%02d\r\n",
                dfpVerapiResData.z_RfsRomVersion.c_GenVerNum,
                dfpVerapiResData.z_RfsRomVersion.c_MajorVerNum,
                dfpVerapiResData.z_RfsRomVersion.c_MinorVerNum,
                dfpVerapiResData.z_RfsRomVersion.c_BuildVerNum);

    CLI_write ("FECSS Lib Version       : %02d.%02d.%02d.%02d\r\n",
                dfpVerapiResData.z_FecssLibVersion.c_GenVerNum,
                dfpVerapiResData.z_FecssLibVersion.c_MajorVerNum,
                dfpVerapiResData.z_FecssLibVersion.c_MinorVerNum,
                dfpVerapiResData.z_FecssLibVersion.c_BuildVerNum);

    CLI_write ("mmWaveLink Version      : %02d.%02d.%02d.%02d\r\n",
                dfpVerapiResData.z_MmwlLibVersion.c_GenVerNum,
                dfpVerapiResData.z_MmwlLibVersion.c_MajorVerNum,
                dfpVerapiResData.z_MmwlLibVersion.c_MinorVerNum,
                dfpVerapiResData.z_MmwlLibVersion.c_BuildVerNum);

    CLI_write ("RFS Patch Version       : %02d.%02d.%02d.%02d\r\n",
                dfpVerapiResData.z_RfsPatchVersion.c_GenVerNum,
                dfpVerapiResData.z_RfsPatchVersion.c_MajorVerNum,
                dfpVerapiResData.z_RfsPatchVersion.c_MinorVerNum,
                dfpVerapiResData.z_RfsPatchVersion.c_BuildVerNum);

    // /* Get Die ID. */
    // retVal = fe_fecssDieIdGet_rom(M_DFP_DEVICE_INDEX_0, &deviceDieId);
    // if (retVal < 0)
    // {
    //     CLI_write ("Error: get device die info [Error %d]\r\n", retVal);
    //     return -1;
    // }

    // CLI_write ("Die ID Val0 : 0x%X\r\n", deviceDieId.w_DieIdData[0]);
    // CLI_write ("Die ID Val1 : 0x%X\r\n", deviceDieId.w_DieIdData[1]);
    // CLI_write ("Die ID Val2 : 0x%X\r\n", deviceDieId.w_DieIdData[2]);
    // CLI_write ("Die ID Val3 : 0x%X\r\n", deviceDieId.w_DieIdData[3]);

    /* Display the version information on the CLI Console: */
    CLI_write ("mmWave SDK Version      : %02d.%02d.%02d.%02d\r\n",
                            MMWAVE_SDK_VERSION_MAJOR,
                            MMWAVE_SDK_VERSION_MINOR,
                            MMWAVE_SDK_VERSION_BUGFIX,
                            MMWAVE_SDK_VERSION_BUILD);
    /* Version string has been formatted successfully. */
    /* Display the Demo information on the CLI Console: */
    //ML_start
    CLI_write ("ML_Classification_Demo\r\n");
    //ML_end

    return 0;
}

static int32_t CLI_MMWaveExtensionHandler(int32_t argc, char* argv[])
{
    CLI_CmdTableEntry*  ptrCLICommandEntry;
    int32_t             cliStatus;
    int32_t             retVal = 0;

    /* Get the pointer to the mmWave extension table */
    ptrCLICommandEntry = &gCLIMMWaveExtensionTable[0];

    /* Cycle through all the registered externsion CLI commands: */
    while (ptrCLICommandEntry->cmdHandlerFxn != NULL)
    {
        /* Do we have a match? */
        if (strcmp(ptrCLICommandEntry->cmd, argv[0]) == 0)
        {
            /* YES: Pass this to the CLI registered function */
            cliStatus = ptrCLICommandEntry->cmdHandlerFxn (argc, argv);
            if (cliStatus == 0)
            {
                /* Successfully executed the CLI command: */
                CLI_write ("Done\r\n\n");
            }
            else
            {
                /* Error: The CLI command failed to execute */
                CLI_write ("Error %d\r\n", cliStatus);
            }
            break;
        }

        /* Get the next entry: */
        ptrCLICommandEntry++;
    }

    /* Was this a valid CLI command? */
    if (ptrCLICommandEntry->cmdHandlerFxn == NULL)
    {
        /* NO: The command was not a valid CLI mmWave extension command. Setup
         * the return value correctly. */
        retVal = -1;
    }
    return retVal;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Execution Task
 *
 *  \ingroup CLI_UTIL_INTERNAL_FUNCTION
 *
 *  @retval
 *      Not Applicable.
 */
static void CLI_task(void* args)
{   
    uint8_t                 cmdString[READ_LINE_BUFSIZE];
    char*                   tokenizedArgs[CLI_MAX_ARGS];
    char*                   ptrCLICommand;
    char                    delimitter[] = " \r\n";
    uint32_t                argIndex;
    CLI_CmdTableEntry*      ptrCLICommandEntry;
    int32_t                 cliStatus, status;
    uint32_t                index;

    /* Do we have a banner to be displayed? */
    if (gCLI.cfg.cliBanner != NULL)
    {
        /* YES: Display the banner */
        CLI_write (gCLI.cfg.cliBanner);
    }

    /* Loop around forever: */
    while (1)
    {
        /* Demo Prompt: */
        CLI_write (gCLI.cfg.cliPrompt);
        /* Reset the command string: */
        memset ((void *)&cmdString[0], 0, sizeof(cmdString));

//ML_start
// ***************************
// Testing the embedded model
// ***************************
        FILE * fileIdDatasetIn;
        int32_t dataSet[NUM_RANGEBINS_TO_ENQUEUE];
        int32_t dataCount=0;
        int32_t badCount=0;
        int32_t goodCount=0;
        float totalGood=0;
        InferenceResult result;

        // ************************;
        int expectedClass;
        uint fileStat;

        // ************************
        // Run the Iron dataset
        // *************************
        // TO DO: Change the path to the dataset file
        char ironDatafile[] = "C:\\github\\Bachelothesis_radar_IWRL6432Boost\\machine_learning_flow\\src\\4-CCS-Integration\\mmwave_dataset_loader\\src\\surface_classification_test_xwrL64xx-SDK5520-Material-Diff_demo\\dataSets\\CCS_class_0_data.bin";
        printf("Iron data");
        fileIdDatasetIn = fopen(ironDatafile, "rb");
        if (fileIdDatasetIn == NULL) {
            printf("Error: Cannot open DatasetIn file !\n");
            exit(0);
        }

        // read file dataset and make comparison between predicted and actual.
        do  {
            fileStat = fread(&dataSet, sizeof(uint32_t), NUM_RANGEBINS_TO_ENQUEUE, fileIdDatasetIn);
            dataCount++;                                // dataset line count.

            // replace the normal detmatrix data with the file data
            uint32_t *pdetMatrix = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;
            memcpy(pdetMatrix, &dataSet[0], sizeof(dataSet)) ;

            // Create feature vector and do inference
            expectedClass = 0;
            result = Inference();
            if (result.surfaceIndex == expectedClass)
               goodCount++;
            else
               badCount++;

            totalGood = ((float)goodCount/(float)(goodCount+badCount))*100;
            if (dataCount % 100 == 0){
                printf("%d: %d, %d, Accuracy: %.2f%%\n", dataCount, goodCount, badCount, totalGood);
                printf("  -> Probs: [%.3f %.3f %.3f %.3f %.3f], pred: %d\n",
                        result.probabilities[0], result.probabilities[1],
                        result.probabilities[2], result.probabilities[3],
                        result.probabilities[4], result.surfaceIndex);
            }   	
        } while (fileStat != 0);

        printf("Iron data complete\n");
        fclose(fileIdDatasetIn);

        // ************************
        // Run the Alu dataset
        // *************************
        // TO DO: Change the path to the dataset file
        char aluDatafile[] = "C:\\github\\Bachelothesis_radar_IWRL6432Boost\\machine_learning_flow\\src\\4-CCS-Integration\\mmwave_dataset_loader\\src\\surface_classification_test_xwrL64xx-SDK5520-Material-Diff_demo\\dataSets\\CCS_class_1_data.bin";
        printf("Alu data");
        fileIdDatasetIn = fopen(aluDatafile, "rb");
        if (fileIdDatasetIn == NULL) {
            printf("Error: Cannot open DatasetIn file !\n");
            exit(0);
        }

        // read file dataset and make comparison between predicted and actual.
        do  {
            fileStat = fread(&dataSet, sizeof(uint32_t), NUM_RANGEBINS_TO_ENQUEUE, fileIdDatasetIn);
            dataCount++;                                // dataset line count.

            // replace the normal detmatrix data with the file data
            uint32_t *pdetMatrix = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;
            memcpy(pdetMatrix, &dataSet[0], sizeof(dataSet)) ;

            // Create feature vector and do inference
            expectedClass = 1;
            result = Inference();
            if (result.surfaceIndex == expectedClass)
               goodCount++;
            else
               badCount++;

            totalGood = ((float)goodCount/(float)(goodCount+badCount))*100;
            if (dataCount % 100 == 0){
                printf("%d: %d, %d, Accuracy: %.2f%%\n", dataCount, goodCount, badCount, totalGood);
                printf("  -> Probs: [%.3f %.3f %.3f %.3f %.3f], pred: %d\n",
                    result.probabilities[0], result.probabilities[1],
                    result.probabilities[2], result.probabilities[3],
                    result.probabilities[4], result.surfaceIndex);
            }
        } while (fileStat != 0);

        printf("Alu data complete\n");
        fclose(fileIdDatasetIn);
        // ************************
        // Run the Glass dataset
        // *************************
        // TO DO: Change the path to the dataset file
        char glassDatafile[] = "C:\\github\\Bachelothesis_radar_IWRL6432Boost\\machine_learning_flow\\src\\4-CCS-Integration\\mmwave_dataset_loader\\src\\surface_classification_test_xwrL64xx-SDK5520-Material-Diff_demo\\dataSets\\CCS_class_2_data.bin";
        printf("Glass data");
        fileIdDatasetIn = fopen(glassDatafile, "rb");
        if (fileIdDatasetIn == NULL) {
            printf("Error: Cannot open DatasetIn file !\n");
            exit(0);
        }

        // read file dataset and make comparison between predicted and actual.
        do  {
            fileStat = fread(&dataSet, sizeof(uint32_t), NUM_RANGEBINS_TO_ENQUEUE, fileIdDatasetIn);
            dataCount++;                                // dataset line count.

            // replace the normal detmatrix data with the file data
            uint32_t *pdetMatrix = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;
            memcpy(pdetMatrix, &dataSet[0], sizeof(dataSet)) ;

            // Create feature vector and do inference
            expectedClass = 2;
            result = Inference();
            if (result.surfaceIndex == expectedClass)
               goodCount++;
            else
               badCount++;

            totalGood = ((float)goodCount/(float)(goodCount+badCount))*100;
            if (dataCount % 100 == 0){
                printf("%d: %d, %d, Accuracy: %.2f%%\n", dataCount, goodCount, badCount, totalGood);
                printf("  -> Probs: [%.3f %.3f %.3f %.3f %.3f], pred: %d\n",
                    result.probabilities[0], result.probabilities[1],
                    result.probabilities[2], result.probabilities[3],
                    result.probabilities[4], result.surfaceIndex);
            }
        } while (fileStat != 0);

        printf("Glass data complete\n");
        fclose(fileIdDatasetIn);
        // ************************
        // Run the Wood dataset
        // *************************
        char woodDatafile[] = "C:\\github\\Bachelothesis_radar_IWRL6432Boost\\machine_learning_flow\\src\\4-CCS-Integration\\mmwave_dataset_loader\\src\\surface_classification_test_xwrL64xx-SDK5520-Material-Diff_demo\\dataSets\\CCS_class_3_data.bin";
        printf("Wood data");
        fileIdDatasetIn = fopen(woodDatafile, "rb");
        if (fileIdDatasetIn == NULL) {
            printf("Error: Cannot open DatasetIn file !\n");
            exit(0);
        }

        // read file dataset and make comparison between predicted and actual.
        do  {
            fileStat = fread(&dataSet, sizeof(uint32_t), NUM_RANGEBINS_TO_ENQUEUE, fileIdDatasetIn);
            dataCount++;                                // dataset line count.

            // replace the normal detmatrix data with the file data
            uint32_t *pdetMatrix = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;
            memcpy(pdetMatrix, &dataSet[0], sizeof(dataSet)) ;

            // Create feature vector and do inference
            expectedClass = 3;
            result = Inference();
            if (result.surfaceIndex == expectedClass)
               goodCount++;
            else
               badCount++;

            totalGood = ((float)goodCount/(float)(goodCount+badCount))*100;
            if (dataCount % 100 == 0){
                printf("%d: %d, %d, Accuracy: %.2f%%\n", dataCount, goodCount, badCount, totalGood);
                printf("  -> Probs: [%.3f %.3f %.3f %.3f %.3f], pred: %d\n",
                result.probabilities[0], result.probabilities[1],
                result.probabilities[2], result.probabilities[3],
                result.probabilities[4], result.surfaceIndex);
            }
        } while (fileStat != 0);

        printf("Wood data complete\n");
        fclose(fileIdDatasetIn);

        // ************************
        // Run the Concrete dataset
        // *************************
        // TO DO: Change the path to the dataset file
        char concreteDatafile[] = "C:\\github\\Bachelothesis_radar_IWRL6432Boost\\machine_learning_flow\\src\\4-CCS-Integration\\mmwave_dataset_loader\\src\\surface_classification_test_xwrL64xx-SDK5520-Material-Diff_demo\\dataSets\\CCS_class_4_data.bin";
        printf("Concrete data");
        fileIdDatasetIn = fopen(concreteDatafile, "rb");
        if (fileIdDatasetIn == NULL) {
            printf("Error: Cannot open DatasetIn file !\n");
            exit(0);
        }

        // read file dataset and make comparison between predicted and actual.
        do  {
            fileStat = fread(&dataSet, sizeof(uint32_t), NUM_RANGEBINS_TO_ENQUEUE, fileIdDatasetIn);
            dataCount++;                                // dataset line count.

            // replace the normal detmatrix data with the file data
            uint32_t *pdetMatrix = (uint32_t *)&gMmwMssMCB.detMatrix[0].data;
            memcpy(pdetMatrix, &dataSet[0], sizeof(dataSet)) ;

            // Create feature vector and do inference
            expectedClass = 4;
            result = Inference();
            if (result.surfaceIndex == expectedClass)
               goodCount++;
            else
               badCount++;

            totalGood = ((float)goodCount/(float)(goodCount+badCount))*100;
            if (dataCount % 100 == 0){
                printf("%d: %d, %d, Accuracy: %.2f%%\n", dataCount, goodCount, badCount, totalGood);
                printf("  -> Probs: [%.3f %.3f %.3f %.3f %.3f], pred: %d\n",
                    result.probabilities[0], result.probabilities[1],
                    result.probabilities[2], result.probabilities[3],
                    result.probabilities[4], result.surfaceIndex);
            }
        } while (fileStat != 0);

        printf("Concrete data complete\n");
        fclose(fileIdDatasetIn);

        exit(0);

//ML_end

        /* Poll the UART for a new command */
        status = CLI_readLine(gCLI.cfg.UartHandle, (char*)&cmdString[0], READ_LINE_BUFSIZE);
        if(status != SystemP_SUCCESS)
        {
            CLI_write("Error reading\n");
        }

        /* Reset all the tokenized arguments: */
        memset ((void *)&tokenizedArgs, 0, sizeof(tokenizedArgs));
        argIndex      = 0;
        ptrCLICommand = (char*)&cmdString[0];

        /* comment lines found - ignore the whole line*/
        if (cmdString[0]=='%' || cmdString[1]=='%')
        {
            CLI_write ("Skipped\n");
            continue;
        }

        /* Set the CLI status: */
        cliStatus = -1;

        /* The command has been entered we now tokenize the command message */
        while (1)
        {
            /* Tokenize the arguments: */
            tokenizedArgs[argIndex] = strtok(ptrCLICommand, delimitter);
            if (tokenizedArgs[argIndex] == NULL)
                break;

            /* Increment the argument index: */
            argIndex++;
            if (argIndex >= CLI_MAX_ARGS)
                break;

            /* Reset the command string */
            ptrCLICommand = NULL;
        }

        /* Were we able to tokenize the CLI command? */
        if (argIndex == 0)
            continue;

        /* Cycle through all the registered CLI commands: */
        for (index = 0; index < gCLI.numCLICommands; index++)
        {
            ptrCLICommandEntry = &gCLI.cfg.tableEntry[index];

            /* Do we have a match? */
            if (strcmp(ptrCLICommandEntry->cmd, tokenizedArgs[0]) == 0)
            {
                /* YES: Pass this to the CLI registered function */
                cliStatus = ptrCLICommandEntry->cmdHandlerFxn (argIndex, tokenizedArgs);
                if (cliStatus == 0)
                {
                    CLI_write ("Done\r\n\n");
                }
                else
                {
                    CLI_write ("Error %d\r\n", cliStatus);
                }
                break;
            }
        }

        /* Did we get a matching CLI command? */
        if (index == gCLI.numCLICommands)
        {
            /* NO matching command found. Is the mmWave extension enabled? */
            if (gCLI.cfg.enableMMWaveExtension == 1U)
            {
                /* Yes: Pass this to the mmWave extension handler */
                cliStatus = CLI_MMWaveExtensionHandler (argIndex, tokenizedArgs);
            }

            /* Was the CLI command found? */
            if (cliStatus == -1)
            {
                /* No: The command was still not found */
                CLI_write ("'%s' is not recognized as a CLI command\r\n", tokenizedArgs[0]);
            }
        }
    }
    /* Never return for this task. */
    SemaphoreP_pend(&gMmwMssMCB.cliInitTaskCompleteSemHandle, SystemP_WAIT_FOREVER);
}

#if (CLI_REMOVAL == 0 && QUICK_START == 1)
extern uint8_t gIsDefCfgUsed;
void CLI_defaultcfg_task(void* args)
{
    gIsDefCfgUsed = 1;
    CLI_write("\r\n Starting the Demo with Default Configurations...\r\n");
    CLI_ByPassApi(&gCLI.cfg, 0);
    CLI_write ("Running the demo...\r\n");
    vTaskDelete( NULL );
}
#endif


static void CLI_MMWaveExtensionHelp(void)
{
    CLI_CmdTableEntry*  ptrCLICommandEntry;

    /* Get the pointer to the mmWave extension table */
    ptrCLICommandEntry = &gCLIMMWaveExtensionTable[0];

    /* Display the banner: */
    CLI_write ("****************************************************\n");
    CLI_write ("mmWave Extension Help\n");
    CLI_write ("****************************************************\n");

    /* Cycle through all the registered externsion CLI commands: */
    while (ptrCLICommandEntry->cmdHandlerFxn != NULL)
    {
        /* Display the help string*/
        CLI_write ("%s: %s\n",
                    ptrCLICommandEntry->cmd,
                   (ptrCLICommandEntry->helpString == NULL) ?
                    "No help available" :
                    ptrCLICommandEntry->helpString);

        /* Get the next entry: */
        ptrCLICommandEntry++;
    }
    return;
}

static int32_t CLI_help (int32_t argc, char* argv[])
{
    uint32_t    index;

    /* Display the banner: */
    CLI_write ("Help: This will display the usage of the CLI commands\n");
    CLI_write ("Command: Help Description\n");

    /* Cycle through all the registered CLI commands: */
    for (index = 0; index < gCLI.numCLICommands; index++)
    {
        /* Display the help string*/
        CLI_write ("%s: %s\n",
                    gCLI.cfg.tableEntry[index].cmd,
                   (gCLI.cfg.tableEntry[index].helpString == NULL) ?
                    "No help available" :
                    gCLI.cfg.tableEntry[index].helpString);
    }

    /* Is the mmWave Extension enabled? */
    if (gCLI.cfg.enableMMWaveExtension == 1U)
    {
        /* YES: Pass the control to the extension help handler. */
        CLI_MMWaveExtensionHelp ();
    }
    return 0;
}

static int32_t CLI_MMWaveSensorStop (int32_t argc, char* argv[])
{
    /* Sanity Check: Minimum argument check */
    if (argc != 2)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    if(isSensorStarted == 1)
    {
        sensorStop = 1;
    }
    gMmwMssMCB.sceneryParams.numBoundaryBoxes = 0;
    /*Clearing Enable ADC data streaming via SPI field*/
    gMmwMssMCB.spiADCStream = 0;
    /*Setting the GPIO pin to default high state for proper behaviour of this pin*/
    GPIO_pinWriteHigh(gpioBaseAddrLed, pinNumLed);
    return 0;
}

#if (ENABLE_MONITORS==1)
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the enableRFmons command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */

static int32_t CLI_MMWaveMonsEnable(int32_t argc, char* argv[])
{
    if (argc != 2)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing 64 Bits of Monitors to be enabled*/
    sscanf(argv[1], "0x%llx", &gMmwMssMCB.rfMonEnbl);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monPllCtrlVolt command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonPllCtrlVolt(int32_t argc, char* argv[])
{
    if (argc != 6)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing which all PLL Monitors to be enabled*/
    sscanf(argv[1], "0x%hhx", &gMmwMssMCB.monPllVolEnaMask);
    /*Storing Spec Values for APLL CTRL Voltage Min*/
    sscanf(argv[2],"%f",&gMmwMssMCB.SpecVal.APLLVSpecMin);
    /*Storing Spec Values for APLL CTRL Voltage Max*/
    sscanf(argv[3],"%f",&gMmwMssMCB.SpecVal.APLLVSpecMax);
    /*Storing Spec Values for SYNTH MIN CTRL Voltage*/
    sscanf(argv[4],"%f",&gMmwMssMCB.SpecVal.SynthMinVSpecMin);
    /*Storing Spec Values for SYNTH MIN CTRL Voltage*/
    sscanf(argv[5],"%f",&gMmwMssMCB.SpecVal.SynthMaxVSpecMax);
    return 0;

}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monTxRxLbCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonTxRxLbCfg(int32_t argc, char* argv[])
{
    uint8_t lbmonenbl;
    if (argc != 18)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing which all Tx Instances to be enabled
    *  1- Enabled   0- Disabled
    *     | Bit Field   | Definition  |
    *     |-------------|-----------  |
    *     | Bit[0]      | Tx 0        |
    *     | Bit[1]      | Tx 1        |
    */
    lbmonenbl = atoi (argv[1]);
    /*Checking if both Tx instances are enabled*/
    if(lbmonenbl==0x3)
    {   
        /*Storing Enable Status of Tx0 Instance*/
        gMmwMssMCB.ismonTxRxLbCfg[0] = 1;
        /*Storing LoopBack Monitors to be enabled*/
        /*Storing Configuration of Tx0 Instance*/
        /* TXn_RX_LB_MON , TXn_BPM_MON can be triggered in one shot. This parameter is used to enable/ disable each of them.*/
        gMmwMssMCB.monTxRxLbCfg[0].monenbl = 0x07;
        /*Storing Monitor RX gain and TX bias code selection.*/
        gMmwMssMCB.monTxRxLbCfg[0].txrxCodeSel = 0x05;
        /*Storing Monitor RX channels gain code override setting as (2 + (Backoff/2))*/
        /*Dividing by 4 as txBackoffSel is already multiplied by 2 to get final resolution in 0.5 dB */
        gMmwMssMCB.monTxRxLbCfg[0].rxGainCode= 2+ (gMmwMssMCB.factoryCalCfg.txBackoffSel/4);
        /*Storing Monitor TX channel Bias code override setting.*/
        gMmwMssMCB.monTxRxLbCfg[0].txBiasCode = 0x0000;
        /*Storing Monitor RF start Frequency*/
        gMmwMssMCB.monTxRxLbCfg[0].rfFreqGhz = atof (argv[2]);
        /*Storing Monitor RF Frequency Slope*/
        gMmwMssMCB.monTxRxLbCfg[0].rffreqSlopeMhz = atof (argv[3]);

        /*Storing Enable Status of Tx1 Instance*/
        gMmwMssMCB.ismonTxRxLbCfg[1] = 1;
        /*Storing LoopBack Monitors to be enabled*/
        /*Storing Configuration of Tx1 Instance*/
        /* TXn_RX_LB_MON , TXn_BPM_MON can be triggered in one shot. This parameter is used to enable/ disable each of them.*/
        gMmwMssMCB.monTxRxLbCfg[1].monenbl = 0x07;
        /*Storing Monitor RX gain and TX bias code selection.*/
        gMmwMssMCB.monTxRxLbCfg[1].txrxCodeSel = 0x05;
         /*Storing Monitor RX channels gain code override setting*/
        /*Dividing by 4 as txBackoffSel is already multiplied by 2 to get final resolution in 0.5 dB */
        gMmwMssMCB.monTxRxLbCfg[1].rxGainCode= 2+ (gMmwMssMCB.factoryCalCfg.txBackoffSel/4);
        /*Storing Monitor TX channel Bias code override setting.*/
        gMmwMssMCB.monTxRxLbCfg[1].txBiasCode = 0x0000;
        /*Storing Monitor RF start Frequency*/
        gMmwMssMCB.monTxRxLbCfg[1].rfFreqGhz = atof (argv[2]);
        /*Storing Monitor RF Frequency Slope*/
        gMmwMssMCB.monTxRxLbCfg[1].rffreqSlopeMhz = atof (argv[3]);
    }
    /*Checking if only one of the Tx instances are enabled*/
    else if ((lbmonenbl== 0x1) || (lbmonenbl== 0x2))
    {
        /*Storing Enable Status of Tx1 Instance*/
        gMmwMssMCB.ismonTxRxLbCfg[lbmonenbl-1] = 1;
        /*Storing LoopBack Monitors to be enabled*/
        /*Storing Configuration of Tx1 Instance*/
        /* TXn_RX_LB_MON , TXn_BPM_MON can be triggered in one shot. This parameter is used to enable/ disable each of them.*/
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].monenbl = 0x07;
        /*Storing Monitor RX gain and TX bias code selection.*/
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].txrxCodeSel = 0x05;
        /*Storing Monitor RX channels gain code override setting*/
        /*Dividing by 4 as txBackoffSel is already multiplied by 2 to get final resolution in 0.5 dB */
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].rxGainCode= 2+ (gMmwMssMCB.factoryCalCfg.txBackoffSel/4);
        /*Storing Monitor TX channel Bias code override setting.*/
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].txBiasCode = 0x0000;
        /*Storing Monitor RF start Frequency*/
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].rfFreqGhz = atof (argv[2]);
        /*Storing Monitor RF Frequency Slope*/
        gMmwMssMCB.monTxRxLbCfg[lbmonenbl-1].rffreqSlopeMhz = atof (argv[3]);
    }
    else
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing Spec Values for Loopback Monitor*/
    /*Storing Spec Values for Rx gain. Spec Rx gain = Prog Rx gain - 14*/
    gMmwMssMCB.SpecVal.RxGainSpecMin= gMmwMssMCB.factoryCalCfg.rxGain - 14;
    /*Storing Spec Values for Rx gain. Spec Rx gain = Prog Rx gain +14*/
    gMmwMssMCB.SpecVal.RxGainSpecMax= gMmwMssMCB.factoryCalCfg.rxGain + 14;
    /*Storing Spec Values for Loopback Noise*/
    sscanf(argv[4],"%f",&gMmwMssMCB.SpecVal.lbNoiseSpecMax);
    /*Storing Spec Values for Loopback BPM Noise*/
    sscanf(argv[5],"%f",&gMmwMssMCB.SpecVal.lbBPMNoiseSpecMax);
    /*Storing Spec Values for BPM Gain error*/
    sscanf(argv[6],"%f",&gMmwMssMCB.SpecVal.lbBPMGainErrSpecMin);
    sscanf(argv[7],"%f",&gMmwMssMCB.SpecVal.lbBPMGainErrSpecMax);
    /*Storing Spec Values for BPM phase error*/
    sscanf(argv[8],"%f",&gMmwMssMCB.SpecVal.lbBPMPhaseErrSpecMin);
    sscanf(argv[9],"%f",&gMmwMssMCB.SpecVal.lbBPMPhaseErrSpecMax);
    /*Storing Spec Values for RX LoopBack Gain Mismatch Variation*/
    sscanf(argv[10],"%f",&gMmwMssMCB.SpecVal.RxlbGainMisVarSpecMin);
    sscanf(argv[11],"%f",&gMmwMssMCB.SpecVal.RxlbGainMisVarSpecMax);
    /*Storing Spec Values for RX LoopBack Phase Mismatch Variation*/
    sscanf(argv[12],"%f",&gMmwMssMCB.SpecVal.RxlbPhaseMisVarSpecMin);
    sscanf(argv[13],"%f",&gMmwMssMCB.SpecVal.RxlbPhaseMisVarSpecMax);
    /*Storing Spec Values for TX LoopBack Gain Mismatch Variation*/
    sscanf(argv[14],"%f",&gMmwMssMCB.SpecVal.TxlbGainMisVarSpecMin);
    sscanf(argv[15],"%f",&gMmwMssMCB.SpecVal.TxlbGainMisVarSpecMax);
    /*Storing Spec Values for TX LoopBack Phase Mismatch Variation*/
    sscanf(argv[16],"%f",&gMmwMssMCB.SpecVal.TxlbPhaseMisVarSpecMin);
    sscanf(argv[17],"%f",&gMmwMssMCB.SpecVal.TxlbPhaseMisVarSpecMax);
    
    return 0;
}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monTxnPowCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonTxnPowCfg(int32_t argc, char* argv[])
{
    uint8_t txinst;
    if (argc != 4)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing which all Tx Instances to be enabled*/
    txinst = atoi (argv[1]);
    /*Storing Enable Status of Power monitor*/
    gMmwMssMCB.ismonTxpwrCfg[txinst] = 1;
    /*Storing Monitor TX bias code selection*/
    gMmwMssMCB.monTxpwrCfg[txinst].txBiasSel = 0;
    /*Storing Monitor TX channel Bias code override setting*/
    gMmwMssMCB.monTxpwrCfg[txinst].txBiasCode = 0x0000;
    /*Storing Monitor RF start Frequency*/
    gMmwMssMCB.monTxpwrCfg[txinst].rfFreqGhz = atof (argv[2]);
    /*Storing Monitor RF Frequency Slope*/
    gMmwMssMCB.monTxpwrCfg[txinst].rffreqSlopeMhz = atof (argv[3]);
    /*Making backoff as 0 because frontend firmware will consider backoff used for calibration*/
    gMmwMssMCB.monTxpwrCfg[txinst].txBackoff =0;
    /*Storing Spec Values for Power Monitor*/
    /*Storing Spec Values for Tx Power*/
    if(gMmwMssMCB.factoryCalCfg.txBackoffSel == 0)
    {
        gMmwMssMCB.SpecVal.TxPowSpecMin[txinst]=5;
    }
    else if(gMmwMssMCB.factoryCalCfg.txBackoffSel > 0)
    {
        /*Dividing txBackoffSel by 2 to get resolution in 1 dB*/
        gMmwMssMCB.SpecVal.TxPowSpecMin[txinst]=3-gMmwMssMCB.factoryCalCfg.txBackoffSel/2;
    }

    return 0;

}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monTxnBBPowCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonTxnBBPowCfg(int32_t argc, char* argv[])
{
    uint8_t txinst;
    if (argc != 6)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing which all Tx Instances to be enabled*/
    txinst = atoi (argv[1]);
    /*Storing Enable Status of Ball Break monitor*/
    gMmwMssMCB.ismonTxpwrBBCfg[txinst] = 1;
    /*Storing Monitor TX bias code selection*/
    gMmwMssMCB.monTxpwrBBCfg[txinst].txBiasSel = 0;
    /*Storing Monitor TX channel Bias code override setting*/
    gMmwMssMCB.monTxpwrBBCfg[txinst].txBiasCode = 0x0000;
    /*Storing Monitor RF start Frequency*/    
    gMmwMssMCB.monTxpwrBBCfg[txinst].rfFreqGhz = atof (argv[2]);
    /*Storing Monitor RF Frequency Slope*/    
    gMmwMssMCB.monTxpwrBBCfg[txinst].rffreqSlopeMhz = atof (argv[3]);
    /*Making backoff as 0 because frontend firmware will consider backoff used for calibration*/
    gMmwMssMCB.monTxpwrBBCfg[txinst].txBackoff = 0;
    /*Storing Spec Values for Ball Break Monitor*/
    /*Storing Spec Values for absolute Return Loss*/
    sscanf(argv[4],"%f",&gMmwMssMCB.SpecVal.TxBBRetLossSpec);
    /*Storing Spec Values for change in return loss from factory*/
    sscanf(argv[5],"%f",&gMmwMssMCB.SpecVal.TxBBRetLossVarSpec);
    return 0;

}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monTxnDcSigCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonTxnDcSigCfg(int32_t argc, char* argv[])
{
    uint8_t txinst;
    if (argc != 5)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing which all Tx Instances to be enabled*/
    txinst = atoi (argv[1]);
    /*Storing Enable Status of DC Signal monitor*/
    gMmwMssMCB.ismonTxDcSigCfg[txinst] = 1;
    /*Storing Monitor TX bias code selection*/
    gMmwMssMCB.monTxDcSigCfg[txinst].txBiasSel = 0;
    /*Storing Monitor TX channel Bias code override setting*/
    gMmwMssMCB.monTxDcSigCfg[txinst].txBiasCode = 0x0000;
    /*Storing Monitor RF start Frequency*/ 
    gMmwMssMCB.monTxDcSigCfg[txinst].rfFreqGhz = atof (argv[2]);
    /*Storing Monitor RF Frequency Slope*/
    gMmwMssMCB.monTxDcSigCfg[txinst].rffreqSlopeMhz = atof (argv[3]);
    /*Storing Spec Values for DC Signal Monitor*/
    /*Storing Spec Values for status of DC Signal Monitor*/
    sscanf(argv[4],"0x%hhx",&gMmwMssMCB.SpecVal.TxDCSigResSpec);
    return 0;

}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monRxHpfDcSigCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonRxHpfDcSigCfg(int32_t argc, char* argv[])
{
    if (argc != 5)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing Monitor RF start Frequency*/ 
    gMmwMssMCB.monRxHpfDcSigCfg.rfFreqGhz = atof (argv[1]);
    /*Storing RX HPF Monitors to be enabled*/
    /*RX_DC_MON , HPF_CUTOFF_MON ENABLED*/
    gMmwMssMCB.monRxHpfDcSigCfg.monenbl = 0x03;
    /*Storing HPF_CUTOFF_MON monitor Chirp HPF corner frequency.*/
    sscanf(argv[2], "0x%hhx", &gMmwMssMCB.monRxHpfDcSigCfg.rxHpfSel);
    /*Storing Spec Values for RX HPF DC Signal Monitor*/
    /*Storing Spec Values for Attenuation*/
    sscanf(argv[3],"%f",&gMmwMssMCB.SpecVal.RxHPFAttnSpecMin);
    sscanf(argv[4],"%f",&gMmwMssMCB.SpecVal.RxHPFAttnSpecMax);
    return 0;
}
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the monPmClkDcCfg command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveMonPmClkDcCfg(int32_t argc, char* argv[])
{
    if (argc != 3)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /*Storing Monitor RF start Frequency*/ 
    gMmwMssMCB.monPmClkDcStFreqGhz = atof (argv[1]);
    /*Storing Spec Values for PM CLK DC Monitor*/
    /*Storing Spec Values for status of PM CLK DC Monitor*/
    sscanf(argv[2],"0x%x",&gMmwMssMCB.SpecVal.PMClkDCSigStatSpec);
    return 0;
}

#endif

static int32_t CLI_MMWaveAdcLogging (int32_t argc, char* argv[])
{
    /* Sanity Check: Minimum argument check */
    if ((argc < 2) || (argc > 6))
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Populate configuration: */
    gMmwMssMCB.adcLogging.enable = (uint8_t) atoi (argv[1]);
    gMmwMssMCB.adcLogging.sideBandEnable   = 0U;
    gMmwMssMCB.adcLogging.scramblerMode    = 0U;
    gMmwMssMCB.adcLogging.laneRate         = 0U;

    if(argc == 3)
    {
        gMmwMssMCB.adcLogging.sideBandEnable   = (uint8_t) atoi (argv[2]);
        gMmwMssMCB.adcLogging.swizzlingMode    = 0U;
    }
    else if(argc == 4)
    {
        gMmwMssMCB.adcLogging.sideBandEnable   = (uint8_t) atoi (argv[2]);
        gMmwMssMCB.adcLogging.swizzlingMode    = (uint8_t) atoi (argv[3]);

    }
    else if(argc == 5)
    {
        gMmwMssMCB.adcLogging.sideBandEnable   = (uint8_t) atoi (argv[2]);
        gMmwMssMCB.adcLogging.swizzlingMode    = (uint8_t) atoi (argv[3]);
        gMmwMssMCB.adcLogging.scramblerMode    = (uint8_t) atoi (argv[4]);
    }
    else if(argc == 6)
    {
        gMmwMssMCB.adcLogging.sideBandEnable   = (uint8_t) atoi (argv[2]);
        gMmwMssMCB.adcLogging.swizzlingMode    = (uint8_t) atoi (argv[3]);
        gMmwMssMCB.adcLogging.scramblerMode    = (uint8_t) atoi (argv[4]);
        gMmwMssMCB.adcLogging.laneRate         = (uint8_t) atoi (argv[5]);
    }
    else
    {
        /*  This is the default swizzling mode used if user config not given */
        gMmwMssMCB.adcLogging.swizzlingMode    = 2U;
    }

    if(gMmwMssMCB.adcLogging.enable == 1)
    {
        /* Configure MDIO pins for ADC data Transfer */
        Pinmux_config(gPinMuxMDOCfg, PINMUX_DOMAIN_ID_MAIN);

        /*  Enable MDLL Clock for RDIF interface */
        SOC_enableMDLLClock();
    }
    if(gMmwMssMCB.adcLogging.enable == 2)
    {   
        #if (SPI_ADC_DATA_STREAMING==1)
            uint32_t            baseAddr, regionId;
            int32_t             testStatus = SystemP_SUCCESS;
            uint8_t             *srcBuffPtr, *dstBuffPtr;
            EDMACCPaRAMEntry    edmaParam;
            uint32_t            dmaCh, tcc, param, parambackup;
            gMmwMssMCB.spiADCStream = 1;
            if(gMmwMssMCB.spiADCStream == 1)
            {
                GPIO_pinWriteHigh(gpioBaseAddrLed, pinNumLed);
            }
            if(gMmwMssMCB.spiADCStream == 1)
            {
                /* PAD config for MCSPI */
                Pinmux_config(gPinMuxMainDomainCfgSpi, PINMUX_DOMAIN_ID_MAIN);
                /* Init the MCSPI */
                clock_init();
                MCSPI_init();
                Drivers_mcspiOpen();
                
                /* Allocate memory for buffer */
                adcDataPerFrame = gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst * gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame * gMmwMssMCB.profileComCfg.h_NumOfAdcSamples * gMmwMssMCB.numRxAntennas * 2;
                /* Configure EDMA channel for reading the Raw ADC data */
                baseAddr = EDMA_getBaseAddr(gEdmaHandle[CONFIG_EDMA1]);
                regionId = EDMA_getRegionId(gEdmaHandle[CONFIG_EDMA1]);
                dmaCh = 37;
                testStatus = EDMA_allocDmaChannel(gEdmaHandle[CONFIG_EDMA1], &dmaCh);
                DebugP_assert(testStatus == SystemP_SUCCESS);
                tcc = 37;
                testStatus = EDMA_allocTcc(gEdmaHandle[CONFIG_EDMA1], &tcc);
                DebugP_assert(testStatus == SystemP_SUCCESS);
                param = 37;
                testStatus = EDMA_allocParam(gEdmaHandle[CONFIG_EDMA1], &param);
                DebugP_assert(testStatus == SystemP_SUCCESS);
                parambackup = EDMA_RESOURCE_ALLOC_ANY;
                testStatus = EDMA_allocParam(gEdmaHandle[CONFIG_EDMA1], &parambackup);
                DebugP_assert(testStatus == SystemP_SUCCESS);
                srcBuffPtr = (uint8_t *) CSL_APP_HWA_ADCBUF_RD_U_BASE;
                dstBuffPtr = (uint8_t *) (adcbuffer + 0x22000000U);
                /* Request channel */
                EDMAConfigureChannelRegion(baseAddr, regionId, EDMA_CHANNEL_TYPE_DMA,
                dmaCh, tcc, param, EDMA_TEST_EVT_QUEUE_NO);
                /* Program Param Set */
                EDMACCPaRAMEntry_init(&edmaParam);
                edmaParam.srcAddr       = (uint32_t) (srcBuffPtr);
                edmaParam.destAddr      = (uint32_t) (dstBuffPtr);
                edmaParam.aCnt          = (uint16_t) gMmwMssMCB.profileComCfg.h_NumOfAdcSamples * gMmwMssMCB.numRxAntennas * 2;
                edmaParam.bCnt          = (uint16_t) gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst * gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame;
                edmaParam.cCnt          = (uint16_t) 1;
                edmaParam.bCntReload    = (uint16_t) gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst * gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame;
                edmaParam.srcBIdx       = (int16_t) 0;
                edmaParam.destBIdx      = (int16_t) (gMmwMssMCB.profileComCfg.h_NumOfAdcSamples * gMmwMssMCB.numRxAntennas * 2);
                edmaParam.srcCIdx       = (int16_t) 0;
                edmaParam.destCIdx      = (int16_t) (-1 * adcDataPerFrame);
                edmaParam.linkAddr      = (0x4000 + (32 * parambackup));
                edmaParam.opt          |= ((((uint32_t)tcc) << EDMA_OPT_TCC_SHIFT) & EDMA_OPT_TCC_MASK);
                EDMASetPaRAM(baseAddr, param, &edmaParam);
                EDMAEnableTransferRegion(baseAddr, regionId, dmaCh,
                    EDMA_TRIG_MODE_EVENT);

                /* Program Param Set */
                EDMACCPaRAMEntry_init(&edmaParam);
                edmaParam.srcAddr       = (uint32_t) (srcBuffPtr);
                edmaParam.destAddr      = (uint32_t) (dstBuffPtr);
                edmaParam.aCnt          = (uint16_t) gMmwMssMCB.profileComCfg.h_NumOfAdcSamples * gMmwMssMCB.numRxAntennas * 2;
                edmaParam.bCnt          = (uint16_t) gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst * gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame;
                edmaParam.cCnt          = (uint16_t) 1;
                edmaParam.bCntReload    = (uint16_t) gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst * gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame;
                edmaParam.srcBIdx       = (int16_t) 0;
                edmaParam.destBIdx      = (int16_t) (gMmwMssMCB.profileComCfg.h_NumOfAdcSamples * gMmwMssMCB.numRxAntennas * 2);
                edmaParam.srcCIdx       = (int16_t) 0;
                edmaParam.destCIdx      = (int16_t) (-1 * adcDataPerFrame);
                edmaParam.linkAddr      = (0x4000 + (32 * parambackup));
                edmaParam.opt          |= ((((uint32_t)tcc) << EDMA_OPT_TCC_SHIFT) & EDMA_OPT_TCC_MASK);
                EDMASetPaRAM(baseAddr, parambackup, &edmaParam);
            } 
        #else
            CLI_write ("Error: SPI ADC Streaming is not enabled \r\n");
            return -1;

        #endif
    }
    return 0;
}

static int32_t MmwDemo_CLISensorWarmReset (int32_t argc, char* argv[])
{
    volatile uint32_t fecPdstatus = 0;
    /* Sanity Check: Minimum argument check */
    if (argc > 2)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }
    /* Gracefully Shutdown the FrontEnd for Warm Reset */
    /* Halt the FECSS core */
    HW_WR_REG32((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_FEC_CORE_SYSRESET_PARAM), FEC_CORE_HALT);

    /* Set to Manual Mode in FEC_PWR_REQ_PARAM register */
    CSL_REG32_FINS((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_FEC_PWR_REQ_PARAM ), TOP_PRCM_FEC_PWR_REQ_PARAM_FEC_PWR_REQ_PARAM_MODE, 0);

    /* Do not retain any FECSS RAM */
    CSL_REG32_FINS((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_PSCON_FEC_PD_RAM_STATE ), TOP_PRCM_PSCON_FEC_PD_RAM_STATE_PSCON_FEC_PD_RAM_STATE_FEC_PD_MEM_SLEEP_STATE,0);
    CSL_REG32_FINS((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_PSCON_FEC_PD_RAM_GRP4_STATE ), TOP_PRCM_PSCON_FEC_PD_RAM_GRP4_STATE_PSCON_FEC_PD_RAM_GRP4_STATE_FEC_PD_MEM_GRP4_SLEEP_STATE,0);

    /* Switch OFF the Front end */
    CSL_REG32_FINS((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_FEC_PWR_REQ_PARAM ), TOP_PRCM_FEC_PWR_REQ_PARAM_FEC_PWR_REQ_PARAM_WAKEUP_OUT_STATE, 0);
    do
    {
        fecPdstatus = ((HW_RD_REG32(CSL_TOP_PRCM_U_BASE+CSL_TOP_PRCM_PSCON_FEC_PD_EN) & 0x200) >> 9);
    }while(fecPdstatus == 0);

    /* Switch ON the Front end for allowing RBL to load the Front End Firmware*/
    CSL_REG32_FINS((CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_FEC_PWR_REQ_PARAM ), TOP_PRCM_FEC_PWR_REQ_PARAM_FEC_PWR_REQ_PARAM_WAKEUP_OUT_STATE, 1);
    do
    {
        fecPdstatus = ((HW_RD_REG32(CSL_TOP_PRCM_U_BASE+CSL_TOP_PRCM_PSCON_FEC_PD_EN) & 0x200) >> 9);
    }while(fecPdstatus == 1);

    /* Enable Clock for FECSS */
    HW_WR_REG32((CSL_APP_CTRL_U_BASE + CSL_APP_CTRL_FECSS_CLK_GATE),0);

    /* Wait for Clock to start */
    ClockP_usleep(500);

    /* Issue Warm Reset */
    /* Following AON Registers fields are used by bootloader during Warm reset. Setting Boot Vector to 0 will make boot loader to reload application from flash in a Warm reset scenario */
    uint32_t *pc_reg1 = (uint32_t *)(CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_PC_REGISTER1);
    uint32_t *pc_reg2 = (uint32_t *)(CSL_TOP_PRCM_U_BASE + CSL_TOP_PRCM_PC_REGISTER2);
    /*Setting pc parity, data integrity check parity,image length parity, boot vector parity, lbist in boot, perform integrity check to 0*/
    *pc_reg1 = 0x0;
    /*setting boot vector to 0*/
    *pc_reg2 = *pc_reg2 & (uint32_t)PCR2_BOOTVEC_CLR;
    SOC_triggerWarmReset();
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for ADC data source configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveAdcDataSourceCfg (int32_t argc, char* argv[])
{
    /* Sanity Check: Minimum argument check */
    if (argc != 3)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Save Configuration to use later */
    gMmwMssMCB.adcDataSourceCfg.source = atoi (argv[1]);
    
    if (strlen(argv[2]) <= DPC_ADC_FILENAME_MAX_LEN)
    {
        strncpy(gMmwMssMCB.adcDataSourceCfg.fileName, argv[2], DPC_ADC_FILENAME_MAX_LEN);
    }
    else
    {
        CLI_write ("Error: Filename too long\n");
        return -1;
    }

    return 0;
}

#ifdef ENABLE_UART_HIGH_BAUD_RATE_DYNAMIC_CFG
#include <drivers/uart.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
extern UART_Params gUartParams[1];
void Drivers_uartInit();

static int32_t MmwDemo_CLIChangeUartBaudRate (int32_t argc, char* argv[])
{
    uint32_t baudRate;
    /* Sanity Check: Minimum argument check */
    if (argc != 2)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    baudRate = (uint32_t) atoi (argv[1]);

    gUartParams[0].baudRate = baudRate;

    Drivers_uartClose();
    UART_deinit();
    Drivers_uartInit();
    Drivers_uartOpen();

    return 0;
}
#endif

static int32_t CLI_MMWaveMPDBoundaryBoxConfig (int32_t argc, char* argv[])
{
/*For Debugging purposes*/
#if 0
    if (argc != 7)
    {
        CLI_write ("Error: Invalid usage of the CLI command\r\n");
        return -1;
    }
#endif
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for configurations related to switching 
 *      between high performance tracker and low power presence configs automatically
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MMWaveprofileSwitchCfg (int32_t argc, char* argv[])
{
    if(argc != 4){
        CLI_write("Error: Invalid usage of CLI command\r\n");
        return -1;
    }
    gMmwMssMCB.profileSwitchCfg.switchCfgEnable = (uint8_t)atoi(argv[1]);
    gMmwMssMCB.profileSwitchCfg.frmPretoTrack = (uint8_t)atoi(argv[2]);
    gMmwMssMCB.profileSwitchCfg.frmTracktoPre = (uint8_t)atoi(argv[3]);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for micro doppler processing configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIMicroDopplerParamCfg (int32_t argc, char* argv[])
{

    DPU_uDopProcCliCfg   cfg;
    int32_t             expectedArgc = 1+9;

    if (argc != expectedArgc)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    //"<enabled> <genApproach> <targetSize> <magnitudeSquared> <circShiftCentroid> <normalizedSpectrum> <interceptThrLowFreq> <interceptThrUpFreq> <specShiftMode>"
    cfg.enabled                 = (uint8_t) atoi (argv[1]);
    cfg.genApproach             = (uint8_t) atoi (argv[2]);
    cfg.targetSize              = (float)   atof (argv[3]);
    cfg.magnitudeSquared        = (uint8_t) atoi (argv[4]);
    cfg.circShiftAroundCentroid = (uint8_t) atoi (argv[5]);
    cfg.normalizedSpectrum      = (uint8_t) atoi (argv[6]);
    cfg.interceptThrLowFreq     = (float)   atof (argv[7]);
    cfg.interceptThrUpFreq      = (float)   atof (argv[8]);
    cfg.specShiftMode           = (uint8_t) atoi (argv[9]);

    /* Save Configuration to use later */
    memcpy(&gMmwMssMCB.microDopplerCliCfg, &cfg, sizeof(cfg));

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for measurement configuration of range bias
 *      and channel phase offsets
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIMeasureRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[])
{
    DPC_ObjectDetection_MeasureRxChannelBiasCliCfg   cfg;

    /* Sanity Check: Minimum argument check */
    if (argc != 4)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled          = (uint8_t) atoi (argv[1]);
    cfg.targetDistance   = (float) atof (argv[2]);
    cfg.searchWinSize   = (float) atof (argv[3]);

    /* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.measureRxChannelBiasCliCfg,
           &cfg, sizeof(cfg));

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for human/nonhuman classifier configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIClassifierParamCfg (int32_t argc, char* argv[])
{

    DPU_uDopClassifierCliCfg   cfg;
    int32_t             expectedArgc = 1+3;

    if (argc != expectedArgc)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    //"<enabled> "
    cfg.enabled                 = (uint8_t) atoi (argv[1]);
    cfg.minNumPntsPerTrack      = (uint8_t) atoi (argv[2]);
    cfg.missTotFrmThre          = (uint8_t) atoi (argv[3]);

    /* Save Configuration to use later */
    memcpy(&gMmwMssMCB.microDopplerClassifierCliCfg, &cfg, sizeof(cfg));

    return 0;
}

int32_t CLI_readLine(UART_Handle uartHandle, char *lineBuf, uint32_t bufSize)
{
    int32_t status = SystemP_FAILURE;

    if(uartHandle!=NULL)
    {
        uint32_t done = 0;
        UART_Transaction trans;
        uint8_t  readByte;
        int32_t  transferOK;
        uint32_t numCharRead = 0;

        while(!done)
        {
            UART_Transaction_init(&trans);

            status = SystemP_SUCCESS;

            /* Read one char */
            trans.buf   = &readByte;
            trans.count = 1;
            transferOK = UART_read(uartHandle, &trans);
            if((SystemP_SUCCESS != (transferOK)) || (UART_TRANSFER_STATUS_SUCCESS != trans.status))
            {
                status = SystemP_FAILURE;
            }
            if(status == SystemP_SUCCESS)
            {
                if(numCharRead < bufSize)
                {
                    lineBuf[numCharRead] = readByte;
                    numCharRead++;
                }

                /* Echo the char */
                trans.buf   = &readByte;
                trans.count = 1;
                transferOK = UART_write(uartHandle, &trans);
                if((SystemP_SUCCESS != (transferOK)) || (UART_TRANSFER_STATUS_SUCCESS != trans.status))
                {
                    status = SystemP_FAILURE;
                }
            }
            if(status == SystemP_SUCCESS)
            {
                if((readByte == 10) || (readByte == 13))/* "LINE FEED" "New Line" entered, (ASCII: 10, 13) */
                {
                    /* terminate the string, reset numCharRead  */
                    lineBuf[numCharRead-1] = 0;

                    done = 1;

                    /* Echo a new line to terminal (ASCII: 10) */
                    readByte = 10;
                    trans.buf   = &readByte;
                    trans.count = 1;
                    transferOK = UART_write(uartHandle, &trans);
                    if((SystemP_SUCCESS != (transferOK)) || (UART_TRANSFER_STATUS_SUCCESS != trans.status))
                    {
                        status = SystemP_FAILURE;
                    }
                }
            }
            if(status != SystemP_SUCCESS)
            {
                done = 1; /* break out in case of error */
            }
        }
    }
    return status;
}
#endif

/* CLI config handler functions */

static int32_t CLI_MMWaveChannelCfg (int32_t argc, char* argv[])
{
    uint32_t i;

    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 4)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate the frame configuration: */
        gMmwMssMCB.channelCfg.h_RxChCtrlBitMask  = atoi (argv[1]);
        gMmwMssMCB.channelCfg.h_TxChCtrlBitMask  = atoi (argv[2]);
        gMmwMssMCB.channelCfg.c_MiscCtrl         = atoi (argv[3]);
    }
    #else
    {
        gMmwMssMCB.channelCfg.h_RxChCtrlBitMask  = CLI_CHA_CFG_RX_BITMASK;
        gMmwMssMCB.channelCfg.h_TxChCtrlBitMask  = CLI_CHA_CFG_TX_BITMASK;
        gMmwMssMCB.channelCfg.c_MiscCtrl         = CLI_CHA_CFG_MISC_CTRL;
    }
    #endif

    gMmwMssMCB.numRxAntennas = 0;
    gMmwMssMCB.numTxAntennas = 0;
    for (i=0; i<16; i++)
    {
        if((gMmwMssMCB.channelCfg.h_TxChCtrlBitMask >> i) & 0x1)
        {
            gMmwMssMCB.numTxAntennas++;
        }
        if((gMmwMssMCB.channelCfg.h_RxChCtrlBitMask >> i) & 0x1)
        {
            if (gMmwMssMCB.numRxAntennas < MAX_NUM_RX_ANTENNA)
            {
                gMmwMssMCB.rxAntOrder[gMmwMssMCB.numRxAntennas] = (uint8_t) i;
                gMmwMssMCB.numRxAntennas++;
            }
            else
            {
                CLI_write ("Error: Number of selected Rx antennas must be less than or equal to max Rx\n");
                return -1;
            }
            
        }
    }

    return 0;
}

static int32_t CLI_MMWaveChirpCommonCfg (int32_t argc, char* argv[])
{
    uint8_t IsValidSampRate = 0;
    uint16_t i = 0;

    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 8)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate the Chirp Common configuration: */
        gMmwMssMCB.profileComCfg.c_DigOutputSampRate  = atoi (argv[1]); //Range 8 to 100
        gMmwMssMCB.profileComCfg.c_DigOutputBitsSel   = atoi (argv[2]);
        gMmwMssMCB.profileComCfg.c_DfeFirSel          = atoi (argv[3]);
        gMmwMssMCB.profileComCfg.h_NumOfAdcSamples    = atoi (argv[4]);
        gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel  = atoi (argv[5]);
        gMmwMssMCB.profileComCfg.h_ChirpRampEndTime   = 10.0 * atof (argv[6]);
        gMmwMssMCB.profileComCfg.c_ChirpRxHpfSel      = atoi (argv[7]);
    }
    #else
    {
        gMmwMssMCB.profileComCfg.c_DigOutputSampRate  = CLI_DIG_OUT_SAMPLING_RATE; //Range 8 to 100
        gMmwMssMCB.profileComCfg.c_DigOutputBitsSel   = CLI_DIG_OUT_BITS_SEL;
        gMmwMssMCB.profileComCfg.c_DfeFirSel          = CLI_DFE_FIR_SEL;
        gMmwMssMCB.profileComCfg.h_NumOfAdcSamples    = CLI_NUM_ADC_SAMPLES;
        gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel  = CLI_MIMO_SEL;
        gMmwMssMCB.profileComCfg.h_ChirpRampEndTime   = CLI_CHIRP_RAMP_END_TIME;
        gMmwMssMCB.profileComCfg.c_ChirpRxHpfSel      = CLI_CHIRP_RX_HPF_SEL;
    }
    #endif

    /* Chirp Common Config: MiscSettings and HPFFastInit Duration */
    gMmwMssMCB.profileComCfg.c_MiscSettings       = 0U;
    gMmwMssMCB.profileComCfg.c_HpfFastInitDuration= 15U; // 15uSec

    gMmwMssMCB.adcSamplingRate = 100.0/gMmwMssMCB.profileComCfg.c_DigOutputSampRate; //Range 1MHz to 12.5MHz

    if ((gMmwMssMCB.profileComCfg.h_NumOfAdcSamples >= 2U) && (gMmwMssMCB.profileComCfg.h_NumOfAdcSamples <= 2048U))
    {
        gMmwMssMCB.numRangeBins = mathUtils_pow2roundup(gMmwMssMCB.profileComCfg.h_NumOfAdcSamples)/2; //Real only sampling
    }
    else
    {
        CLI_write ("Error: Number of adc samples configured is not within the supported range\n");
        return -1;
    }

    /* Check if ADC Sampling Rate is standard*/
    for(i=0;i<NUM_VALID_SAMP_RATE;i++)
    {
        if (gMmwMssMCB.profileComCfg.c_DigOutputSampRate == ValidDigSampRates[i])
        {
            IsValidSampRate = 1U;
            break;
        }
    }

    if (IsValidSampRate !=1U)
    {
        CLI_write("Warning: The non-standard ADC Sampling Rates are not validated in TI for spur performance\r\n");
    }

    if ((gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel == 1) || (gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel == 0))
    {
        /* TDM-MIMO*/
        gMmwMssMCB.isBpmEnabled = 0;
    }
    else if (gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel == 4)
    {
        /* BPM-MIMO*/
        gMmwMssMCB.isBpmEnabled = 1;
    }
    else
    {
        CLI_write ("Error: c_ChirpTxMimoPatSel must have value either 1 (TDM-MIMO) or 4 (BPM-MIMO)\n");
        return -1;
    }

    return 0;
}

static int32_t CLI_MMWaveChirpTimingCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 6)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Assumes Low res clock and to be updated for fast chirping/clock use cases */
        /* Populate the Chirp Timing configuration: */
        gMmwMssMCB.profileTimeCfg.h_ChirpIdleTime      = 10.0 * atof (argv[1]);
        gMmwMssMCB.profileTimeCfg.h_ChirpAdcStartTime  = (atoi (argv[2])) << 10; //num of skip samples
        /* Front End Firmware expects the ChirpTxStartTime in resolution of 20ns, hence multiply by 50 */
        gMmwMssMCB.profileTimeCfg.xh_ChirpTxStartTime  = 50.0 * (atof (argv[3]));
        gMmwMssMCB.chirpSlope                          = atof (argv[4]); //MHz/us
        gMmwMssMCB.startFreq                           = atof (argv[5]); //GHz
    }
    #else
    {
        gMmwMssMCB.profileTimeCfg.h_ChirpIdleTime      = CLI_CHIRP_IDLE_TIME;
        gMmwMssMCB.profileTimeCfg.h_ChirpAdcStartTime  = CLI_CHIRP_ADC_START_TIME;
        gMmwMssMCB.profileTimeCfg.xh_ChirpTxStartTime  = CLI_CHIRP_TX_START_TIME;
        gMmwMssMCB.chirpSlope                          = CLI_CHIRP_SLOPE;
        gMmwMssMCB.startFreq                           = CLI_START_FREQ;
    }
    #endif

#ifdef SOC_XWRL64XX
    gMmwMssMCB.profileTimeCfg.xh_ChirpRfFreqSlope  = (gMmwMssMCB.chirpSlope * 1048576.0)/(3* 100 * 100);
    /* Front End Firmware expects Start freq (MHz) as 1 LSB = (3 x APLL_FREQ / 2^16) * 2^6 resolution  */
    gMmwMssMCB.profileTimeCfg.w_ChirpRfFreqStart   = (gMmwMssMCB.startFreq * 1000.0 * 256.0)/(300);
#else
    gMmwMssMCB.profileTimeCfg.xh_ChirpRfFreqSlope  = (gMmwMssMCB.chirpSlope * 1048576.0)/(4* 100 * 100);
    /* Front End Firmware expects Start freq (MHz) as 1 LSB = (4 x APLL_FREQ / 2^16) * 2^6 resolution */
    gMmwMssMCB.profileTimeCfg.w_ChirpRfFreqStart   = (gMmwMssMCB.startFreq * 1000.0 * 256.0)/(400);
#endif

    return 0;
}

static int32_t CLI_MMWaveFrameCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 7)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate the frame configuration: */
        gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst      = atoi (argv[1]);
        gMmwMssMCB.frameCfg.c_NumOfChirpsAccum        = atoi (argv[2]);
        gMmwMssMCB.burstPeriod                        = atof (argv[3]); //us
        gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame      = atoi (argv[4]);
        gMmwMssMCB.frameCfg.w_FramePeriodicity        = ((atof (argv[5])) * gSocClk)/1000; // x crystal_clk_MHz x 1000
        gMmwMssMCB.frameCfg.h_NumOfFrames             = atoi (argv[6]);
    }
    #else
    {
        gMmwMssMCB.frameCfg.h_NumOfChirpsInBurst = CLI_NUM_CHIRPS_PER_BURST;
        gMmwMssMCB.frameCfg.c_NumOfChirpsAccum = CLI_NUM_CHIRPS_ACCUM;
        gMmwMssMCB.burstPeriod = CLI_BURST_PERIOD;
        gMmwMssMCB.frameCfg.h_NumOfBurstsInFrame = CLI_NUM_BURSTS_PER_FRAME;
        gMmwMssMCB.frameCfg.w_FramePeriodicity = CLI_FRAME_PERIOD;
        gMmwMssMCB.frameCfg.h_NumOfFrames = CLI_NUM_FRAMES;
    }
    #endif

    gMmwMssMCB.frameCfg.w_BurstPeriodicity = 10.0 * gMmwMssMCB.burstPeriod;

    return 0;
}

static int32_t CLI_MMWaveGuiMonSel (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if ((argc < 12) || (argc > 13))
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: minimum mandatory configs */
        gMmwMssMCB.guiMonSel.pointCloud      = atoi (argv[1]);
        gMmwMssMCB.guiMonSel.rangeProfile          = atoi (argv[2]);
        gMmwMssMCB.guiMonSel.noiseProfile         = atoi (argv[3]);
        gMmwMssMCB.guiMonSel.rangeAzimuthHeatMap  = atoi (argv[4]);
        gMmwMssMCB.guiMonSel.rangeDopplerHeatMap  = atoi (argv[5]);
        gMmwMssMCB.guiMonSel.statsInfo            = atoi (argv[6]);
        gMmwMssMCB.guiMonSel.presenceInfo          = atoi (argv[7]);
        gMmwMssMCB.guiMonSel.adcSamples            = atoi (argv[8]);
        gMmwMssMCB.guiMonSel.trackerInfo           = atoi (argv[9]);
        gMmwMssMCB.guiMonSel.microDopplerInfo      = atoi (argv[10]);
        gMmwMssMCB.guiMonSel.classifierInfo      = atoi (argv[11]);
        
        /* Populate configuration: additional configs */
        if (argc > 12)
        {
            gMmwMssMCB.guiMonSel.quickEvalInfo      = atoi (argv[12]);
        }
    }
    #else
    {
        gMmwMssMCB.guiMonSel.pointCloud           = CLI_GUIMON_POINTCLOUD_EN;
        gMmwMssMCB.guiMonSel.rangeProfile         = CLI_GUIMON_RNGPROFILE_EN;
        gMmwMssMCB.guiMonSel.noiseProfile         = CLI_GUIMON_NOISEPROFILE_EN;
        gMmwMssMCB.guiMonSel.rangeAzimuthHeatMap  = CLI_GUIMON_RNGAZHEATMAP_EN;
        gMmwMssMCB.guiMonSel.rangeDopplerHeatMap  = CLI_GUIMON_RNGDOPPHEATMAP_EN;
        gMmwMssMCB.guiMonSel.statsInfo            = CLI_GUIMON_STATSINFO_EN;
        gMmwMssMCB.guiMonSel.presenceInfo         = CLI_GUIMON_PRESENCEINFO_EN;
        gMmwMssMCB.guiMonSel.adcSamples           = CLI_GUIMON_ADCSAMPLES_EN;
        gMmwMssMCB.guiMonSel.trackerInfo          = CLI_GUIMON_TRACKERINFO_EN;
        gMmwMssMCB.guiMonSel.microDopplerInfo     = CLI_GUIMON_MDINFO_EN;
        gMmwMssMCB.guiMonSel.classifierInfo       = CLI_GUIMON_CLASSIFIERINFO_EN;
        gMmwMssMCB.guiMonSel.quickEvalInfo        = CLI_GUIMON_QUICKEVAL_EN;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveSigProcChainCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 9)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.sigProcChainCfg.azimuthFftSize   = (uint16_t) atoi (argv[1]);
        gMmwMssMCB.sigProcChainCfg.elevationFftSize = (uint16_t) atoi (argv[2]);
        gMmwMssMCB.sigProcChainCfg.motDetMode       = (uint16_t) atoi (argv[3]);
        gMmwMssMCB.sigProcChainCfg.coherentDoppler  = (uint16_t) atoi (argv[4]);
        gMmwMssMCB.sigProcChainCfg.numFrmPerMinorMotProc = (uint16_t) atoi (argv[5]);
        gMmwMssMCB.sigProcChainCfg.numMinorMotionChirpsPerFrame    = (uint16_t) atoi (argv[6]);
        gMmwMssMCB.sigProcChainCfg.forceMinorMotionVelocityToZero    = (uint16_t) atoi (argv[7]);
        gMmwMssMCB.sigProcChainCfg.minorMotionVelocityInclusionThr    = (float) atof (argv[8]);
    }
    #else
    {
        gMmwMssMCB.sigProcChainCfg.azimuthFftSize                  = CLI_SIGPROC_AZ_FFT_SIZE;
        gMmwMssMCB.sigProcChainCfg.elevationFftSize                = CLI_SIGPROC_EL_FFT_SIZE;
        gMmwMssMCB.sigProcChainCfg.motDetMode                      = CLI_SIGPROC_MOTDETMODE;
        gMmwMssMCB.sigProcChainCfg.coherentDoppler                 = CLI_SIGPROC_COHERENT_DOPP;
        gMmwMssMCB.sigProcChainCfg.numFrmPerMinorMotProc           = CLI_SIGPROC_NUM_FRAMES_MINORMOT;
        gMmwMssMCB.sigProcChainCfg.numMinorMotionChirpsPerFrame    = CLI_SIGPROC_NUM_CHIRPS_MINORMOT;
        gMmwMssMCB.sigProcChainCfg.forceMinorMotionVelocityToZero  = CLI_SIGPROC_MM_ZERO_VELOCITY;
        gMmwMssMCB.sigProcChainCfg.minorMotionVelocityInclusionThr = CLI_SIGPROC_MM_VELOCITY_THRESH;
    }
    #endif

    if (gMmwMssMCB.sigProcChainCfg.motDetMode == 1)
    {
        gMmwMssMCB.enableMajorMotion = 1;
        gMmwMssMCB.enableMinorMotion = 0;
    }
    else if (gMmwMssMCB.sigProcChainCfg.motDetMode == 2)
    {
        gMmwMssMCB.enableMajorMotion = 0;
        gMmwMssMCB.enableMinorMotion = 1;
    }
    else if (gMmwMssMCB.sigProcChainCfg.motDetMode == 3)
    {
        gMmwMssMCB.enableMajorMotion = 1;
        gMmwMssMCB.enableMinorMotion = 1;
    }
    else
    {
        CLI_write ("Error: Motion Detection mode not valid\n");
        return -1;
    }

    return 0;
}

static int32_t CLI_MMWaveCfarCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 13)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.cfarCfg.averageMode       = (uint8_t) atoi (argv[1]);
        gMmwMssMCB.cfarCfg.winLen            = (uint8_t) atoi (argv[2]);
        gMmwMssMCB.cfarCfg.guardLen          = (uint8_t) atoi (argv[3]);
        gMmwMssMCB.cfarCfg.noiseDivShift     = (uint8_t) atoi (argv[4]);
        gMmwMssMCB.cfarCfg.cyclicMode        = (uint8_t) atoi (argv[5]);
        gMmwMssMCB.cfarCfg.threshold_dB      = (float) atof (argv[6]);
        gMmwMssMCB.cfarCfg.peakGroupingEn    = (uint8_t) atoi (argv[7]);

        gMmwMssMCB.cfarCfg.sideLobeThresholdScaleQ8 = (int16_t) (atof (argv[8]) * DPU_CFARPROCHWA_ONE_Q8);
        gMmwMssMCB.cfarCfg.enableLocalMaxRange      = (uint8_t) atoi (argv[9]);
        gMmwMssMCB.cfarCfg.enableLocalMaxAzimuth    = (uint8_t) atoi (argv[10]);
        gMmwMssMCB.cfarCfg.enableInterpRangeDom     = (uint8_t) atoi (argv[11]);
        gMmwMssMCB.cfarCfg.enableInterpAzimuthDom   = (uint8_t) atoi (argv[12]);
    }
    #else
    {
        gMmwMssMCB.cfarCfg.averageMode       = (uint8_t) CLI_CFARCFG_AVGMODE;
        gMmwMssMCB.cfarCfg.winLen            = (uint8_t) CLI_CFARCFG_WINLEN;
        gMmwMssMCB.cfarCfg.guardLen          = (uint8_t) CLI_CFARCFG_GUARDLEN;
        gMmwMssMCB.cfarCfg.noiseDivShift     = (uint8_t) CLI_CFARCFG_NOISEDIVSHIFT;
        gMmwMssMCB.cfarCfg.cyclicMode        = (uint8_t) CLI_CFARCFG_CYCLICMODE;
        gMmwMssMCB.cfarCfg.threshold_dB      = (float) CLI_CFARCFG_THRESHOLD;
        gMmwMssMCB.cfarCfg.peakGroupingEn    = (uint8_t) CLI_CFARCFG_PEAKGROUP_EN;

        gMmwMssMCB.cfarCfg.sideLobeThresholdScaleQ8 = (int16_t) CLI_CFARCFG_SIDELOBE_THRESH;
        gMmwMssMCB.cfarCfg.enableLocalMaxRange      = (uint8_t) CLI_CFARCFG_LOCMAX_RNG_EN;
        gMmwMssMCB.cfarCfg.enableLocalMaxAzimuth    = (uint8_t) CLI_CFARCFG_LOCMAX_AZI_EN;
        gMmwMssMCB.cfarCfg.enableInterpRangeDom     = (uint8_t) CLI_CFARCFG_INTERP_RNG_EN;
        gMmwMssMCB.cfarCfg.enableInterpAzimuthDom   = (uint8_t) CLI_CFARCFG_INTERP_AZI_EN;
    }
    #endif

    if (gMmwMssMCB.cfarCfg.threshold_dB > 100.0)
    {
        CLI_write("Error: Maximum value for CFAR thresholdScale is 100.0 dB.\n");
        return -1;
    }

    return 0;
}

static int32_t CLI_MMWaveAoaCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 5)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.fovCfg.minAzimuthDeg      = (float) atoi (argv[1]);
        gMmwMssMCB.fovCfg.maxAzimuthDeg      = (float) atoi (argv[2]);
        gMmwMssMCB.fovCfg.minElevationDeg    = (float) atoi (argv[3]);
        gMmwMssMCB.fovCfg.maxElevationDeg    = (float) atoi (argv[4]);
    }
    #else
    {
        gMmwMssMCB.fovCfg.minAzimuthDeg      = CLI_AOACFG_MIN_AZ;
        gMmwMssMCB.fovCfg.maxAzimuthDeg      = CLI_AOACFG_MAX_AZ;
        gMmwMssMCB.fovCfg.minElevationDeg    = CLI_AOACFG_MIN_EL;
        gMmwMssMCB.fovCfg.maxElevationDeg    = CLI_AOACFG_MAX_EL;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveRangeSelCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 3)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.rangeSelCfg.min               = (float) atof (argv[1]);
        gMmwMssMCB.rangeSelCfg.max               = (float) atof (argv[2]);
    }
    #else
    {
        gMmwMssMCB.rangeSelCfg.min = CLI_RANGE_SEL_MIN;
        gMmwMssMCB.rangeSelCfg.max = CLI_RANGE_SEL_MAX;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveClutterRemoval (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 2)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.staticClutterRemovalEnable          = (uint32_t) atoi (argv[1]);
    }
    #else
    {
        gMmwMssMCB.staticClutterRemovalEnable          = CLI_STATIC_CLUTTER_REM_EN;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveCompRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[])
{
    int32_t Re, Im;
    int32_t i;

    #if (CLI_REMOVAL == 0)
    {
        DPU_DoaProc_compRxChannelBiasCfg   cfg;
        int32_t argInd;

        /* Sanity Check: Minimum argument check */
        if (argc != (1+1+SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2))
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Initialize configuration: */
        memset ((void *)&cfg, 0, sizeof(cfg));

        /* Populate configuration: */
        cfg.rangeBias          = (float) atof (argv[1]);

        argInd = 2;
        for (i=0; i < SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL; i++)
        {
            Re = (int32_t) (atof (argv[argInd++]) * 1048576.);
            MATHUTILS_SATURATE21(Re);
            cfg.rxChPhaseComp[i].real = (int32_t) Re;

            Im = (int32_t) (atof (argv[argInd++]) * 1048576.);
            MATHUTILS_SATURATE21(Im);
            cfg.rxChPhaseComp[i].imag = (int32_t) Im;

        }
        /* Save Configuration to use later */
        memcpy((void *) &gMmwMssMCB.compRxChannelBiasCfg, &cfg, sizeof(cfg));
    }
    #else
    {
        float ph;

        /* Range bias and Rx phase compensation */
        gMmwMssMCB.compRxChannelBiasCfg.rangeBias          = (float) CLI_COMP_RANGE_BIAS;
        ph = 1.0;
        for (i=0; i < SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL; i++)
        {
            Re = (int32_t) (ph * 1048576.);
            MATHUTILS_SATURATE21(Re);
            gMmwMssMCB.compRxChannelBiasCfg.rxChPhaseComp[i].real = (int32_t) Re;

            if(gMmwMssMCB.isDevAOP != 1u)
            {
                ph = ph * -1.0;
            }

            Im = (int32_t) (0.0 * 1048576.);
            MATHUTILS_SATURATE21(Im);
            gMmwMssMCB.compRxChannelBiasCfg.rxChPhaseComp[i].imag = (int32_t) Im;
        }
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveSensorPositionCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 6)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        gMmwMssMCB.sceneryParams.sensorPosition.x           = (float) atof (argv[1]);
        gMmwMssMCB.sceneryParams.sensorPosition.y           = (float) atof (argv[2]);
        gMmwMssMCB.sceneryParams.sensorPosition.z           = (float) atof (argv[3]);
        
        gMmwMssMCB.sceneryParams.sensorOrientation.azimTilt      = (float) atof (argv[4]);
        gMmwMssMCB.sceneryParams.sensorOrientation.elevTilt    = (float) atof (argv[5]);
    }
    #endif
    
    #if (MPD_ENABLE == 1)
    {
        gMmwMssMCB.sceneryParams.sensorPosition.x           = (float) CLI_SENSOR_POS_X;
        gMmwMssMCB.sceneryParams.sensorPosition.y           = (float) CLI_SENSOR_POS_Y;
        gMmwMssMCB.sceneryParams.sensorPosition.z           = (float) CLI_SENSOR_POS_Z;
        
        gMmwMssMCB.sceneryParams.sensorOrientation.azimTilt      = (float) CLI_SENSOR_POS_AZI;
        gMmwMssMCB.sceneryParams.sensorOrientation.elevTilt    = (float) CLI_SENSOR_POS_ELE;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveMpdBoundaryBox (int32_t argc, char* argv[])
{
    uint8_t i = 0;
    
    /* Sanity Check: Minimum argument check */
    if (argc != 8)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Populate configuration: */
    gMmwMssMCB.sceneryParams.numBoundaryBoxes++;
    
    i = gMmwMssMCB.sceneryParams.numBoundaryBoxes - 1;

    gMmwMssMCB.sceneryParams.boundaryBox[i].x1 = (float) atof (argv[2]);
    gMmwMssMCB.sceneryParams.boundaryBox[i].x2 = (float) atof (argv[3]);
    gMmwMssMCB.sceneryParams.boundaryBox[i].y1 = (float) atof (argv[4]);
    gMmwMssMCB.sceneryParams.boundaryBox[i].y2 = (float) atof (argv[5]);
    gMmwMssMCB.sceneryParams.boundaryBox[i].z1 = (float) atof (argv[6]);
    gMmwMssMCB.sceneryParams.boundaryBox[i].z2 = (float) atof (argv[7]);

    return 0;
}

static int32_t CLI_MMWaveClusterParamCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 4)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        gMmwMssMCB.clusterParamCfg.enabled = atoi (argv[1]);
        gMmwMssMCB.clusterParamCfg.maxDistance = atof (argv[2]);
        gMmwMssMCB.clusterParamCfg.minPoints = atoi (argv[3]);
    }
    #endif

    #if (MPD_ENABLE == 1)
    {
        gMmwMssMCB.clusterParamCfg.enabled = CLI_CLUSTER_ENABLE;
        gMmwMssMCB.clusterParamCfg.maxDistance = CLI_CLUSTER_RADIUS;
        gMmwMssMCB.clusterParamCfg.minPoints = CLI_CLUSTER_MIN_POINTS;
    }
    #endif

    return 0;
}

static int32_t CLI_MMWaveMajorMotionStateCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 9)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        gMmwMssMCB.majorStateParamCfg.pointThre1 = atoi (argv[1]);
        gMmwMssMCB.majorStateParamCfg.pointThre2 = atoi (argv[2]);
        gMmwMssMCB.majorStateParamCfg.snrThre2 = atof (argv[3]);
        gMmwMssMCB.majorStateParamCfg.pointHistThre1 = atoi (argv[4]);
        gMmwMssMCB.majorStateParamCfg.pointHistThre2 = atoi (argv[5]);
        gMmwMssMCB.majorStateParamCfg.snrHistThre2 = atof (argv[6]);
        gMmwMssMCB.majorStateParamCfg.histBufferSize = atoi (argv[7]);
        gMmwMssMCB.majorStateParamCfg.stateExitThre = atoi (argv[8]);
    }
    #endif

    #if CLI_MAJOR_STATE_CFG_EN
    {
        gMmwMssMCB.majorStateParamCfg.pointThre1 = CLI_MAJOR_STATE_POINT_THRE1;
        gMmwMssMCB.majorStateParamCfg.pointThre2 = CLI_MAJOR_STATE_POINT_THRE2;
        gMmwMssMCB.majorStateParamCfg.snrThre2 = CLI_MAJOR_STATE_SNR_THRE2;
        gMmwMssMCB.majorStateParamCfg.pointHistThre1 = CLI_MAJOR_STATE_POINTHIST_THRE1;
        gMmwMssMCB.majorStateParamCfg.pointHistThre2 = CLI_MAJOR_STATE_POINTHIST_THRE2;
        gMmwMssMCB.majorStateParamCfg.snrHistThre2 = CLI_MAJOR_STATE_SNRHIST_THRE2;
        gMmwMssMCB.majorStateParamCfg.histBufferSize = CLI_MAJOR_STATE_HISTBUFF_SIZE;
        gMmwMssMCB.majorStateParamCfg.stateExitThre = CLI_MAJOR_STATE_EXIT_THRE;
    }
    #endif

    gMmwMssMCB.isMotionPresenceDpuEnabled = 1;
    return 0;
}

static int32_t CLI_MMWaveMinorMotionStateCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 9)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        gMmwMssMCB.minorStateParamCfg.pointThre1 = atoi (argv[1]);
        gMmwMssMCB.minorStateParamCfg.pointThre2 = atoi (argv[2]);
        gMmwMssMCB.minorStateParamCfg.snrThre2 = atof (argv[3]);
        gMmwMssMCB.minorStateParamCfg.pointHistThre1 = atoi (argv[4]);
        gMmwMssMCB.minorStateParamCfg.pointHistThre2 = atoi (argv[5]);
        gMmwMssMCB.minorStateParamCfg.snrHistThre2 = atof (argv[6]);
        gMmwMssMCB.minorStateParamCfg.histBufferSize = atoi (argv[7]);
        gMmwMssMCB.minorStateParamCfg.stateExitThre = atoi (argv[8]);
    }
    #endif

    #if CLI_MINOR_STATE_CFG_EN
    {
        gMmwMssMCB.minorStateParamCfg.pointThre1 = CLI_MINOR_STATE_POINT_THRE1;
        gMmwMssMCB.minorStateParamCfg.pointThre2 = CLI_MINOR_STATE_POINT_THRE2;
        gMmwMssMCB.minorStateParamCfg.snrThre2 = CLI_MINOR_STATE_SNR_THRE2;
        gMmwMssMCB.minorStateParamCfg.pointHistThre1 = CLI_MINOR_STATE_POINTHIST_THRE1;
        gMmwMssMCB.minorStateParamCfg.pointHistThre2 = CLI_MINOR_STATE_POINTHIST_THRE2;
        gMmwMssMCB.minorStateParamCfg.snrHistThre2 = CLI_MINOR_STATE_SNRHIST_THRE2;
        gMmwMssMCB.minorStateParamCfg.histBufferSize = CLI_MINOR_STATE_HISTBUFF_SIZE;
        gMmwMssMCB.minorStateParamCfg.stateExitThre = CLI_MINOR_STATE_EXIT_THRE;
    }
    #endif

    gMmwMssMCB.isMotionPresenceDpuEnabled = 1;
    return 0;
}

int32_t CLI_MMWaveSensorStart (int32_t argc, char* argv[])
{
    int32_t retVal = SystemP_SUCCESS;

    #if (CLI_REMOVAL == 0)
    {
        /* Sanity Check: Minimum argument check */
        if (argc != 5)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Populate the SensorStop configuration: */
        gMmwMssMCB.sensorStart.frameTrigMode      = atoi (argv[1]);
        gMmwMssMCB.sensorStart.chirpStartSigLbEn  = atoi (argv[2]);
        gMmwMssMCB.sensorStart.frameLivMonEn      = atoi (argv[3]);
        gMmwMssMCB.sensorStart.frameTrigTimerVal  = atoi (argv[4]);
    }
    #else
    {
        /* Populate the SensorStart configuration: */
        gMmwMssMCB.sensorStart.frameTrigMode      = CLI_SENSOR_START_FRM_TRIG;
        gMmwMssMCB.sensorStart.chirpStartSigLbEn  = CLI_SENSOR_START_LB_EN;
        gMmwMssMCB.sensorStart.frameLivMonEn      = CLI_SENSOR_START_MON_EN;
        gMmwMssMCB.sensorStart.frameTrigTimerVal  = CLI_SENSOR_START_TRIG_TIMER;
    }
    #endif

    if((gMmwMssMCB.adcLogging.enable == 1) && ((gMmwMssMCB.profileComCfg.h_NumOfAdcSamples % 4) !=0))
    {
        CLI_write ("Error: When RDIF is enabled number of adc samples must be a multiple of 4\r\n");
        retVal = SystemP_FAILURE;
    }

    /* Calculate CRD NSLOPE Magnitude */
    {
        float scale, rfBandwidth, rampDownTime;
#ifdef SOC_XWRL64XX
        scale = 65536./(3*100*100);
#else
        scale = 65536./(4*100*100);
#endif
        rfBandwidth = (gMmwMssMCB.profileComCfg.h_ChirpRampEndTime*0.1) * gMmwMssMCB.chirpSlope; //In MHz/usec
        rampDownTime = MIN((gMmwMssMCB.profileTimeCfg.h_ChirpIdleTime*0.1-1.0), 6.0); //In usec
        gMmwMssMCB.profileComCfg.h_CrdNSlopeMag = (uint16_t) fabs((scale * rfBandwidth / rampDownTime + 0.5));
    }

    /* Chirp Timing Configuration */
    {
        gMmwMssMCB.profileTimeCfg.h_ChirpTxEnSel       = gMmwMssMCB.channelCfg.h_TxChCtrlBitMask;
        gMmwMssMCB.profileTimeCfg.h_ChirpTxBpmEnSel    = 0U;
    }

    /* Check for Rx Saturation on 6432 AOP devices */
    #if defined (SOC_XWRL64XX)
    if(gMmwMssMCB.isDevAOP == 1u)
    {
        float rxSatFactor = (gMmwMssMCB.factoryCalCfg.rxGain - (gMmwMssMCB.factoryCalCfg.txBackoffSel/2) - (6 * gMmwMssMCB.profileComCfg.c_ChirpRxHpfSel) + 
                            (20 * log10((gMmwMssMCB.chirpSlope/100))));
        /* The case of c_ChirpTxMimoPatSel = 0 is grouped with 1 as h_ChirpTxBpmEnSel is hardcoded to 0 in this demo implementation */
        if(gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel == 4)
        {
            CLI_write ("Warning! BPM is not supported on this device\r\n");
        }
        else if(gMmwMssMCB.startFreq<61)
        {
            if((gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel <= 1) && (rxSatFactor > 23.5)) 
                {
                    CLI_write ("Warning! Be cautious about ADC saturation when using this config on this device\r\n");
                }
        }
        else
        {
            if((gMmwMssMCB.profileComCfg.c_ChirpTxMimoPatSel <= 1) && (rxSatFactor > 30.5)) 
                {
                    CLI_write ("Warning! Be cautious about ADC saturation when using this config on this device\r\n");
                }
        }  
    }
    #endif
    /* FEC Power Config */
    {
        /* RDIF clock enable control.*/
        gMmwMssMCB.channelCfg.c_MiscCtrl = 1U << M_RL_RF_MISC_CTRL_RDIF_CLK;
    }

    /* FECSS RF Power ON*/
    retVal = rl_fecssRfPwrOnOff(M_DFP_DEVICE_INDEX_0, &gMmwMssMCB.channelCfg);
    if(retVal != M_DFP_RET_CODE_OK)
    {
        CLI_write ("Error: FECSS RF Power ON/OFF failed\r\n");
        retVal = SystemP_FAILURE;
        MmwDemo_debugAssert (0);
    }
    #if (ENABLE_MONITORS==1)
        // Enable monitors if requested via CFG
        if(gMmwMssMCB.rfMonEnbl != 0)
        {
            mmwDemo_MonitorConfig();
        }
    #endif
    /*Configure Live Monitors*/
    if(gMmwMssMCB.sensorStart.frameLivMonEn !=0)
    {
        mmwDemo_LiveMonConfig();
    }
    /* Perform factory Calibrations. */
    retVal = mmwDemo_factoryCal();
    if(retVal != SystemP_SUCCESS)
    {
        CLI_write ("Error: mmWave factory calibration failed\r\n");
        retVal = SystemP_FAILURE;
    }
    isSensorStarted = 1;

    #if (CLI_REMOVAL == 0 && DYNAMIC_RECONFIG == 1)
    if(gMmwMssMCB.profileSwitchCfg.switchCfgEnable)
    {
        if(!gMmwMssMCB.trackerCfg.staticCfg.trackerEnabled)
        {
            gUserCfgPresenceTrack = 1;
        }
        else 
        {
            gUserCfgPresenceTrack = 0;
        }
        mmwDemo_UserConfigStore();
    }
    #endif

    CLI_MMWStart();

    return retVal;
}

static int32_t CLI_MMWaveFactoryCalConfig (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        if (argc != 6)
        {
            CLI_write ("Error: Invalid usage of the CLI command\r\n");
            return -1;
        }

        /* Populate configuration: */
        gMmwMssMCB.factoryCalCfg.saveEnable = (uint32_t) atoi(argv[1]);
        gMmwMssMCB.factoryCalCfg.restoreEnable = (uint32_t) atoi(argv[2]);
        gMmwMssMCB.factoryCalCfg.rxGain = (uint32_t) atoi(argv[3]);
        /* Front End Firmware expects in 0.5 dB resolution, hence multiplying by 2 */
        gMmwMssMCB.factoryCalCfg.txBackoffSel = (uint32_t)(2 * atoi(argv[4]));
        sscanf(argv[5], "0x%x", &gMmwMssMCB.factoryCalCfg.flashOffset);
    }
    #else
    {
        gMmwMssMCB.factoryCalCfg.saveEnable = CLI_FACCALCFG_SAVE_EN;
        gMmwMssMCB.factoryCalCfg.restoreEnable = CLI_FACCALCFG_RES_EN;
        gMmwMssMCB.factoryCalCfg.rxGain = CLI_FACCALCFG_RX_GAIN;
        gMmwMssMCB.factoryCalCfg.txBackoffSel = CLI_FACCALCFG_TX_BACKOFF_SEL;
        gMmwMssMCB.factoryCalCfg.flashOffset = CLI_FACCALCFG_FLASH_OFFSET;
    }
    #endif

    /* Validate inputs */
    /* <Save> and <re-store> shouldn't be enabled in CLI*/
    if ((gMmwMssMCB.factoryCalCfg.saveEnable == 1) && (gMmwMssMCB.factoryCalCfg.restoreEnable == 1))
    {
        CLI_write ("Error: Save and Restore can be enabled only one at a time\r\n");
        return -1;
    }

    /* Validate inputs */
    /* RxGain should be between 30db to 40db */
    if ( (gMmwMssMCB.factoryCalCfg.rxGain > 40U) || (gMmwMssMCB.factoryCalCfg.rxGain < 30U))
    {
        CLI_write ("Error: Valid RxGain should be between 30db to 40db\r\n");
        return -1;
    }

    /* txBackoffSel should be between 0db to 26db */
    if ((uint32_t) (gMmwMssMCB.factoryCalCfg.txBackoffSel/2) > 26U)
    {
        CLI_write ("Error: Valid txBackoffSel should be between 0db to 26db\r\n");
        return -1;
    }

    /* This check if to avoid accedently courrupt OOB Demo image. */
    if(gMmwMssMCB.factoryCalCfg.flashOffset < MMWDEMO_CALIB_FLASH_ADDR_1MB)
    {
        CLI_write ("Error: Valid flashOffset should be greater than 0x100000\r\n");
        DebugP_assert(0);
    }

    return 0;
}

static int32_t CLI_MMWaveLowPwrModeEnable(int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        if (argc != 2)
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        gMmwMssMCB.lowPowerMode = atoi (argv[1]);
    }
    #else
    {
        gMmwMssMCB.lowPowerMode = CLI_LOW_POWER_MODE;
    }
    #endif

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for antenna geometry configuration
 *      Arguments are row/column coordinates of the virtual antennas in 
 *      units of lambda/2. The arguments are 
 *      <virtAnt0_row> <virtAnt0_col> <virtAnt1_row> <virtAnt1_col> ... <virtAnt5_row> <virtAnt5_col> <antennaDistanceXdim> <antennaDistanceZdim>
 *      where 
 *           virtAnt0 corresponds to tx0-rx0,
 *           virtAnt1 corresponds to tx0-rx1,
 *           virtAnt2 corresponds to tx0-rx2,
 *           virtAnt3 corresponds to tx1-rx0,
 *           virtAnt4 corresponds to tx1-rx1,
 *           virtAnt5 corresponds to tx1-rx2
 *      The last two parameters are optional and represent antenna spacing (mm)
 *         <antennaDistanceXdim> antenna spacing in X dimension
 *         <antennaDistanceZdim> antenna spacing in Z dimension
 *      If these two arguments are not specified, it is assumed that lambda/d=2 where d is distance beetween antennas.
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t CLI_MmwDemo_AntGeometryCfg (int32_t argc, char* argv[])
{
    #if (CLI_REMOVAL == 0)
    {
        MmwDemo_antennaGeometryCfg   cfg;
        int32_t argInd;
        int32_t i;

        /* Sanity Check: Minimum argument check */
        if ((argc < (1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2)) ||
            (argc > (1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2 + 2)))
        {
            CLI_write ("Error: Invalid usage of the CLI command\n");
            return -1;
        }

        /* Initialize configuration: */
        memset ((void *)&cfg, 0, sizeof(cfg));


        argInd = 1;
        for (i=0; i < SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL; i++)
        {
            cfg.ant[i].row  = (int8_t) atoi(argv[argInd++]);
            cfg.ant[i].col  = (int8_t) atoi(argv[argInd++]);
        }

        /* Check if antenna spacings in X-dimesnsion is present  */
        if (argc > (1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2))
        {
            cfg.antDistanceXdim = atof(argv[1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2 + 0]) * 1e-3; //Saved in meters
        }
        /* Check if antenna spacings in Z-dimension is present  */
        if (argc > (1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2 + 1))
        {
            cfg.antDistanceZdim = atof(argv[1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2 + 1]) * 1e-3; //Saved in meters
        }

        /* Save Configuration to use later */
        memcpy((void *) &gMmwMssMCB.antennaGeometryCfg, &cfg, sizeof(cfg));
    }
    #else
    {
        gMmwMssMCB.antennaGeometryCfg.ant[0].row  = (int8_t) CLI_ANT0_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[0].col  = (int8_t) CLI_ANT0_COL;

        gMmwMssMCB.antennaGeometryCfg.ant[1].row  = (int8_t) CLI_ANT1_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[1].col  = (int8_t) CLI_ANT1_COL;

        gMmwMssMCB.antennaGeometryCfg.ant[2].row  = (int8_t) CLI_ANT2_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[2].col  = (int8_t) CLI_ANT2_COL;

        gMmwMssMCB.antennaGeometryCfg.ant[3].row  = (int8_t) CLI_ANT3_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[3].col  = (int8_t) CLI_ANT3_COL;

        gMmwMssMCB.antennaGeometryCfg.ant[4].row  = (int8_t) CLI_ANT4_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[4].col  = (int8_t) CLI_ANT4_COL;

        gMmwMssMCB.antennaGeometryCfg.ant[5].row  = (int8_t) CLI_ANT5_ROW;
        gMmwMssMCB.antennaGeometryCfg.ant[5].col  = (int8_t) CLI_ANT5_COL;

        gMmwMssMCB.antennaGeometryCfg.antDistanceXdim = (float) CLI_ANT_XDIST;
        gMmwMssMCB.antennaGeometryCfg.antDistanceZdim = (float) CLI_ANT_ZDIST;
    }
    #endif

    return 0;
}

void CLI_write (const char* format, ...)
{
    va_list     arg;
    char        logMessage[256];
    int32_t     sizeMessage;
    UART_Transaction trans;
    UART_Handle uartWriteHandle;

    #if (CLI_REMOVAL == 0)
    uartWriteHandle = gCLI.cfg.UartHandle;
    #else
    uartWriteHandle = gMmwMssMCB.commandUartHandle;
    #endif

    UART_Transaction_init(&trans);

    /* Format the message: */
    va_start (arg, format);
    sizeMessage = vsnprintf (&logMessage[0], sizeof(logMessage), format, arg);
    va_end (arg);

    /* If CLI_write is called before CLI init has happened, return */
    if (uartWriteHandle == NULL)
    {
        return;
    }

    trans.buf   = &logMessage[0U];
    trans.count = sizeMessage;

    /* Log the message on the UART CLI console: */
    /* Blocking Mode: */
    UART_write (uartWriteHandle, &trans);
}

void CLI_init (uint8_t taskPriority)
{
    #if (CLI_REMOVAL == 1)
    {
        int32_t       retVal = 0;
        int32_t       argCnt = 0;
        char* argStr[CLI_MAX_ARGS] = {"CLI Removed"};
        
        #if (MPD_ENABLE == 1) 
        uint8_t       mpd_num = 0U;
        uint8_t       cmdString[12][READ_LINE_BUFSIZE];
        char*         ptrCLICommand;
        char          delimitter[] = " \r\n";
        #endif

        /* Populate the Channel configuration */
        retVal = CLI_MMWaveChannelCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);
        
        /* Populate the Chirp Timing Configuration */
        retVal = CLI_MMWaveChirpTimingCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Chirp Common configuration */
        retVal = CLI_MMWaveChirpCommonCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Frame configuration */
        retVal = CLI_MMWaveFrameCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the GUI monitor configuration */
        retVal = CLI_MMWaveGuiMonSel(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the CFAR configuration */
        retVal = CLI_MMWaveCfarCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Range FOV configuration */
        retVal = CLI_MMWaveRangeSelCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Signal chain configuration */
        retVal = CLI_MMWaveSigProcChainCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Angle FOV configuration */
        retVal = CLI_MMWaveAoaCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the Static clutter removal configuration */
        retVal = CLI_MMWaveClutterRemoval(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the low power mode configuration */
        retVal = CLI_MMWaveLowPwrModeEnable(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the antenna geometry configuration */
        retVal = CLI_MmwDemo_AntGeometryCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the factory calibration configuration */
        retVal = CLI_MMWaveFactoryCalConfig(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the range and phase calibration configuration */
        retVal = CLI_MMWaveCompRangeBiasAndRxChanPhaseCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate mpd configurations */
        #if (MPD_ENABLE == 1)
         
        /* Reset the command string: */
        memset ((void *)&cmdString[0][0], 0, sizeof(cmdString));

        strncpy((char*)&cmdString[0], CLI_MPD_BOUNDARY_BOX1, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[1], CLI_MPD_BOUNDARY_BOX2, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[2], CLI_MPD_BOUNDARY_BOX3, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[3], CLI_MPD_BOUNDARY_BOX4, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[4], CLI_MPD_BOUNDARY_BOX5, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[5], CLI_MPD_BOUNDARY_BOX6, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[6], CLI_MPD_BOUNDARY_BOX7, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[7], CLI_MPD_BOUNDARY_BOX8, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[8], CLI_MPD_BOUNDARY_BOX9, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[9], CLI_MPD_BOUNDARY_BOX10, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[10], CLI_MPD_BOUNDARY_BOX11, READ_LINE_BUFSIZE);
        strncpy((char*)&cmdString[11], CLI_MPD_BOUNDARY_BOX12, READ_LINE_BUFSIZE);

        for(mpd_num = 0U; mpd_num < CLI_NUM_MPD_BOUNDARY_BOX; mpd_num++)
        {
            memset ((void *)&argStr, 0, sizeof(argStr));
            argCnt = 0;
            ptrCLICommand = (char*)&cmdString[mpd_num][0];

            /* The command has been entered we now tokenize the command message */
            while (1)
            {
                /* Tokenize the arguments: */
                argStr[argCnt] = strtok(ptrCLICommand, delimitter);
                if (argStr[argCnt] == NULL)
                    break;

                /* Increment the argument index: */
                argCnt++;
                if (argCnt >= CLI_MAX_ARGS)
                    break;

                /* Reset the command string */
                ptrCLICommand = NULL;
            }
            
            retVal = CLI_MMWaveMpdBoundaryBox(argCnt, argStr);
            DebugP_assert(retVal == 0);
        }
        
        /* Populate the sensor start configuration */
        retVal = CLI_MMWaveSensorPositionCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the sensor start configuration */
        retVal = CLI_MMWaveMinorMotionStateCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the sensor start configuration */
        retVal = CLI_MMWaveMajorMotionStateCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        /* Populate the sensor start configuration */
        retVal = CLI_MMWaveClusterParamCfg(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);

        #endif

        /* Populate the sensor start configuration */
        retVal = CLI_MMWaveSensorStart(argCnt, &argStr[0]);
        DebugP_assert(retVal == 0);
    }
    #else
    {
        CLI_Cfg     cliCfg;
        char        demoBanner[256];
        uint32_t    cnt;

        /* Create Demo Banner to be printed out by CLI */
        sprintf(&demoBanner[0],
                //ML_start
                        "******************************************\r\n" \
                        "%s ML Demo %02d.%02d.%02d.%02d\r\n"  \
                        "******************************************\r\n",
                //ML_end
                            DEVICE_STRING,
                            MMWAVE_SDK_VERSION_MAJOR,
                            MMWAVE_SDK_VERSION_MINOR,
                            MMWAVE_SDK_VERSION_BUGFIX,
                            MMWAVE_SDK_VERSION_BUILD
                );

        /* Initialize the CLI configuration: */
        memset ((void *)&cliCfg, 0, sizeof(CLI_Cfg));

        /* Populate the CLI configuration: */
        cliCfg.cliPrompt                    = "mmwDemo:/>";
        cliCfg.cliBanner                    = demoBanner;
        cliCfg.UartHandle                   = gMmwMssMCB.commandUartHandle;
        cliCfg.taskPriority                 = CLI_TASK_PRIORITY;
        cliCfg.mmWaveHandle                 = gMmwMssMCB.ctrlHandle;
        cliCfg.enableMMWaveExtension        = 1U;
        cliCfg.usePolledMode                = true;
        cliCfg.overridePlatform             = false;
        cliCfg.overridePlatformString       = NULL;

        cnt=0;
        cliCfg.tableEntry[cnt].cmd            = "sensorStop";
        cliCfg.tableEntry[cnt].helpString     = "<FrameStopMode>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveSensorStop;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "channelCfg";
        cliCfg.tableEntry[cnt].helpString     = "<RxChCtrlBitMask> <TxChCtrlBitMask> <MiscCtrl>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveChannelCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "chirpComnCfg";
        cliCfg.tableEntry[cnt].helpString     = "<DigOutputSampRate_Decim> <DigOutputBitsSel> <DfeFirSel> <NumOfAdcSamples> <ChirpTxMimoPatSel> <ChirpRampEndTime> <ChirpRxHpfSel>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveChirpCommonCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "chirpTimingCfg";
        cliCfg.tableEntry[cnt].helpString     = "<ChirpIdleTime> <ChirpAdcSkipSamples> <ChirpTxStartTime> <ChirpRfFreqSlope> <ChirpRfFreqStart>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveChirpTimingCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "frameCfg";
        cliCfg.tableEntry[cnt].helpString     = "<NumOfChirpsInBurst> <NumOfChirpsAccum> <BurstPeriodicity> <NumOfBurstsInFrame> <FramePeriodicity> <NumOfFrames>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveFrameCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "guiMonitor";
        cliCfg.tableEntry[cnt].helpString     = "<pointCloud> <rangeProfile> <noiseProfile> <rangeAzimuthHeatMap> <rangeDopplerHeatMap> <statsInfo> <presenceInfo> <adcSamples> <trackerInfo> <microDopplerInfo> <classifierInfo> <quickEvalInfo>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveGuiMonSel;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "sigProcChainCfg";
        cliCfg.tableEntry[cnt].helpString     = "<azimuthFftSize> <elevationFftSize> <motDetMode> <coherentDoppler> <numFrmPerMinorMotProc> <numMinorMotionChirpsPerFrame> <forceMinorMotionVelocityToZero> <minorMotionVelocityInclusionThr>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveSigProcChainCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "cfarCfg";
        cliCfg.tableEntry[cnt].helpString     = "<procDirection> <averageMode> <winLen> <guardLen> <noiseDiv> <cyclicMode> <thresholdScale> <peakGroupingEn>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveCfarCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "aoaFovCfg";
        cliCfg.tableEntry[cnt].helpString     = "<minAzimuthDeg> <maxAzimuthDeg> <minElevationDeg> <maxElevationDeg>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveAoaCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "rangeSelCfg";
        cliCfg.tableEntry[cnt].helpString     = "<minMeters> <maxMeters>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveRangeSelCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "clutterRemoval";
        cliCfg.tableEntry[cnt].helpString     = "<0-disable, 1-enable>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveClutterRemoval;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "compRangeBiasAndRxChanPhase";
        cliCfg.tableEntry[cnt].helpString     = "<rangeBias> <Re00> <Im00> <Re01> <Im01> <Re02> <Im02> <Re03> <Im03> <Re04> <Im04> <Re05> <Im05> ";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveCompRangeBiasAndRxChanPhaseCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "adcDataSource";
        cliCfg.tableEntry[cnt].helpString     = "<0-DFP, 1-File> <fileName>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveAdcDataSourceCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "adcLogging";
        cliCfg.tableEntry[cnt].helpString     = "<0-disable, 1-enableDCA, 2-enableSPI> <sideBandEnable> <swizzlingMode> <scramblerMode> <laneRate>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveAdcLogging;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "sensorPosition";
        cliCfg.tableEntry[cnt].helpString     = "<xOffset> <yOffset> <zOffset> <azimuthTilt> <elevationTilt>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveSensorPositionCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "mpdBoundaryBox";
        cliCfg.tableEntry[cnt].helpString     = "<zone_number> <xMin> <xMax> <yMin> <yMax> <zMin> <zMax>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMpdBoundaryBox;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "clusterCfg";
        cliCfg.tableEntry[cnt].helpString     = "<0-disable, 1-enable> <cluster_radius> <minPoints>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveClusterParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "majorStateCfg";
        cliCfg.tableEntry[cnt].helpString     = "<pointThre1> <pointThre2> <snrThre2> <pointHistThre1> <pointHistThre2> <snrHistThre2> <histBuffSize> <stateExitThre>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMajorMotionStateCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "minorStateCfg";
        cliCfg.tableEntry[cnt].helpString     = "<pointThre1> <pointThre2> <snrThre2> <pointHistThre1> <pointHistThre2> <snrHistThre2> <histBuffSize> <stateExitThre>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMinorMotionStateCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "sensorStart";
        cliCfg.tableEntry[cnt].helpString     = "<FrameTrigMode> <LoopBackEn> <FrameLivMonEn> <FrameTrigTimerVal>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveSensorStart;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "lowPowerCfg";
        cliCfg.tableEntry[cnt].helpString     = "<LowPowerModeEnable>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveLowPwrModeEnable;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "profileSwitchCfg";
        cliCfg.tableEntry[cnt].helpString     = "<switchCfgEnable> <frmPretoTrack> <frmTracktoPre>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveprofileSwitchCfg;
        cnt++;
    #if (ENABLE_MONITORS==1)
        cliCfg.tableEntry[cnt].cmd            = "enableRFmons";
        cliCfg.tableEntry[cnt].helpString     = "<ListofMonsToEnable>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonsEnable;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monPllCtrlVolt";
        cliCfg.tableEntry[cnt].helpString     = "<ListofVoltagemonitorsToEnable>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonPllCtrlVolt;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monTxRxLbCfg";
        cliCfg.tableEntry[cnt].helpString     = "<TX inst> <Mon Enable Ctrl> <TxRx Code Sel> <Rx Gain code> <Tx Bias Code> <RF FreqGHz> <RF Freq SlopeMhz/us>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonTxRxLbCfg;
        cnt++;


        cliCfg.tableEntry[cnt].cmd            = "monTxnPowCfg";
        cliCfg.tableEntry[cnt].helpString     = "<TX inst> <Tx Bias Sel><Tx Bias Code> <RF FreqGHz> <RF Freq SlopeMhz/us> <TX Backoff>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonTxnPowCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monTxnBBPowCfg";
        cliCfg.tableEntry[cnt].helpString     = "<TX inst> <Tx Bias Sel><Tx Bias Code> <RF FreqGHz> <RF Freq SlopeMhz/us> <TX Backoff>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonTxnBBPowCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monTxnDcSigCfg";
        cliCfg.tableEntry[cnt].helpString     = "<TX inst> <Tx Bias Sel><Tx Bias Code> <RF FreqGHz> <RF Freq SlopeMhz/us> <TX Backoff>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonTxnDcSigCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monRxHpfDcSigCfg";
        cliCfg.tableEntry[cnt].helpString     = "<RF StartFreqGHz> <Mon Enable Ctrl><RX HPF Corner Freq Sel>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonRxHpfDcSigCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "monPmClkDcCfg";
        cliCfg.tableEntry[cnt].helpString     = "<RF StartFreqGHz>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveMonPmClkDcCfg;
        cnt++;
    #endif
        cliCfg.tableEntry[cnt].cmd            = "factoryCalibCfg";
        cliCfg.tableEntry[cnt].helpString     = "<save enable> <restore enable> <rxGain> <backoff0> <Flash offset>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = CLI_MMWaveFactoryCalConfig;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "trackingCfg";
        cliCfg.tableEntry[cnt].helpString     = "<enable> <paramSet> <numPoints> <numTracks> <maxDoppler> <framePeriod>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLITrackingCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "staticBoundaryBox";
        cliCfg.tableEntry[cnt].helpString      = "<X min> <X Max> <Y min> <Y max> <Z min> <Z max>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIStaticBoundaryBoxCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "boundaryBox";
        cliCfg.tableEntry[cnt].helpString      = "<X min> <X Max> <Y min> <Y max> <Z min> <Z max>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIBoundaryBoxCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "gatingParam";// PC: 4 gating volume, Limits are set to 3m in length, 2m in width, 0 no limit in doppler
        cliCfg.tableEntry[cnt].helpString      = "<gating volume> <length> <width> <doppler>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIGatingParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "stateParam";// PC: 10 frames to activate, 5 to forget, 10 active to free, 1000 static to free, 5 exit to free, 6000 sleep to free
        cliCfg.tableEntry[cnt].helpString      = "<det2act> <det2free> <act2free> <stat2free> <exit2free> <sleep2free>";//det2act, det2free, act2free, stat2free, exit2free, sleep2free
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIStateParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "allocationParam";// PC: 250 SNR, 0.1 minimal velocity, 5 points, 1m in distance, 2m/s in velocity
        cliCfg.tableEntry[cnt].helpString      = "<SNRs> <minimal velocity> <points> <in distance> <in velocity>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIAllocationParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "maxAcceleration";
        cliCfg.tableEntry[cnt].helpString      = "<max X acc.> <max Y acc.> <max Z acc.>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemoCLIMaxAccelerationParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "presenceBoundaryBox";
        cliCfg.tableEntry[cnt].helpString      = "<X min> <X Max> <Y min> <Y max> <Z min> <Z max>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIPresenceParamCfg;
        cnt++;

    #ifdef ENABLE_UART_HIGH_BAUD_RATE_DYNAMIC_CFG
        cliCfg.tableEntry[cnt].cmd             = "baudRate";
        cliCfg.tableEntry[cnt].helpString      = "<baudRate>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIChangeUartBaudRate;
        cnt++;
    #endif

        cliCfg.tableEntry[cnt].cmd             = "antGeometryCfg";
        cliCfg.tableEntry[cnt].helpString      = "<row0> <col0> <row1> <col1> <row2> <col2> <row3> <col3> <row4> <col4> <row5> <col5> <antDistX (mm)> <antDistY (mm)>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = CLI_MmwDemo_AntGeometryCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "microDopplerCfg";
        cliCfg.tableEntry[cnt].helpString      = "<enabled> <genApproach> <targetSize> <magnitudeSquared> <circShiftCentroid> <normalizedSpectrum> <interceptThrLowFreq> <interceptThrUpFreq> <specShiftMode>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIMicroDopplerParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd             = "classifierCfg";
        cliCfg.tableEntry[cnt].helpString      = "<enabled> <minNumPntsPerTrack> <missTotFrmThre>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn   = MmwDemo_CLIClassifierParamCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "measureRangeBiasAndRxChanPhase";
        cliCfg.tableEntry[cnt].helpString     = "<enabled> <targetDistance> <searchWin>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIMeasureRangeBiasAndRxChanPhaseCfg;
        cnt++;

        cliCfg.tableEntry[cnt].cmd            = "sensorWarmRst";
        cliCfg.tableEntry[cnt].helpString     = "<Reserved>";
        cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLISensorWarmReset;
        cnt++;

        cnt++;

        /* Open the CLI: */
        if (CLI_open (&cliCfg) < 0)
        {
            DebugP_log ("Error: Unable to open the CLI\r\n");
            return;
        }
    }
    #endif
}

int32_t CLI_MMWStart(void)
{
    #if(ENABLE_GPADC==1U)
    int32_t statenable;
    #endif
    int32_t errCode = 0;
    demoStartTime = PRCMSlowClkCtrGet();

    #ifdef INA228
    I2C_Handle  i2cHandle = gI2cHandle[CONFIG_I2C0];
    #endif

    #if (CLI_REMOVAL == 0)
    if(gMmwMssMCB.adcDataSourceCfg.source == 0)
    #endif
    {
        //CLI_getMMWaveExtensionOpenConfig (&gMmwMssMCB.mmwOpenCfg);
        Mmwave_populateDefaultOpenCfg (&gMmwMssMCB.mmwOpenCfg);
        errCode = MmwDemo_openSensor();
        if(errCode != 0)
        {
            goto exit;
        }
        //CLI_getMMWaveExtensionConfig (&gMmwMssMCB.mmwCtrlCfg);
        Mmwave_populateDefaultChirpControlCfg (&gMmwMssMCB.mmwCtrlCfg); /* regular frame config */
        errCode = MmwDemo_configSensor();
        if(errCode != 0)
        {
            goto exit;
        }
    }

    gDpcTask = xTaskCreateStatic(mmwDemo_dpcTask, /* Pointer to the function that implements the task. */
                                 "dpc_task",      /* Text name for the task.  This is to facilitate debugging only. */
                                 DPC_TASK_STACK_SIZE,   /* Stack depth in units of StackType_t typically uint32_t on 32b CPUs */
                                 NULL,                  /* We are not using the task parameter. */
                                 DPC_TASK_PRI,          /* task priority, 0 is lowest priority, configMAX_PRIORITIES-1 is highest */
                                 gDpcTaskStack,      /* pointer to stack base */
                                 &gDpcTaskObj);         /* pointer to statically allocated task object memory */
    configASSERT(gDpcTask != NULL);


    gTlvTask = xTaskCreateStatic(mmwDemo_TransmitProcessedOutputTask, /* Pointer to the function that implements the task. */
                                "tlv_task",      /* Text name for the task.  This is to facilitate debugging only. */
                                TLV_TASK_STACK_SIZE,   /* Stack depth in units of StackType_t typically uint32_t on 32b CPUs */
                                NULL,                  /* We are not using the task parameter. */
                                TLV_TASK_PRI,          /* task priority, 0 is lowest priority, configMAX_PRIORITIES-1 is highest */
                                gTlvTaskStack,      /* pointer to stack base */
                                &gTlvTaskObj);         /* pointer to statically allocated task object memory */
    configASSERT(gTlvTask != NULL);

    #if (CLI_REMOVAL == 0)
    if(gMmwMssMCB.adcDataSourceCfg.source == 0)
    #else
    /* Wait for DPC config to complete */
    SemaphoreP_pend(&gMmwMssMCB.dpcCfgDoneSemHandle, SystemP_WAIT_FOREVER);
    #endif
    {
        if (!gMmwMssMCB.oneTimeConfigDone)
        {
            /* Config INA sensors. */
            #ifdef INA228
                if(gMmwMssMCB.spiADCStream != 1)
                {
                    SensorConfig(i2cHandle);
                }
            #endif
        }
        
        
        errCode = MmwDemo_startSensor();
        
        #if (ENABLE_GPADC==1U)
        // Enabling GPADC Pins 1 & 2
        statenable=MMWave_enableGPADC(GPADCPIN1_ENABLE|GPADCPIN2_ENABLE);

        if(statenable!=0)
        {
            CLI_write("\r\n GPADC Config Error : %d \r\n",statenable);
        }
        #endif

        if(errCode != 0)
        {
            goto exit;
        }
    }
    #if (CLI_REMOVAL == 0)
    else
    {
        if (!gMmwMssMCB.oneTimeConfigDone)
        {
            gAdcFileTask = xTaskCreateStatic(mmwDemo_adcFileReadTask, /* Pointer to the function that implements the task. */
                                     "adcFileRead_task",      /* Text name for the task.  This is to facilitate debugging only. */
                                     ADC_FILEREAD_TASK_STACK_SIZE,   /* Stack depth in units of StackType_t typically uint32_t on 32b CPUs */
                                     NULL,                  /* We are not using the task parameter. */
                                     ADC_FILEREAD_TASK_PRI,          /* task priority, 0 is lowest priority, configMAX_PRIORITIES-1 is highest */
                                     gAdcFileTaskStack,      /* pointer to stack base */
                                     &gAdcFileTaskObj);         /* pointer to statically allocated task object memory */
            configASSERT(gAdcFileTask != NULL);
        }
    }
    #endif

    if (!gMmwMssMCB.oneTimeConfigDone)
    {
        gMmwMssMCB.oneTimeConfigDone = 1;
    }

exit:
    return errCode;
}

#if (CLI_REMOVAL == 0)
int32_t CLI_open (CLI_Cfg* ptrCLICfg)
{
    uint32_t        index;

    /* Sanity Check: Validate the arguments */
    if (ptrCLICfg == NULL)
        return -1;

    /* Initialize the CLI MCB: */
    memset ((void*)&gCLI, 0, sizeof(CLI_MCB));

    /* Copy over the configuration: */
    memcpy ((void *)&gCLI.cfg, (void *)ptrCLICfg, sizeof(CLI_Cfg));

    /* Cycle through and determine the number of supported CLI commands: */
    for (index = 0; index < CLI_MAX_CMD; index++)
    {
        /* Do we have a valid entry? */
        if (gCLI.cfg.tableEntry[index].cmd == NULL)
        {
            /* NO: This is the last entry */
            break;
        }
        else
        {
            /* YES: Increment the number of CLI commands */
            gCLI.numCLICommands = gCLI.numCLICommands + 1;
        }
    }

    /* Do we have a CLI Prompt specified?  */
    if (gCLI.cfg.cliPrompt == NULL)
        gCLI.cfg.cliPrompt = "CLI:/>";

    /* The CLI provides a help command by default:
     * - Since we are adding this at the end of the table; a user of this module can also
     *   override this to provide its own implementation. */
    gCLI.cfg.tableEntry[gCLI.numCLICommands].cmd           = "help";
    gCLI.cfg.tableEntry[gCLI.numCLICommands].helpString    = NULL;
    gCLI.cfg.tableEntry[gCLI.numCLICommands].cmdHandlerFxn = CLI_help;

    /* Increment the number of CLI commands: */
    gCLI.numCLICommands++;

    gCliTask = xTaskCreateStatic( CLI_task,   /* Pointer to the function that implements the task. */
                                  "cli_task_main", /* Text name for the task.  This is to facilitate debugging only. */
                                  CLI_TASK_STACK_SIZE,  /* Stack depth in units of StackType_t typically uint32_t on 32b CPUs */
                                  NULL,              /* We are not using the task parameter. */
                                  ptrCLICfg->taskPriority,      /* task priority, 0 is lowest priority, configMAX_PRIORITIES-1 is highest */
                                  gCliTskStack,  /* pointer to stack base */
                                  &gCliTaskObj );    /* pointer to statically allocated task object memory */
    configASSERT(gCliTask != NULL);

    return 0;
}
#endif
