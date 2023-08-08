/*
 * Copyright 2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "pin_mux.h"
#include "board.h"
#include "fsl_lpspi_edma.h"
#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
#include "fsl_dmamux.h"
#endif

#include "fsl_common.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* Master related */
#define EXAMPLE_LPSPI_MASTER_BASEADDR              	(LPSPI4)					//was: (LPSPI1) - MaaXBoard has LPSPI4
#define EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE          	(DMAMUX0)
#define EXAMPLE_LPSPI_MASTER_DMA_RX_REQUEST_SOURCE 	kDmaRequestMuxLPSPI4Rx		//was: kDmaRequestMuxLPSPI1Rx
#define EXAMPLE_LPSPI_MASTER_DMA_TX_REQUEST_SOURCE 	kDmaRequestMuxLPSPI4Tx		//was: kDmaRequestMuxLPSPI1Tx
#define EXAMPLE_LPSPI_MASTER_DMA_BASE              	(DMA0)
#define EXAMPLE_LPSPI_MASTER_DMA_RX_CHANNEL        	0U
#define EXAMPLE_LPSPI_MASTER_DMA_TX_CHANNEL        	1U

#define EXAMPLE_LPSPI_MASTER_PCS_FOR_INIT     		(kLPSPI_Pcs0)
#define EXAMPLE_LPSPI_MASTER_PCS_FOR_TRANSFER 		(kLPSPI_MasterPcs0)

#define LPSPI_MASTER_CLK_FREQ (CLOCK_GetFreqFromObs(CCM_OBS_LPSPI1_CLK_ROOT))

#define EXAMPLE_LPSPI_DEALY_COUNT 0xFFFFFU
#define TRANSFER_SIZE     64U     	/* Transfer dataSize */
#define TRANSFER_BAUDRATE 1000000U 	/* Transfer bit rate - 1000k */

#define EXAMPLE_SW_GPIO         BOARD_USER_BUTTON_GPIO
#define EXAMPLE_SW_GPIO_PIN     BOARD_USER_BUTTON_GPIO_PIN
#define EXAMPLE_SW_IRQ          BOARD_USER_BUTTON_IRQ
#define EXAMPLE_GPIO_IRQHandler BOARD_USER_BUTTON_IRQ_HANDLER
#define EXAMPLE_SW_NAME         BOARD_USER_BUTTON_NAME

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/* LPSPI user callback */
void LPSPI_MasterUserCallback(LPSPI_Type *base, lpspi_master_edma_handle_t *handle, status_t status, void *userData);

/*******************************************************************************
 * Variables
 ******************************************************************************/
AT_NONCACHEABLE_SECTION_INIT(uint8_t masterRxData[TRANSFER_SIZE]) = {0};
AT_NONCACHEABLE_SECTION_INIT(uint8_t masterTxData[TRANSFER_SIZE]) = {0};

AT_NONCACHEABLE_SECTION_INIT(lpspi_master_edma_handle_t g_m_edma_handle) = {0};
edma_handle_t lpspiEdmaMasterRxRegToRxDataHandle;
edma_handle_t lpspiEdmaMasterTxDataToTxRegHandle;

#if (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && DEMO_EDMA_HAS_CHANNEL_CONFIG)
extern edma_config_t userConfig;
#else
edma_config_t userConfig = {0};
#endif

volatile bool isTransferCompleted  = false;
volatile uint32_t g_systickCounter = 20U;

volatile bool g_InputSignal = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Interrupt service function of switch.
 */
void EXAMPLE_GPIO_IRQHandler(void)
{
    /* clear the interrupt status */
    GPIO_PortClearInterruptFlags(EXAMPLE_SW_GPIO, 1U << EXAMPLE_SW_GPIO_PIN);
    /* Change state of switch. */
    g_InputSignal = true;
    SDK_ISR_EXIT_BARRIER;
}

void SysTick_Handler(void)
{
    if (g_systickCounter != 0U)
    {
        g_systickCounter--;
    }
}

void LPSPI_MasterUserCallback(LPSPI_Type *base, lpspi_master_edma_handle_t *handle, status_t status, void *userData)
{
    if (status == kStatus_Success)
    {
        PRINTF("This is LPSPI master edma transfer completed callback\r\n");
    }

    isTransferCompleted = true;
}

/*!
 * @brief Main function
 */
