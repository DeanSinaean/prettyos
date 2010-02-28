/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "os.h"
#include "pci.h"
#include "ehci.h"
#include "kheap.h"
#include "paging.h"

extern page_directory_t* kernel_pd;

uint32_t opregs;
struct ehci_CapRegs* pCapRegs;
struct ehci_OpRegs*  pOpRegs;

void analyzeEHCI(uint32_t bar)
{
    pCapRegs->CAPLENGTH = *((volatile uint8_t* )(bar + 0x00));
    opregs = pOpRegs->USBCMD = bar + pCapRegs->CAPLENGTH;

    pCapRegs->HCIVERSION = *((volatile uint16_t*)(bar + 0x02));
    printformat(  "HCIVERSION: %x ", pCapRegs->HCIVERSION); // Interface Version Number

    pCapRegs->HCSPARAMS = *((volatile uint32_t*)(bar + 0x04));
    printformat(  "HCSPARAMS: %X ", pCapRegs->HCSPARAMS);   // Structural Parameters

    pCapRegs->HCCPARAMS = *((volatile uint32_t*)(bar + 0x08));
    printformat(  "HCCPARAMS: %X ", pCapRegs->HCCPARAMS);   // Capability Parameters

    if(BYTE2(*((volatile uint32_t*) (bar + 0x08)))==0) printformat("No ext. capabil. "); // Extended Capabilities Pointer

    printformat("\nOpRegs Address: %X ", opregs); // Host Controller Operational Registers
}

void initEHCIHostController()
{
    // Program the CTRLDSSEGMENT register with 4-Gigabyte segment where all of the interface data structures are allocated.
    pOpRegs->CTRLDSSEGMENT = *((volatile uint32_t*)(opregs + 0x10)); // default

    // Write the appropriate value to the USBINTR register to enable the appropriate interrupts.
    // pOpRegs->USBINTR       = *((volatile uint32_t*)(opregs + 0x08)) = 0x3F; // 63 = 00111111b
    pOpRegs->USBINTR       = *((volatile uint32_t*)(opregs + 0x08)) = 0x0; /// TEST

    // Write the base address of the Periodic Frame List to the PERIODICLIST BASE register.
    uint32_t virtualMemoryPERIODICLISTBASE = (uint32_t) malloc(0x1000,PAGESIZE);
    uint32_t physicalMemoryPERIODICLISTBASE = paging_get_phys_addr( kernel_pd, (void*)virtualMemoryPERIODICLISTBASE );
    pOpRegs->PERIODICLISTBASE = *((volatile uint32_t*)(opregs + 0x14)) = physicalMemoryPERIODICLISTBASE;

    // If there are no work items in the periodic schedule,
    // all elements of the Periodic Frame List should have their T-Bits set to 1.
    /// TODO: set T-Bits

    // Write the USBCMD register to set the desired interrupt threshold
    // and turn the host controller ON via setting the Run/Stop bit.
    // Software must not write a one to this field unless the host controller is in the Halted state
    // (i.e. HCHalted in the USBSTS register is a one). Doing so will yield undefined results.
    pOpRegs->USBSTS = (*((volatile uint32_t*)(opregs + 0x04)) |= (1<<12)   ); // set Bit 12
    pOpRegs->USBCMD = (*((volatile uint32_t*)(opregs + 0x00)) |= (0x8<<16) ); // Bits 23-16: 08h, means 8 micro-frames
    pOpRegs->USBCMD = (*((volatile uint32_t*)(opregs + 0x00)) |=  0x1      ); // set Start-Stop-Bit                                                           |=  0x1 );    // Run-Stop-Bit

    // Write a 1 to CONFIGFLAG register to route all ports to the EHCI controller
    pOpRegs->CONFIGFLAG    = *((volatile uint32_t*)(opregs + 0x40)) = 1;

    printformat("\n\nAfter Init of EHCI:");
    printformat("\nCTRLDSSEGMENT:              %X", *((volatile uint32_t*)(opregs + 0x10)) );
    printformat("\nUSBINTR:                    %X", *((volatile uint32_t*)(opregs + 0x08)) );
    printformat("\nPERIODICLISTBASE phys addr: %X", *((volatile uint32_t*)(opregs + 0x14)) );
    printformat("  virt addr: %X", virtualMemoryPERIODICLISTBASE);
    printformat("\nUSBCMD:                     %X", *((volatile uint32_t*)(opregs + 0x00)) );
    printformat("\nCONFIGFLAG:                 %X", *((volatile uint32_t*)(opregs + 0x40)) );
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

