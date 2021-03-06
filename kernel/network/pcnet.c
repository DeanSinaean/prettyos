/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "pcnet.h"
#include "util/util.h"
#include "irq.h"
#include "timer.h"
#include "kheap.h"
#include "paging.h"
#include "video/console.h"


// Offsets to 16 bit IO-Ports
#define APROM0 0x00
#define APROM2 0x02
#define APROM4 0x04
#define RDP    0x10
#define RAP    0x12
#define RESET  0x14
#define BDP    0x16


static void writeBCR(PCNet_card* pAdapter, uint16_t bcr, uint16_t value)
{
    outportw(pAdapter->IO_base+RAP, bcr);   // Enables BCR register
    outportw(pAdapter->IO_base+BDP, value); // Write value to BCR register
}
static void writeCSR(PCNet_card* pAdapter, uint8_t csr, uint16_t value)
{
    outportw(pAdapter->IO_base+RAP, csr);   // Enable CSR register
    outportw(pAdapter->IO_base+RDP, value); // Write value to CSR register
}
static uint16_t readCSR(PCNet_card* pAdapter, uint8_t csr)
{
    outportw(pAdapter->IO_base+RAP, csr);   // Enable CSR register
    return inportw(pAdapter->IO_base+RDP);  // Read value from CSR register
}

void AMDPCnet_install(network_adapter_t* adapter)
{
    PCNet_card* pAdapter = malloc(sizeof(PCNet_card), 16, "PCNet_card");
    pAdapter->initialized = false;
    pAdapter->device = adapter;
    adapter->data = pAdapter;

    // Detect IO space
    pciDev_t* device = adapter->PCIdev;
    uint16_t pciCommandRegister = pci_config_read(device->bus, device->device, device->func, PCI_COMMAND, 2);
    pci_config_write_dword(device->bus, device->device, device->func, PCI_COMMAND, pciCommandRegister | PCI_CMD_IO | PCI_CMD_BUSMASTER); // resets status register, sets command register
    for (uint8_t j = 0; j < 6; ++j) // check network card BARs
    {
        if (device->bar[j].memoryType == PCI_IO)
        {
            pAdapter->IO_base = device->bar[j].baseAddress &= 0xFFFC;
        }
    }

  #ifdef _NETWORK_DIAGNOSIS_
    printf("\nIO: %xh", pAdapter->IO_base);
  #endif

    // Get MAC
    uint16_t temp = inportw(pAdapter->IO_base + APROM0);
    adapter->MAC[0] = temp;
    adapter->MAC[1] = temp>>8;
    temp = inportw(pAdapter->IO_base + APROM2);
    adapter->MAC[2] = temp;
    adapter->MAC[3] = temp>>8;
    temp = inportw(pAdapter->IO_base + APROM4);
    adapter->MAC[4] = temp;
    adapter->MAC[5] = temp>>8;

    // Reset
    inportw(pAdapter->IO_base+RESET);
    outportw(pAdapter->IO_base+RESET, 0); // Needed for NE2100LANCE adapters
    sleepMilliSeconds(10);
    writeBCR(pAdapter, 20, 0x0102); // Enable 32-bit mode

    // Stop
    writeCSR(pAdapter, 0, 0x04); // STOP-Reset

    // Setup descriptors, Init send and receive buffers
    pAdapter->currentRecDesc = 0;
    pAdapter->currentTransDesc = 0;
    pAdapter->receiveDesc = malloc(8*sizeof(PCNet_descriptor), 16, "PCNet: RecDesc");
    pAdapter->transmitDesc = malloc(8*sizeof(PCNet_descriptor), 16, "PCNet: TransDesc");

    for (uint8_t i = 0; i < 8; i++)
    {
        void* buffer = malloc(2048, 16, "PCnet receive buffer");
        pAdapter->receiveBuf[i] = buffer;
        pAdapter->receiveDesc[i].address = paging_getPhysAddr(buffer);
        pAdapter->receiveDesc[i].flags = 0x80000000 | 0x7FF | 0x0000F000; // Descriptor OWN | Buffer length | ?
        pAdapter->receiveDesc[i].flags2 = 0;

        buffer = malloc(2048, 16, "PCnet transmit buffer");
        pAdapter->transmitBuf[i] = buffer;
        pAdapter->transmitDesc[i].address = paging_getPhysAddr(buffer);
        pAdapter->transmitDesc[i].flags = 0;
        pAdapter->transmitDesc[i].flags2 = 0;
    }

    // Fill and register initialization block
    PCNet_initBlock* initBlock = malloc(sizeof(PCNet_initBlock), 16, "PCNet init block");
    memset(initBlock, 0, sizeof(PCNet_initBlock));
    initBlock->mode = 0x8000; // Promiscuous mode
    initBlock->receive_length = 3;
    initBlock->transfer_length = 3;
    initBlock->physical_address = *(uint64_t*)adapter->MAC;
    initBlock->receive_descriptor = paging_getPhysAddr(pAdapter->receiveDesc);
    initBlock->transmit_descriptor = paging_getPhysAddr(pAdapter->transmitDesc);
    uintptr_t phys_address = (uintptr_t)paging_getPhysAddr(initBlock);
    writeCSR(pAdapter, 1, phys_address); // Lower bits of initBlock address
    writeCSR(pAdapter, 2, phys_address>>16); // Higher bits of initBlock address

    irq_resetCounter(adapter->PCIdev->irq);

    // TEST:
    // When the FDRPAD (BCR9, bit 2) is set and the Am79C973/ Am79C975 controller is in full-duplex mode
    writeBCR(pAdapter, 9, BIT(2));
    // TEST

    // Init card
    writeCSR(pAdapter, 0, 0x0041); // Initialize card, activate interrupts
    if (!waitForIRQ(adapter->PCIdev->irq, 1000))
    {
        textColor(ERROR);
        printf("\nIRQ did not occur.\n");
        textColor(TEXT);
    }
    writeCSR(pAdapter, 4, 0x0C00 | readCSR(pAdapter, 4));

    // Activate card
    writeCSR(pAdapter, 0, 0x0042);
}