int main(void)
{
    uint32_t srcClock_Hz;
    uint32_t errorCount;
    uint32_t loopCount = 1U;
    uint32_t i;
    lpspi_master_config_t masterConfig;
    lpspi_transfer_t masterXfer;

    gpio_pin_config_t sw_config = {
            kGPIO_DigitalInput,
            0,
            kGPIO_IntFallingEdge,
    };

    BOARD_ConfigMPU();
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    /* Init input switch GPIO */
    EnableIRQ(EXAMPLE_SW_IRQ);
    GPIO_PinInit(EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN, &sw_config);
    /* Enable GPIO pin interrupt */
    GPIO_PortEnableInterrupts(EXAMPLE_SW_GPIO, 1U << EXAMPLE_SW_GPIO_PIN);

    PRINTF("\r\n**** LPSPI4 EDMA example ****\r\n");
    PRINTF("This example uses the SPI Master as LPSPI4.\r\n");
    PRINTF("Master runs in EDMA mode. Master sends 1st w/o Rx,\r\n2nd with bi-directional and check\r\n");
    PRINTF("Please make sure you make the correct line connection.\r\nActually, the connection is:\r\n");
    PRINTF("LPSPI4_master -- external slave\r\n");
    PRINTF("   SCLK pin23 --    CLK\r\n");
    PRINTF("   CS0  pin24 --    PCS\r\n");
    PRINTF("   MOSI pin21 --    SIN\r\n");
    PRINTF("   MISO pin19 --    SDO\r\n");
    PRINTF("   GND  pin25 --    GND\r\n");
    PRINTF("Connect MOSI (pin21) with MISO (pin19) - Loopback\r\n");

/*DMA Mux setting and EDMA init*/
#if defined(FSL_FEATURE_SOC_DMAMUX_COUNT) && FSL_FEATURE_SOC_DMAMUX_COUNT
    /* DMA MUX init*/
    DMAMUX_Init(EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE);

    DMAMUX_SetSource(EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE, EXAMPLE_LPSPI_MASTER_DMA_RX_CHANNEL,
                     EXAMPLE_LPSPI_MASTER_DMA_RX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE, EXAMPLE_LPSPI_MASTER_DMA_RX_CHANNEL);

    DMAMUX_SetSource(EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE, EXAMPLE_LPSPI_MASTER_DMA_TX_CHANNEL,
                     EXAMPLE_LPSPI_MASTER_DMA_TX_REQUEST_SOURCE);
    DMAMUX_EnableChannel(EXAMPLE_LPSPI_MASTER_DMA_MUX_BASE, EXAMPLE_LPSPI_MASTER_DMA_TX_CHANNEL);
#endif
    /* EDMA init*/
#if (!defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) || (defined(DEMO_EDMA_HAS_CHANNEL_CONFIG) && !DEMO_EDMA_HAS_CHANNEL_CONFIG))
    /*
     * userConfig.enableRoundRobinArbitration = false;
     * userConfig.enableHaltOnError = true;
     * userConfig.enableContinuousLinkMode = false;
     * userConfig.enableDebugMode = false;
     */
    EDMA_GetDefaultConfig(&userConfig);
#endif
    EDMA_Init(EXAMPLE_LPSPI_MASTER_DMA_BASE, &userConfig);

    /*Master config*/
    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = TRANSFER_BAUDRATE;
    masterConfig.whichPcs = EXAMPLE_LPSPI_MASTER_PCS_FOR_INIT;

    srcClock_Hz = LPSPI_MASTER_CLK_FREQ;
    LPSPI_MasterInit(EXAMPLE_LPSPI_MASTER_BASEADDR, &masterConfig, srcClock_Hz);

    /*Set up lpspi master*/
    memset(&(lpspiEdmaMasterRxRegToRxDataHandle), 0, sizeof(lpspiEdmaMasterRxRegToRxDataHandle));
    memset(&(lpspiEdmaMasterTxDataToTxRegHandle), 0, sizeof(lpspiEdmaMasterTxDataToTxRegHandle));

    EDMA_CreateHandle(&(lpspiEdmaMasterRxRegToRxDataHandle), EXAMPLE_LPSPI_MASTER_DMA_BASE,
                      EXAMPLE_LPSPI_MASTER_DMA_RX_CHANNEL);
    EDMA_CreateHandle(&(lpspiEdmaMasterTxDataToTxRegHandle), EXAMPLE_LPSPI_MASTER_DMA_BASE,
                      EXAMPLE_LPSPI_MASTER_DMA_TX_CHANNEL);
#if defined(FSL_FEATURE_EDMA_HAS_CHANNEL_MUX) && FSL_FEATURE_EDMA_HAS_CHANNEL_MUX
    EDMA_SetChannelMux(EXAMPLE_LPSPI_MASTER_DMA_BASE, EXAMPLE_LPSPI_MASTER_DMA_TX_CHANNEL,
                       DEMO_LPSPI_TRANSMIT_EDMA_CHANNEL);
    EDMA_SetChannelMux(EXAMPLE_LPSPI_MASTER_DMA_BASE, EXAMPLE_LPSPI_MASTER_DMA_RX_CHANNEL,
                       DEMO_LPSPI_RECEIVE_EDMA_CHANNEL);
#endif
    LPSPI_MasterTransferCreateHandleEDMA(EXAMPLE_LPSPI_MASTER_BASEADDR, &g_m_edma_handle, LPSPI_MasterUserCallback,
                                         NULL, &lpspiEdmaMasterRxRegToRxDataHandle,
                                         &lpspiEdmaMasterTxDataToTxRegHandle);

    while (1)
    {
        /* Set up the transfer data */
        for (i = 0U; i < TRANSFER_SIZE; i++)
        {
            masterTxData[i] = (i + loopCount) % 256U;
            masterRxData[i] = 0U;
        }

        /* Print out transmit buffer */
        PRINTF("\r\n Master transmit:\r\n");
        for (i = 0; i < TRANSFER_SIZE; i++)
        {
            /* Print 16 numbers in a line */
            if ((i & 0x0FU) == 0U)
            {
                PRINTF("\r\n");
            }
            PRINTF(" %02X", masterTxData[i]);
        }
        PRINTF("\r\n");

        /*Start master transfer*/
        masterXfer.txData   = masterTxData;
        masterXfer.rxData   = NULL;
        masterXfer.dataSize = TRANSFER_SIZE;
        masterXfer.configFlags =
            EXAMPLE_LPSPI_MASTER_PCS_FOR_TRANSFER | kLPSPI_MasterByteSwap | kLPSPI_MasterPcsContinuous;

        isTransferCompleted = false;
        LPSPI_MasterTransferEDMA(EXAMPLE_LPSPI_MASTER_BASEADDR, &g_m_edma_handle, &masterXfer);

        /* Wait until transfer completed */
        while (!isTransferCompleted)
        {
        }

        /* Delay to wait slave is ready */
        if (SysTick_Config(SystemCoreClock / 1000U))
        {
            while (1)
            {
            }
        }

        /* Delay 20 ms */
        g_systickCounter = 20U;
        while (g_systickCounter != 0U)
        {
        }

        /* Start master transfer, receive data from slave */
        isTransferCompleted = false;
        masterXfer.txData   = masterTxData;			//NULL;
        masterXfer.rxData   = masterRxData;
        masterXfer.dataSize = TRANSFER_SIZE;
        masterXfer.configFlags =
            EXAMPLE_LPSPI_MASTER_PCS_FOR_TRANSFER | kLPSPI_MasterByteSwap | kLPSPI_MasterPcsContinuous;

        LPSPI_MasterTransferEDMA(EXAMPLE_LPSPI_MASTER_BASEADDR, &g_m_edma_handle, &masterXfer);

        /* Wait until transfer completed */
        while (!isTransferCompleted)
        {
        }

        errorCount = 0;
        for (i = 0; i < TRANSFER_SIZE; i++)
        {
            if (masterTxData[i] != masterRxData[i])
            {
            	PRINTF("Tx: %02x != Rx: %02x\r\n", masterTxData[i], masterRxData[i]);
                errorCount++;
            }
        }
        if (errorCount == 0)
        {
            PRINTF("\r\nLPSPI transfer all data matched! \r\n");
            /* Print out receive buffer */
            PRINTF("\r\n Master received:\r\n");
            for (i = 0; i < TRANSFER_SIZE; i++)
            {
                /* Print 16 numbers in a line */
                if ((i & 0x0FU) == 0U)
                {
                    PRINTF("\r\n");
                }
                PRINTF(" %02X", masterRxData[i]);
            }
            PRINTF("\r\n");
        }
        else
        {
            PRINTF("\r\nError occurred in LPSPI transfer !\r\n");
        }

        /* Wait for press any key */
        PRINTF("\r\n Press any key to run again\r\n");
        GETCHAR();
        /* Increase loop count to change transmit buffer */
        loopCount++;

        /* check if USER Button (GPIO INT) was there */
        if (g_InputSignal)
        {
            if (0 == GPIO_PinRead(EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN))
            {
            	PRINTF("\r\n==> %s is pressed\r\n", EXAMPLE_SW_NAME);
            }
            /* Reset state of switch */
            g_InputSignal = false;
        }
    }
}
