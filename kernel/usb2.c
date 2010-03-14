/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "os.h"
#include "ehci.h"
#include "kheap.h"
#include "paging.h"
#include "sys_speaker.h"
#include "usb2.h"

void testTransfer(uint32_t device, uint8_t port)
{
	settextcolor(3,0);
	printformat("Test transfer at port %d on device address: %d\n", port, device);
    settextcolor(15,0);

 	void* virtualAsyncList = malloc(sizeof(struct ehci_qhd), PAGESIZE);
	uint32_t phsysicalAddr = paging_get_phys_addr(kernel_pd, virtualAsyncList);
	pOpRegs->ASYNCLISTADDR = phsysicalAddr;

	// Create QTDs (in reversed order)
	void* next                = createQTD(0x1, 0x0, 1, 0);	// Handshake is the opposite direction of Data
	next       =     InQTD    = createQTD((uint32_t)next, 0x1, 1, 18); // IN DATA1, 18 byte
	void* firstQTD = SetupQTD = createQTD((uint32_t)next, 0x2, 0,  8); // SETUP DATA0, 8 byte

	// Create QH
	createQH(virtualAsyncList, firstQTD, device);

	// Enable Async...
	printformat("\nEnabling Async Schedule\n");
	pOpRegs->USBCMD = pOpRegs->USBCMD | CMD_ASYNCH_ENABLE /*| CMD_ASYNCH_INT_DOORBELL*/;

	sleepSeconds(2);
	printformat("\n");
	showPacket(InQTDpage0,18);
	showDeviceDesriptor( (struct usb2_deviceDescriptor*)InQTDpage0 );
	sleepSeconds(2);
}

void showDeviceDesriptor(struct usb2_deviceDescriptor* d)
{
   settextcolor(10,0);
   printformat("\nlength:            %d\n",  d->length);
   printformat("descriptor type:   %d\n",    d->descriptorType);
   printformat("USB specification: %d.%d\n", d->bcdUSB>>8, d->bcdUSB&0xFF);     // e.g. 0x0210 means 2.10
   printformat("USB class:         %x\n",    d->deviceClass);
   printformat("USB subclass:      %x\n",    d->deviceSubclass);
   printformat("USB protocol       %x\n",    d->deviceProtocol);
   printformat("max packet size:   %d\n",    d->maxPacketSize);             // MPS0, must be 8,16,32,64
   printformat("vendor:            %x\n",    d->idVendor);
   printformat("product:           %x\n",    d->idProduct);
   printformat("release number:    %d.%d\n", d->bcdDevice>>8, d->bcdDevice&0xFF);  // release of the device
   printformat("manufacturer:      %x\n",    d->manufacturer);
   printformat("product:           %x\n",    d->product);
   printformat("serial number:     %x\n",    d->serialNumber);
   printformat("number of config.: %d\n",    d->bumConfigurations); // number of possible configurations
   settextcolor(15,0);
}

/*
* Copyright (c) 2009 The PrettyOS Project. All rights reserved.
*
* http://www.c-plusplus.de/forum/viewforum-var-f-is-62.html
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
