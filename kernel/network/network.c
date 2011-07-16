/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "network.h"
#include "util.h"
#include "list.h"
#include "todo_list.h"
#include "kheap.h"
#include "irq.h"
#include "video/console.h"
#include "netprotocol/ethernet.h"
#include "rtl8139.h"
#include "rtl8168.h"
#include "pcnet.h"


typedef enum
{
    RTL8139, RTL8168, PCNET, ND_COUNT
} network_drivers;

static network_driver_t drivers[ND_COUNT] =
{
    {.install = &install_RTL8139, .interruptHandler = &rtl8139_handler, .sendPacket = &rtl8139_send},
    {.install = &install_RTL8168, .interruptHandler = &rtl8168_handler, .sendPacket = 0},
    {.install = &install_AMDPCnet, .interruptHandler = &PCNet_handler, .sendPacket = &PCNet_send}
};

Packet_t lastPacket; // save data during packet receive thru the protocols

static list_t*  adapters = 0;
static list_t*  RxBuffers = 0;
static mutex_t* RxMutex = 0;


bool network_installDevice(pciDev_t* device)
{
    textColor(HEADLINE);

    network_driver_t* driver = 0;
    if(device->deviceID == 0x8139 && device->vendorID == 0x10EC) // RTL 8139
    {
        driver = &drivers[RTL8139];
        printf("\nRealtek RTL8139");
    }
    else if (device->deviceID == 0x8168 && device->vendorID == 0x10EC) // RTL 8111b/8168
    {
        driver = &drivers[RTL8168];
        printf("\nRealtek RTL8168");
    }
    else if (device->deviceID == 0x2000 && device->vendorID == 0x1022) // AMD PCNet III (Am79C973)
    {
        driver = &drivers[PCNET];
        printf("\nAMD PCnet III");
    }

    if(driver == 0 || driver->install == 0) // PrettyOS does not know the card or the driver is not properly installed
    {
        textColor(TEXT);
        return(false);
    }

    printf(" network adapter:");
    textColor(TEXT);

    // PrettyOS has a driver for this adapter. Install it.
    network_adapter_t* adapter = malloc(sizeof(network_adapter_t), 0, "network apdapter");
    adapter->driver = driver;
    adapter->PCIdev = device;
    device->data = adapter;

    arp_initTable(&adapter->arpTable);

    // Set IRQ handler
    irq_installHandler(device->irq, driver->interruptHandler);

    // Detect MMIO and IO space
    uint16_t pciCommandRegister = pci_config_read(device->bus, device->device, device->func, PCI_COMMAND);
    pci_config_write_dword(device->bus, device->device, device->func, PCI_COMMAND&0xFF, pciCommandRegister /*already set*/ | BIT(2) /*bus master*/); // resets status register, sets command register

    for (uint8_t j = 0; j < 6; ++j) // check network card BARs
    {
        if(device->bar[j].memoryType == PCI_MMIO)
            adapter->MMIO_base = (void*)(device->bar[j].baseAddress &= 0xFFFFFFF0);
        else if(device->bar[j].memoryType == PCI_IO)
            adapter->IO_base = device->bar[j].baseAddress &= 0xFFFC;
    }

    // Input makes #PF at some computers (gets is source of error #PF)
    /*
    printf("\nPlease type in your IP address: ");
    char temp[30];
    memset(temp, 0, 30);
    gets(temp);
    for(uint8_t i_start = 0, i_end = 0, byte = 0; i_end < 30 && byte < 4; i_end++)
    {
        if(temp[i_end] == 0)
        {
            adapter->IP[byte] = atoi(temp+i_start);
            break;
        }
        if(temp[i_end] == '.')
        {
            temp[i_end] = 0;
            adapter->IP[byte] = atoi(temp+i_start);
            i_start = i_end+1;
            byte++;
        }
    }
    */
    // Workaround: TODO
    adapter->IP.IP[0] =  IP_1;
    adapter->IP.IP[1] =  IP_2;
    adapter->IP.IP[2] =  IP_3;
    adapter->IP.IP[3] =  IP_4;

    adapter->Gateway_IP.IP[0] = GW_IP_1;
    adapter->Gateway_IP.IP[1] = GW_IP_2;
    adapter->Gateway_IP.IP[2] = GW_IP_3;
    adapter->Gateway_IP.IP[3] = GW_IP_4;

  #ifdef QEMU_HACK
    uint8_t gatewayMAC[6] = {GW_MAC_1, GW_MAC_2, GW_MAC_3, GW_MAC_4, GW_MAC_5, GW_MAC_6}; // HACK for TCP with qemu
    arp_addTableEntry(&(adapter->arpTable), gatewayMAC, adapter->Gateway_IP, false);
    memcpy(adapter->Subnet.IP, adapter->Gateway_IP.IP, 4);
  #endif

    adapter->driver->install(adapter);

    if(adapters == 0)
        adapters = list_Create();
    list_Append(adapters, adapter);

    // Try to get an IP by DHCP
    adapter->DHCP_State  = START;
  #ifndef QEMU_HACK
    DHCP_Discover(adapter);
  #endif
    textColor(TEXT);
    printf("\nMAC: ");
    textColor(IMPORTANT);
    printf("%M", adapter->MAC);
    textColor(TEXT);
    printf("\tIP: ");
    textColor(IMPORTANT);
    printf("%I\n\n", adapter->IP);
    textColor(TEXT);

    return(true);
}

