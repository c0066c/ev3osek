//*****************************************************************************
//
// usb_dev_bulk.c - Main routines for the generic bulk device example.
//
// Copyright (c) 2008-2010 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 6288 of the DK-LM3S9B96 Firmware Package.
//
//*****************************************************************************

#include "ustdlib.h"
#include "uart.h"
#include "hw_uart.h"
#include "hw_types.h"
#include "psc.h"
#include "interrupt.h"
#include "soc_AM1808.h"
#include "hw_psc_AM1808.h"
#include "evmAM1808.h"
#include "raster.h"
#include "grlib.h"
#include "widget.h"
#include "canvas.h"
#include "pushbutton.h"
#include "checkbox.h"
#include "radiobutton.h"
#include "container.h"
#include "slider.h"
#include "usblib.h"
#include "usb.h"
#include "usb-ids.h"
#include "usbdevice.h"
#include "usbdbulk.h"
#include "uartStdio.h"
#include "usb_bulk_structs.h"
#include "hw_usb.h"
#include "delay.h"

//*****************************************************************************
//
//! \addtogroup example_list
//! <h1>USB Generic Bulk Device (usb_dev_bulk)</h1>
//!
//! This example provides a generic USB device offering simple bulk data
//! transfer to and from the host.  The device uses a vendor-specific class ID
//! and supports a single bulk IN endpoint and a single bulk OUT endpoint.
//! Data received from the host is assumed to be ASCII text and it is
//! echoed back with the case of all alphabetic characters swapped.
//!
//! A Windows INF file for the device is provided on the installation CD.  This
//! INF contains information required to install the WinUSB subsystem on
//! WindowsXP and Vista PCs.  WinUSB is a Windows subsystem allowing user mode
//! applications to access the USB device without the need for a
//! vendor-specific kernel mode driver.  The device driver may also be
//! downloaded from http://www.luminarymicro.com/products/software_updates.html
//! as part of the ``Stellaris embedded USB drivers'' package
//! (SW-USB-windrivers).
//!
//! A sample Windows command-line application, usb_bulk_example, illustrating
//! how to connect to and communicate with the bulk device is also provided.
//! The application binary is installed as part of the ``Windows-side examples
//! for USB kits'' package (SW-USB-win) on the installation CD or via download
//! from http://www.luminarymicro.com/products/software_updates.html .  Project
//! files are included to allow the examples to be built using Microsoft
//! VisualStudio.  Source code for this application can be found in directory
//! StellarisWare/tools/usb_bulk_example.
//
//*****************************************************************************

//*****************************************************************************
//
// The system tick rate expressed both as ticks per second and a millisecond
// period.
//
//*****************************************************************************
#define SYSTICKS_PER_SECOND 100
#define SYSTICK_PERIOD_MS (1000 / SYSTICKS_PER_SECOND)



//*****************************************************************************
//
// Graphics context used to show text on the color STN display.
//
//*****************************************************************************

#define TEXT_FONT               &g_sFontCmss22b
#define TEXT_HEIGHT             (GrFontHeightGet(TEXT_FONT))
#define BUFFER_METER_HEIGHT     TEXT_HEIGHT
#define BUFFER_METER_WIDTH      150
#define LCD_SIZE 261156
#define LCD_CLK  150000000
#define PALETTE_SIZE 32
#define LCD_WIDTH 480
#define LCD_HEIGHT 272
#define PALETTE_OFFSET	4


//Graphics context structure
tContext g_sContext;

// Memory that is used as the local frame buffer.
#if defined(__IAR_SYSTEMS_ICC__)
#pragma data_alignment=4
unsigned char g_pucBuffer[GrOffScreen16BPPSize(LCD_WIDTH, LCD_HEIGHT)];
#elif defined __TMS470__ || defined _TMS320C6X
#pragma DATA_ALIGN(g_pucBuffer, 4);
unsigned char g_pucBuffer[GrOffScreen16BPPSize(LCD_WIDTH, LCD_HEIGHT)];
#else
unsigned char g_pucBuffer[GrOffScreen16BPPSize(LCD_WIDTH, LCD_HEIGHT)]__attribute__ ((aligned (4)));
#endif

// The graphics library display structure.
tDisplay g_sSHARP480x272x16Display;

// 32 byte Palette.
unsigned short palette_32b[PALETTE_SIZE/2] = 
			{0x4000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
			 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u};


//*****************************************************************************
//
// The global system tick counter.
//
//*****************************************************************************
volatile unsigned int g_ulSysTickCount = 0;

//*****************************************************************************
//
// Variables tracking transmit and receive counts.
//
//*****************************************************************************
volatile unsigned int g_ulTxCount = 0;
volatile unsigned int g_ulRxCount = 0;
#ifdef DEBUG
unsigned int g_ulUARTRxErrors = 0;
#endif

//*****************************************************************************
//
// Debug-related definitions and declarations.
//
// Debug output is available via UART0 if DEBUG is defined during build.
//
//*****************************************************************************
#ifdef DEBUG
//*****************************************************************************
//
// Map all debug print calls to UARTprintf in debug builds.
//
//*****************************************************************************
#define DEBUG_PRINT UARTprintf

#else

//*****************************************************************************
//
// Compile out all debug print calls in release builds.
//
//*****************************************************************************
#define DEBUG_PRINT while(0) ((int (*)(char *, ...))0)
#endif

//*****************************************************************************
//
// Graphics context used to show text on the color LCD display.
//
//*****************************************************************************
tContext g_sContext;
#define TEXT_FONT               &g_sFontCmss22b

//*****************************************************************************
//
// Flags used to pass commands from interrupt context to the main loop.
//
//*****************************************************************************
#define COMMAND_PACKET_RECEIVED 0x00000001
#define COMMAND_STATUS_UPDATE   0x00000002

volatile unsigned int g_ulFlags = 0;
char *g_pcStatus;

//*****************************************************************************
//
// Global flag indicating that a USB configuration has been set.
//
//*****************************************************************************
static volatile tBoolean g_bUSBConfigured = false;

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, unsigned int ulLine)
{
    UARTprintf("Error at line %d of %s\n", ulLine, pcFilename);
    while(1)
    {
    }
}
#endif

//*****************************************************************************
//
// Interrupt handler for the system tick counter.
//
//*****************************************************************************
void
SysTickIntHandler(void)
{
    //
    // Update our system tick counter.
    //
    g_ulSysTickCount++;
}

/*
** For each end of frame interrupt base and ceiling is reconfigured 
*/
static void LCDIsr(void)
{
    unsigned int  status;

    IntSystemStatusClear(SYS_INT_LCDINT);

    status = RasterIntStatus(SOC_LCDC_0_REGS,RASTER_END_OF_FRAME0_INT_STAT |
                                             RASTER_END_OF_FRAME1_INT_STAT );

    status = RasterClearGetIntStatus(SOC_LCDC_0_REGS, status);    
}

static void SetupAINTCInt(void)
{
    /* Initialize the ARM Interrupt Controller(AINTC). */
    IntAINTCInit();

    /* Enable IRQ in CPSR.*/     
    IntMasterIRQEnable();

    /* Enable the interrupts in GER of AINTC.*/
    IntGlobalEnable();

    /* Enable the interrupts in HIER of AINTC.*/
    IntIRQEnable();

}

/*
** \brief  This function confiugres the AINTC to receive UART interrupts.
*/ 
static void ConfigureAINTCIntUSB(void)
{
    /*
    ** Registers the UARTIsr in the Interrupt Vector Table of AINTC.
    ** The event number of UART2 interrupt is 61.
    */

    IntRegister(SYS_INT_USB0, USB0DeviceIntHandler);

    /*
    ** Map the channel number 2 of AINTC to system interrupt 61.
    ** Channel number 2 of AINTC is mapped to IRQ interrupt of ARM9 processor.
    */
    IntChannelSet(SYS_INT_USB0, 2);

    /* Enable the system interrupt number 61 in AINTC.*/
    IntSystemEnable(SYS_INT_USB0);
}

