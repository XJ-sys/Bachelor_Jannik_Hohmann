################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

CG_TOOL_ROOT := C:/ti/ccs2011/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS

GEN_OPTS__FLAG := 
GEN_CMDS__FLAG := 

ORDERED_OBJS += \
"./ADC_testbuf.o" \
"./dpc.o" \
"./syscfg/ti_dpl_config.o" \
"./syscfg/ti_drivers_config.o" \
"./syscfg/ti_drivers_open_close.o" \
"./syscfg/ti_pinmux_config.o" \
"./syscfg/ti_power_clock_config.o" \
"./syscfg/ti_board_config.o" \
"./syscfg/ti_board_open_close.o" \
"./factory_cal.o" \
"./interrupts.o" \
"./main.o" \
"./mmw_cli.o" \
"./mmw_demo_utils.o" \
"./mmw_flash_cal.o" \
"./mmwave_control_config.o" \
"./monitors.o" \
"./motion_detect.o" \
"./power_management.o" \
"./range_phase_bias_measurement.o" \
"./surface.o" \
"./tracker_utils.o" \
"../linker.cmd" \
$(GEN_CMDS__FLAG) \
-Wl,-lalg_gtrack3d.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lalg_featExtract.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lalg_classifier.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lfreertos.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-ldatapath_tracker3d.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-ldrivers.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lboard.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lcontrol.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lutils.xwrL64xx.m4f.ti-arm-clang.debug.lib \
-Wl,-lmmwavelink_m4.lib \
-Wl,-lfecss_rom_m4.lib \
-Wl,-llibc.a \

-include ../makefile.init

RM := DEL /F
RMDIR := RMDIR /S/Q

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include model/subdir_vars.mk
-include subdir_rules.mk
-include model/subdir_rules.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C55_DEPS)),)
-include $(C55_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(S67_DEPS)),)
-include $(S67_DEPS)
endif
ifneq ($(strip $(S62_DEPS)),)
-include $(S62_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(OPT_DEPS)),)
-include $(OPT_DEPS)
endif
ifneq ($(strip $(C??_DEPS)),)
-include $(C??_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(S??_DEPS)),)
-include $(S??_DEPS)
endif
ifneq ($(strip $(C64_DEPS)),)
-include $(C64_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S64_DEPS)),)
-include $(S64_DEPS)
endif
ifneq ($(strip $(INO_DEPS)),)
-include $(INO_DEPS)
endif
ifneq ($(strip $(CLA_DEPS)),)
-include $(CLA_DEPS)
endif
ifneq ($(strip $(S55_DEPS)),)
-include $(S55_DEPS)
endif
ifneq ($(strip $(SV7A_DEPS)),)
-include $(SV7A_DEPS)
endif
ifneq ($(strip $(C62_DEPS)),)
-include $(C62_DEPS)
endif
ifneq ($(strip $(C67_DEPS)),)
-include $(C67_DEPS)
endif
ifneq ($(strip $(PDE_DEPS)),)
-include $(PDE_DEPS)
endif
ifneq ($(strip $(K_DEPS)),)
-include $(K_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C43_DEPS)),)
-include $(C43_DEPS)
endif
ifneq ($(strip $(S43_DEPS)),)
-include $(S43_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(SA_DEPS)),)
-include $(SA_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
EXE_OUTPUTS += \
surface_classification_demo_xwrL64xx-MATERIAL-DIFF.out 

EXE_OUTPUTS__QUOTED += \
"surface_classification_demo_xwrL64xx-MATERIAL-DIFF.out" 


# All Target
all: $(OBJS) $(CMD_SRCS) $(A_SRCS) $(GEN_CMDS)
	@$(MAKE) --no-print-directory -Onone "surface_classification_demo_xwrL64xx-MATERIAL-DIFF.out"

# Tool invocations
surface_classification_demo_xwrL64xx-MATERIAL-DIFF.out: $(OBJS) $(CMD_SRCS) $(A_SRCS) $(GEN_CMDS)
	@echo 'Building target: "$@"'
	@echo 'Invoking: Arm Linker'
	"C:/ti/ccs2011/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS/bin/tiarmclang.exe" -mcpu=cortex-m4 -mfloat-abi=hard -mlittle-endian -mthumb -O0 -DSOC_XWRL64XX -DGTRACK_3D=1 -DTRACKER_MAX_NUM_TR=10 -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -mno-unaligned-access -Wl,-m"motion_and_presence_detection_demo.Debug.map" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/alg/gtrack/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/alg/featExtract/spectrumBased/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/alg/classifier/targetClassifier/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/kernel/freertos/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/drivers/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/datapath/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/board/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/control/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/source/utils/lib" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/firmware/mmwave_dfp/mmwavelink/lib/xWRLx432" -Wl,-i"C:/ti/MMWAVE_L_SDK_05_05_02_00/firmware/mmwave_dfp/fecsslib/lib/xWRLx432" -Wl,-i"C:/ti/ccs2011/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS/lib" -Wl,--reread_libs -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--xml_link_info="surface_classification_demo_xwrL64xx-MATERIAL-DIFF_linkInfo.xml" -Wl,--ram_model -o "surface_classification_demo_xwrL64xx-MATERIAL-DIFF.out" $(ORDERED_OBJS) $(A_SRCS)
	@echo 'Finished building target: "$@"'
	@echo ' '
	@$(MAKE) --no-print-directory post-build

# Other Targets
clean:
	-$(RM) $(GEN_MISC_FILES__QUOTED)$(GEN_FILES__QUOTED)$(EXE_OUTPUTS__QUOTED)
	-$(RM) "ADC_testbuf.o" "dpc.o" "syscfg\ti_dpl_config.o" "syscfg\ti_drivers_config.o" "syscfg\ti_drivers_open_close.o" "syscfg\ti_pinmux_config.o" "syscfg\ti_power_clock_config.o" "syscfg\ti_board_config.o" "syscfg\ti_board_open_close.o" "factory_cal.o" "interrupts.o" "main.o" "mmw_cli.o" "mmw_demo_utils.o" "mmw_flash_cal.o" "mmwave_control_config.o" "monitors.o" "motion_detect.o" "power_management.o" "range_phase_bias_measurement.o" "surface.o" "tracker_utils.o" 
	-$(RM) "ADC_testbuf.d" "dpc.d" "syscfg\ti_dpl_config.d" "syscfg\ti_drivers_config.d" "syscfg\ti_drivers_open_close.d" "syscfg\ti_pinmux_config.d" "syscfg\ti_power_clock_config.d" "syscfg\ti_board_config.d" "syscfg\ti_board_open_close.d" "factory_cal.d" "interrupts.d" "main.d" "mmw_cli.d" "mmw_demo_utils.d" "mmw_flash_cal.d" "mmwave_control_config.d" "monitors.d" "motion_detect.d" "power_management.d" "range_phase_bias_measurement.d" "surface.d" "tracker_utils.d" 
	-$(RMDIR) $(GEN_MISC_DIRS__QUOTED)
	-@echo 'Finished clean'
	-@echo ' '

post-build:
	-$(MAKE) -C C:\Users\janni\workspace_ccstheia\surface_classification_demo_xwrL64xx-MATERIAL-DIFF -f makefile_ccs_bootimage_gen OUTNAME=surface_classification_demo_xwrL64xx-MATERIAL-DIFF PROFILE=Debug MCU_PLUS_SDK_PATH=C:/ti/MMWAVE_L_SDK_05_05_02_00 CG_TOOL_ROOT=C:/ti/ccs2011/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS CCS_INSTALL_DIR=C:\ti\ccs2011\ccs\ CCS_IDE_MODE=desktop
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets

