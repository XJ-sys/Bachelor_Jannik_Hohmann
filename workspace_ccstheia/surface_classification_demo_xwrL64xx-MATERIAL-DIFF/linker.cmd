
/* make sure below retain is there in your linker command file, it keeps the vector table in the final binary */
--retain="*(.vectors)"
/* This is the stack that is used by code running within main()
 * In case of NORTOS,
 * - This means all the code outside of ISR uses this stack
 * In case of FreeRTOS
 * - This means all the code until vTaskStartScheduler() is called in main()
 *   uses this stack.
 * - After vTaskStartScheduler() each task created in FreeRTOS has its own stack
 */
--stack_size=8192
/* This is the heap size for malloc() API in NORTOS and FreeRTOS
 * This is also the heap used by pvPortMalloc in FreeRTOS
 */
--heap_size=12288


SECTIONS
{
    /* This has the M4F entry point and vector table, this MUST be at 0x0 */
    .vectors:{} palign(8) > M4F_VECS
    .bss:    {} palign(8) > M4F_RAM12     			/* This is where uninitialized globals go */
    RUN_START(__BSS_START)
    RUN_END(__BSS_END)
    .text:   {} align(8)  >> M4F_RAM12 | M4F_RAM3    /* This is where code resides */
    .data:   {} align(8)  >> M4F_RAM12 | M4F_RAM3    /* This is where initialized globals and static go */
    .rodata: {} align(8)  >  M4F_RAM0_S		     	/* This is where const's go */	//ML_start
    .sysmem: {} palign(8) >  M4F_RBL     			/* This is where the malloc heap goes */
    .stack:  {} palign(8) >  M4F_RBL     			/* This is where the main() stack goes */
    .l3:     {} palign(8) > HWASS_SHM_MEM     		/* This is where L3 data goes */
}

MEMORY
{
    M4F_VECS   (RX) : ORIGIN = 0x00400000 , LENGTH = 0x00000200
    M4F_RAM12  (RWX): ORIGIN = 0x00400200 , LENGTH = (0x00058000 - 0x200) 	/* 32KB of RAM2 is being used by RBL */
    M4F_RBL    (X)  : ORIGIN = 0x00458000 , LENGTH = 0x8000 				/* 32KB of RAM2 is being used by RBL */
    M4F_RAM3   (RWX): ORIGIN = 0x00460000 , LENGTH = 0x00020000
    M4F_RAM0_S (R)  : ORIGIN = 0x00480000 , LENGTH = 0x00020000				//shared RAM0 = 128k	//ML_start

    HWASS_SHM_MEM : ORIGIN = 0x60000000, LENGTH = 0x00080000 	/* 256KB in APPSS PD, 96KB in FECSS PD and 160KB in HWA PD */
}