/*
** Configures raster to display image 
*/
static void SetUpLCD(void)
{
    PSCModuleControl(SOC_PSC_1_REGS, HW_PSC_LCDC, PSC_POWERDOMAIN_ALWAYS_ON,
		     PSC_MDCTL_NEXT_ENABLE);

    LCDPinMuxSetup();

    /* disable raster */
    RasterDisable(SOC_LCDC_0_REGS);
    
    /* configure the pclk */
    RasterClkConfig(SOC_LCDC_0_REGS, 7833600, LCD_CLK);

    /* configuring DMA of LCD controller */ 
    RasterDMAConfig(SOC_LCDC_0_REGS, RASTER_DOUBLE_FRAME_BUFFER,
                    RASTER_BURST_SIZE_16, RASTER_FIFO_THRESHOLD_8,
                    RASTER_BIG_ENDIAN_DISABLE);

    /* configuring modes(ex:tft or stn,color or monochrome etc) for raster controller */
    RasterModeConfig(SOC_LCDC_0_REGS, RASTER_DISPLAY_MODE_TFT,
                     RASTER_PALETTE_DATA, RASTER_COLOR, RASTER_RIGHT_ALIGNED);

    /* frame buffer data is ordered from least to Most significant bye */
    RasterLSBDataOrderSelect(SOC_LCDC_0_REGS);
    
    /* disable nibble mode */
    RasterNibbleModeDisable(SOC_LCDC_0_REGS);
   
     /* configuring the polarity of timing parameters of raster controller */
    RasterTiming2Configure(SOC_LCDC_0_REGS, RASTER_FRAME_CLOCK_LOW |
                                            RASTER_LINE_CLOCK_LOW  |
                                            RASTER_PIXEL_CLOCK_LOW |
                                            RASTER_SYNC_EDGE_RISING|
                                            RASTER_SYNC_CTRL_ACTIVE|
                                            RASTER_AC_BIAS_HIGH     , 0, 255);

    /* configuring horizontal timing parameter */
   RasterHparamConfig(SOC_LCDC_0_REGS, 480, 41, 2, 2);

    /* configuring vertical timing parameters */
   RasterVparamConfig(SOC_LCDC_0_REGS, 272, 10, 3, 3);

   /* configuring fifo delay to */
   RasterFIFODMADelayConfig(SOC_LCDC_0_REGS, 2);
}

/*
** configures arm interrupt controller to generate raster interrupt 
*/
static void SetupIntc(void)
{
    /* Initialize the ARM Interrupt Controller.*/
    IntAINTCInit();

    /* Register the ISR in the Interrupt Vector Table.*/
    IntRegister(SYS_INT_LCDINT, LCDIsr);

    /* Set the channnel number 2 of AINTC for LCD system interrupt.
     */
    IntChannelSet(SYS_INT_LCDINT, 4);

    /* Enable the System Interrupts for AINTC.*/
    IntSystemEnable(SYS_INT_LCDINT);

    /* Enable IRQ in CPSR.*/
    IntMasterIRQEnable();

    /* Enable the interrupts in GER of AINTC.*/
    IntGlobalEnable();

    /* Enable the interrupts in HIER of AINTC.*/
    IntIRQEnable();
}





