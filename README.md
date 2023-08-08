# MaaXBoard-RT_lpspi_edma_b2b_transfer_master_cm7
 MaaXBoard-RT LPSPI4 demo plus GPIO INT demo

## LPSPI4 in EDMA mode
This project demonstrates how to initialize and use LPSPI4 as SPI Master,
using EDMA mode.

## GPIO INT demo
It has also a demo how to use GPIO Input Interrupt, using the
User Button.

## Testing SPI
The SPI as LPSI4 is populated on these J1 header pins:
pin 19 : SPI_MISO
pin 21 : SPI_MOSI
pin 23 : SPI_SCLK
pin 24 : SPI_CS0
pin 25 : use as GND

### Attention
If you see the J1 pin assigments in the User Guide - SPI_MOSI and SPI_MISO are flipped.

For testing, you can do this:
connect SPI_MISO with SPI_MOSI (a cable between pin 19 and 21).
It will "mirror" what was sent and results in an error-free comparision of
Tx and Rx.

The GPIO Interrupt is based on pressing the User Button.
There is a message on UART (Debug UART via MCU-LINK used) if the User Button
was pressed.