bool network_sendPacket(network_adapter_t* adapter, uint8_t* buffer, size_t length)
{
    return(adapter->driver->sendPacket != 0 && adapter->driver->sendPacket(adapter, buffer, length));
}

static void network_handleReceivedBuffers(void* data, size_t length)
{
    mutex_lock(RxMutex);
    for(element_t* e = RxBuffers->head; e != 0;)
    {
        networkBuffer_t* buffer = e->data;
        e = e->next;
        EthernetRecv(buffer->adapter, (ethernet_t*)buffer->data, buffer->length);
        free(buffer->data);
        list_Delete(RxBuffers, buffer);
        free(buffer);
    }
    mutex_unlock(RxMutex);
}

void network_receivedPacket(network_adapter_t* adapter, uint8_t* data, size_t length) // Called by driver
{
    if(RxBuffers == 0)
        RxBuffers = list_Create();
    if(!RxMutex)
        RxMutex = mutex_create();

    networkBuffer_t* buffer = malloc(sizeof(networkBuffer_t), 0, "networkBuffer_t");
    buffer->adapter = adapter;
    buffer->length = length;
    buffer->data = malloc(length, 0, "networkBuffer_t::data");
    memcpy(buffer->data, data, length);
    mutex_lock(RxMutex);
    list_Append(RxBuffers, buffer);
    mutex_unlock(RxMutex);

    todoList_add(kernel_idleTasks, &network_handleReceivedBuffers, 0, 0, 0);
}

void network_displayArpTables()
{
    if(adapters == 0) // No adapters installed
        return;
    textColor(TEXT);
    printf("\n\nARP Cache:");
    uint8_t i = 0;
    for (element_t* e = adapters->head; e != 0; e = e->next, i++)
    {
        printf("\n\nAdapter %u: %I", i, ((network_adapter_t*)e->data)->IP);
        arp_showTable(&((network_adapter_t*)e->data)->arpTable);
    }
    printf("\n");
}

network_adapter_t* network_getAdapter(IP_t IP)
{
    if(adapters == 0) return(0);
    for(element_t* e = adapters->head; e != 0; e = e->next)
    {
        network_adapter_t* adapter = e->data;
        if(adapter->IP.iIP == IP.iIP)
        {
            return(adapter);
        }
    }
    return(0);
}

network_adapter_t* network_getFirstAdapter()
{
    if(adapters == 0)
        return(0);
    return(adapters->head->data);
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