//*****************************************************************************
//
// Receive new data and echo it back to the host.
//
// \param psDevice points to the instance data for the device whose data is to
// be processed.
// \param pcData points to the newly received data in the USB receive buffer.
// \param ulNumBytes is the number of bytes of data available to be processed.
//
// This function is called whenever we receive a notification that data is
// available from the host. We read the data, byte-by-byte and swap the case
// of any alphabetical characters found then write it back out to be
// transmitted back to the host.
//
// \return Returns the number of bytes of data processed.
//
//*****************************************************************************
static unsigned int
EchoNewDataToHost(tUSBDBulkDevice *psDevice, unsigned char *pcData,
                  unsigned int ulNumBytes)
{
    unsigned int ulLoop, ulSpace, ulCount;
    unsigned int ulReadIndex;
    unsigned int ulWriteIndex;
    tUSBRingBufObject sTxRing;

    //
    // Get the current buffer information to allow us to write directly to
    // the transmit buffer (we already have enough information from the
    // parameters to access the receive buffer directly).
    //
    USBBufferInfoGet(&g_sTxBuffer, &sTxRing);

    //
    // How much space is there in the transmit buffer?
    //
    ulSpace = USBBufferSpaceAvailable(&g_sTxBuffer);

    //
    // How many characters can we process this time round?
    //
    ulLoop = (ulSpace < ulNumBytes) ? ulSpace : ulNumBytes;
    ulCount = ulLoop;

    //
    // Update our receive counter.
    //
    g_ulRxCount += ulNumBytes;

    //
    // Dump a debug message.
    //
    DEBUG_PRINT("Received %d bytes\n", ulNumBytes);

    //
    // Set up to process the characters by directly accessing the USB buffers.
    //
    ulReadIndex = (unsigned int)(pcData - g_pucUSBRxBuffer);
    ulWriteIndex = sTxRing.ulWriteIndex;

    while(ulLoop)
    {
        //
        // Copy from the receive buffer to the transmit buffer converting
        // character case on the way.
        //

        //
        // Is this a lower case character?
        //
        if((g_pucUSBRxBuffer[ulReadIndex] >= 'a') &&
           (g_pucUSBRxBuffer[ulReadIndex] <= 'z'))
        {
            //
            // Convert to upper case and write to the transmit buffer.
            //
            g_pucUSBTxBuffer[ulWriteIndex] =
                (g_pucUSBRxBuffer[ulReadIndex] - 'a') + 'A';
        }
        else
        {
            //
            // Is this an upper case character?
            //
            if((g_pucUSBRxBuffer[ulReadIndex] >= 'A') &&
               (g_pucUSBRxBuffer[ulReadIndex] <= 'Z'))
            {
                //
                // Convert to lower case and write to the transmit buffer.
                //
                g_pucUSBTxBuffer[ulWriteIndex] =
                    (g_pucUSBRxBuffer[ulReadIndex] - 'Z') + 'z';
            }
            else
            {
                //
                // Copy the received character to the transmit buffer.
                //
                g_pucUSBTxBuffer[ulWriteIndex] = g_pucUSBRxBuffer[ulReadIndex];
            }
        }

        //
        // Move to the next character taking care to adjust the pointer for
        // the buffer wrap if necessary.
        //
        ulWriteIndex++;
        ulWriteIndex = (ulWriteIndex == BULK_BUFFER_SIZE) ? 0 : ulWriteIndex;

        ulReadIndex++;
        ulReadIndex = (ulReadIndex == BULK_BUFFER_SIZE) ? 0 : ulReadIndex;

        ulLoop--;
    }

    //
    // We've processed the data in place so now send the processed data
    // back to the host.
    //
    USBBufferDataWritten(&g_sTxBuffer, ulCount);

    DEBUG_PRINT("Wrote %d bytes\n", ulCount);

    //
    // We processed as much data as we can directly from the receive buffer so
    // we need to return the number of bytes to allow the lower layer to
    // update its read pointer appropriately.
    //
    return(ulCount);
}

//*****************************************************************************
//
// Shows the status string on the color STN display.
//
// \param psContext is a pointer to the graphics context representing the
// display.
// \param pcStatus is a pointer to the string to be shown.
//
//*****************************************************************************
void
DisplayStatus(tContext *psContext, char *pcStatus)
{
    tRectangle rectLine;
    int lY;

    //
    // Calculate the Y coordinate of the top left of the character cell
    // for our line of text.
    //
    lY = (GrContextDpyHeightGet(psContext) / 4) -
         (GrFontHeightGet(TEXT_FONT) / 2);

    //
    // Determine the bounding rectangle for this line of text. We add 4 pixels
    // to the height just to ensure that we clear a couple of pixels above and
    // below the line of text.
    //
    rectLine.sXMin = 0;
    rectLine.sXMax = GrContextDpyWidthGet(psContext) - 1;
    rectLine.sYMin = lY;
    rectLine.sYMax = lY + GrFontHeightGet(TEXT_FONT) + 3;

    //
    // Clear the line with black.
    //
    GrContextForegroundSet(&g_sContext, ClrBlack);
    GrRectFill(psContext, &rectLine);

    //
    // Draw the new status string
    //
    DEBUG_PRINT("%s\n", pcStatus);
    GrContextForegroundSet(&g_sContext, ClrWhite);
    GrStringDrawCentered(psContext, pcStatus, -1,
                         GrContextDpyWidthGet(psContext) / 2,
                         GrContextDpyHeightGet(psContext) / 4 , false);
}