static void PCNet_receive(PCNet_card* pAdapter)
{
    while ((pAdapter->receiveDesc[pAdapter->currentRecDesc].flags & 0x80000000) == 0)
    {
        if (!(pAdapter->receiveDesc[pAdapter->currentRecDesc].flags & 0x40000000) &&
            (pAdapter->receiveDesc[pAdapter->currentRecDesc].flags & 0x03000000) == 0x03000000)
        {
            size_t size = pAdapter->receiveDesc[pAdapter->currentRecDesc].flags2 & 0xFFFF;
            if (size > 64)
                size -= 4; // Do not copy CRC32

            network_receivedPacket(pAdapter->device, pAdapter->receiveBuf[pAdapter->currentRecDesc], size);
        }
        pAdapter->receiveDesc[pAdapter->currentRecDesc].flags = 0x8000F7FF; // Set OWN-Bit and default values
        pAdapter->receiveDesc[pAdapter->currentRecDesc].flags2 = 0;

        pAdapter->currentRecDesc++; // Go to next descriptor
        if (pAdapter->currentRecDesc == 8)
            pAdapter->currentRecDesc = 0;
    }
}

bool PCNet_send(network_adapter_t* adapter, uint8_t* data, size_t length)
{
  #ifdef _NETWORK_DIAGNOSIS_
    printf("\nPCNet: Send packet");
  #endif
    PCNet_card* pAdapter = adapter->data;
    if (!pAdapter->initialized)
    {
        textColor(ERROR);
        printf("\nPCNet not initialized. Packet can not be sent.");
        textColor(TEXT);
        return (false);
    }

    // Prepare buffer
    memcpy(pAdapter->transmitBuf[pAdapter->currentTransDesc], data, length);

    // Prepare descriptor
    pAdapter->transmitDesc[pAdapter->currentTransDesc].flags2 = 0;
    pAdapter->transmitDesc[pAdapter->currentTransDesc].flags = 0x8300F000 | ((-length) & 0x7FF);
    writeCSR(pAdapter, 0, 0x48);

    pAdapter->currentTransDesc++;
    if (pAdapter->currentTransDesc == 8)
        pAdapter->currentTransDesc = 0;

    return (true);
}

void PCNet_handler(registers_t* data, pciDev_t* device)
{
    network_adapter_t* adapter = device->data;
    if (!adapter || adapter->driver != &network_drivers[PCNET])
    {
        return;
    }

    PCNet_card* pAdapter = adapter->data;

    uint16_t csr0 = readCSR(pAdapter, 0);

    if(!(csr0 & BIT(7)))
        return;

  #ifdef _NETWORK_DIAGNOSIS_
    textColor(0x03);
    printf("\n--------------------------------------------------------------------------------");

    textColor(YELLOW);
    printf("\nPCNet Interrupt Status: %yh, ", csr0);
    textColor(0x03);
  #endif

    if (pAdapter->initialized == false)
    {
        pAdapter->initialized = true;
      #ifdef _NETWORK_DIAGNOSIS_
        printf("\nInitialized");
      #endif
    }
    else
    {
        if (csr0 & 0x8000) // Error
        {
            textColor(ERROR);
            if (csr0 & 0x2000)
                printf("\nCollision error");
            else if (csr0 & 0x1000)
                printf("\nMissed frame error");
            else if (csr0 & 0x800)
                printf("\nMemory error");
          #ifdef _NETWORK_DIAGNOSIS_
            else
                printf("\nUndefined error: %x", csr0);
          #endif
            textColor(TEXT);
        }
      #ifdef _NETWORK_DIAGNOSIS_
        else if (csr0 & 0x0200)
            printf("\nTransmit descriptor finished");
      #endif
        else if (csr0 & 0x0400)
        {
            PCNet_receive(pAdapter);
        }
    }
    writeCSR(pAdapter, 0, csr0);
}


/*
* Copyright (c) 2011 The PrettyOS Project. All rights reserved.
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