//*****************************************************************************
//
// Handles bulk driver notifications related to the transmit channel (data to
// the USB host).
//
// \param pvCBData is the client-supplied callback pointer for this channel.
// \param ulEvent identifies the event we are being notified about.
// \param ulMsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the bulk driver to notify us of any events
// related to operation of the transmit data channel (the IN channel carrying
// data to the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
unsigned int
TxHandler(void *pvCBData, unsigned int ulEvent, unsigned int ulMsgValue,
          void *pvMsgData)
{
    //
    // We are not required to do anything in response to any transmit event
    // in this example. All we do is update our transmit counter.
    //
    if(ulEvent == USB_EVENT_TX_COMPLETE)
    {
        g_ulTxCount += ulMsgValue;
    }

    //
    // Dump a debug message.
    //
    DEBUG_PRINT("TX complete %d\n", ulMsgValue);

    return(0);
}

//*****************************************************************************
//
// Handles bulk driver notifications related to the receive channel (data from
// the USB host).
//
// \param pvCBData is the client-supplied callback pointer for this channel.
// \param ulEvent identifies the event we are being notified about.
// \param ulMsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the bulk driver to notify us of any events
// related to operation of the receive data channel (the OUT channel carrying
// data from the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
unsigned int
RxHandler(void *pvCBData, unsigned int ulEvent,
               unsigned int ulMsgValue, void *pvMsgData)
{
    //
    // Which event are we being sent?
    //
    switch(ulEvent)
    {
        //
        // We are connected to a host and communication is now possible.
        //
        case USB_EVENT_CONNECTED:
        {
            g_bUSBConfigured = true;
            g_pcStatus = "Host connected.";
            g_ulFlags |= COMMAND_STATUS_UPDATE;

            //
            // Flush our buffers.
            //
            USBBufferFlush(&g_sTxBuffer);
            USBBufferFlush(&g_sRxBuffer);

            break;
        }

        //
        // The host has disconnected.
        //
        case USB_EVENT_DISCONNECTED:
        {
            g_bUSBConfigured = false;
            g_pcStatus = "Host disconnected.";
            g_ulFlags |= COMMAND_STATUS_UPDATE;
            break;
        }

        //
        // A new packet has been received.
        //
        case USB_EVENT_RX_AVAILABLE:
        {
            tUSBDBulkDevice *psDevice;

            //
            // Get a pointer to our instance data from the callback data
            // parameter.
            //
            psDevice = (tUSBDBulkDevice *)pvCBData;

            //
            // Read the new packet and echo it back to the host.
            //
            return(EchoNewDataToHost(psDevice, pvMsgData, ulMsgValue));
        }

        //
        // Ignore SUSPEND and RESUME for now.
        //
        case USB_EVENT_SUSPEND:
        case USB_EVENT_RESUME:
            break;

        //
        // Ignore all other events and return 0.
        //
        default:
            break;
    }

    return(0);
}

//*****************************************************************************
//
// This is the main application entry function.
//
//*****************************************************************************
int
main(void)
{
    unsigned int ulTxCount;
    unsigned int ulRxCount;
    tRectangle sRect;
    char pcBuffer[16];
    unsigned int i = 0;
	unsigned char *src, *dest;

   
    //
    // Open UART0 for debug output.
    //
   	UARTStdioInit();
   
    //
    // Not configured initially.
    //
    g_bUSBConfigured = false;

	
	//
	//configures arm interrupt controller to generate raster interrupt 
	//
	SetupIntc();

	//
	//Configures raster to display image 
	//
	SetUpLCD();

  	/* configuring the base ceiling */
	RasterDMAFBConfig(SOC_LCDC_0_REGS, 
					  (unsigned int)(g_pucBuffer+PALETTE_OFFSET),
					  (unsigned int)(g_pucBuffer+PALETTE_OFFSET) + sizeof(g_pucBuffer) - 2 - PALETTE_OFFSET,
					  0);

	RasterDMAFBConfig(SOC_LCDC_0_REGS, 
					  (unsigned int)(g_pucBuffer+PALETTE_OFFSET),
					  (unsigned int)(g_pucBuffer+PALETTE_OFFSET) + sizeof(g_pucBuffer) - 2 - PALETTE_OFFSET,
					  1);

	// Copy palette info into buffer
	src = (unsigned char *) palette_32b;
	dest = (unsigned char *) (g_pucBuffer+PALETTE_OFFSET);
	for( i = 4; i < (PALETTE_SIZE+4); i++)
	{
		*dest++ = *src++;
	}

	GrOffScreen16BPPInit(&g_sSHARP480x272x16Display, g_pucBuffer, LCD_WIDTH, LCD_HEIGHT);

	// Initialize a drawing context.
	GrContextInit(&g_sContext, &g_sSHARP480x272x16Display);

	/* enable End of frame interrupt */
	RasterEndOfFrameIntEnable(SOC_LCDC_0_REGS);

	/* enable raster */
	RasterEnable(SOC_LCDC_0_REGS);
	ConfigRasterDisplayEnable();
	
    //
    // Fill the top 15 rows of the screen with blue to create the banner.
    //
  	sRect.sXMin = 0;
    sRect.sYMin = 0;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = 23;
    GrContextForegroundSet(&g_sContext, ClrDarkBlue);
    GrRectFill(&g_sContext, &sRect);

    //
    // Put a white box around the banner.
    //
  	GrContextForegroundSet(&g_sContext, ClrWhite);
    GrRectDraw(&g_sContext, &sRect);

    //
    // Put the application name in the middle of the banner.
    //
    GrContextFontSet(&g_sContext, &g_sFontCm20);
    GrStringDrawCentered(&g_sContext, "usb-dev-bulk", -1,
                         GrContextDpyWidthGet(&g_sContext) / 2, 10, 0);

    //
    // Show the various static text elements on the color STN display.
    //
  	GrContextFontSet(&g_sContext, TEXT_FONT);
    GrStringDraw(&g_sContext, "Tx bytes:", -1, 8, 100, false);
    GrStringDraw(&g_sContext, "Rx bytes:", -1, 8, 130, false);



   
    //
    // Tell the user what we are up to.
    //
  	 DisplayStatus(&g_sContext, " Configuring USB... ");



	//
	//Setup the AINT Controller
	//
	SetupAINTCInt();	

	//
	//Configure the AINTC controller for USB
	//
	ConfigureAINTCIntUSB();

	//
	//Delay timer setup
	//
	DelayTimerSetup();
	
    //
    // Initialize the transmit and receive buffers.
    //
    USBBufferInit((tUSBBuffer *)&g_sTxBuffer);
    USBBufferInit((tUSBBuffer *)&g_sRxBuffer);

    //
    // Pass our device information to the USB library and place the device
    // on the bus.
    //
    USBDBulkInit(0, (tUSBDBulkDevice *)&g_sBulkDevice);

    //
    // Wait for initial configuration to complete.
    //
   	DisplayStatus(&g_sContext, "Waiting for host...");

    //
    // Clear our local byte counters.
    //
    ulRxCount = 0;
    ulTxCount = 0;

    //
    // Main application loop.
    //
    while(1)
    {

        //
        // Have we been asked to update the status display?
        //
        if(g_ulFlags & COMMAND_STATUS_UPDATE)
        {
            //
            // Clear the command flag
            //
            g_ulFlags &= ~COMMAND_STATUS_UPDATE;
           	DisplayStatus(&g_sContext, g_pcStatus);
        }

        //
        // Has there been any transmit traffic since we last checked?
        //
        if(ulTxCount != g_ulTxCount)
        {
            //
            // Take a snapshot of the latest transmit count.
            //
            ulTxCount = g_ulTxCount;

            //
            // Update the display of bytes transmitted by the UART.
            //
            usnprintf(pcBuffer, 16, " %d ", ulTxCount);
            GrStringDraw(&g_sContext, pcBuffer, -1, 120, 100, true);
        }

        //
        // Has there been any receive traffic since we last checked?
        //
        if(ulRxCount != g_ulRxCount)
        {
            //
            // Take a snapshot of the latest receive count.
            //
            ulRxCount = g_ulRxCount;

            //
            // Update the display of bytes received by the UART.
            //
            usnprintf(pcBuffer, 16, " %d ", ulRxCount);
          	GrStringDraw(&g_sContext, pcBuffer, -1, 120, 130, true);
        }
    }
}
