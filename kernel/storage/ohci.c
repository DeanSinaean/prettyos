/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f?r die Verwendung dieses Sourcecodes siehe unten
*/

#include "ohci.h"
#include "util/util.h"
#include "timer.h"
#include "kheap.h"
#include "tasking/task.h"
#include "irq.h"
#include "keyboard.h"
#include "audio/sys_speaker.h"
#include "usb_msd.h"

#define NUMBER_OF_OHCI_RETRIES 3


static uint8_t index   = 0;
static ohci_t* curOHCI = 0;
static ohci_t* ohci[OHCIMAX];


static void ohci_handler(registers_t* r, pciDev_t* device);
static void ohci_start();
static void ohci_portCheck(ohci_t* o);
static void ohci_showPortstatus(ohci_t* o, uint8_t j);
static void ohci_resetPort(ohci_t* o, uint8_t j);
static void ohci_resetMempool(ohci_t* o, usb_transferType_t usbType);
static void ohci_toggleFrameInterval(ohci_t* o);


void ohci_install(pciDev_t* PCIdev, uintptr_t bar_phys, size_t memorySize)
{
  #ifdef _OHCI_DIAGNOSIS_
    printf("\n>>>ohci_install<<<\n");
  #endif

    curOHCI = ohci[index]   = malloc(sizeof(ohci_t), 0, "ohci");
    ohci[index]->PCIdevice  = PCIdev;
    ohci[index]->PCIdevice->data = ohci[index];
    uint16_t offset         = bar_phys % PAGESIZE;
    ohci[index]->num        = index;
    ohci[index]->enabledPortFlag = false;

    void* bar = paging_acquirePciMemory(bar_phys, alignUp(memorySize, PAGESIZE)/PAGESIZE);

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nOHCI_MMIO %Xh mapped to virt addr %Xh, offset: %xh, size: %xh", bar_phys, bar, offset, memorySize);
  #endif

    ohci[index]->OpRegs = (ohci_OpRegs_t*)(bar + offset);

    char str[10];
    snprintf(str, 10, "OHCI %u", index+1);

    scheduler_insertTask(create_cthread(&ohci_start, str));

    index++;
    sleepMilliSeconds(20); // HACK: Avoid race condition between ohci_install and the thread just created. Problem related to curOHCI global variable
}

static void ohci_start(void)
{
    ohci_t* o = curOHCI;

  #ifdef _OHCI_DIAGNOSIS_
    printf("\n>>>startOHCI<<<\n");
  #endif

    ohci_initHC(o);
    printf("\n\n>>> Press key to close this console. <<<");
    getch();
}

void ohci_initHC(ohci_t* o)
{
  #ifdef _OHCI_DIAGNOSIS_
    printf("\n>>>initOHCIHostController<<<\n");
  #endif

    textColor(HEADLINE);
    printf("Initialize OHCI Host Controller:");
    textColor(TEXT);

    // pci bus data
    uint8_t bus  = o->PCIdevice->bus;
    uint8_t dev  = o->PCIdevice->device;
    uint8_t func = o->PCIdevice->func;

    // prepare PCI command register
    // bit 9: Fast Back-to-Back Enable // not necessary
    // bit 2: Bus Master               // cf. http://forum.osdev.org/viewtopic.php?f=1&t=20255&start=0
    uint16_t pciCommandRegister = pci_config_read(bus, dev, func, PCI_COMMAND, 2);
    pci_config_write_dword(bus, dev, func, PCI_COMMAND, pciCommandRegister | PCI_CMD_MMIO | PCI_CMD_BUSMASTER); // resets status register, sets command register
    //uint8_t pciCapabilitiesList = pci_config_read(bus, dev, func, PCI_CAPLIST, 1);

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nPCI Command Register before:          %xh", pciCommandRegister);
    printf("\nPCI Command Register plus bus master: %xh", pci_config_read(bus, dev, func, PCI_COMMAND, 2));
    //printf("\nPCI Capabilities List: first Pointer: %yh", pciCapabilitiesList);
 #endif
    irq_installPCIHandler(o->PCIdevice->irq, ohci_handler, o->PCIdevice);

    ohci_resetHC(o);
}

void ohci_resetHC(ohci_t* o)
{
  #ifdef _OHCI_DIAGNOSIS_
    printf("\n\n>>>ohci_resetHostController<<<\n");
  #endif

    // Revision and Number Downstream Ports (NDP)
    /*
    When checking the Revision, the HC Driver must mask the rest of the bits in the HcRevision register
    as they are used to specify which optional features that are supported by the HC.
    */
    textColor(IMPORTANT);
    printf("\nOHCI: Revision %u.%u, Number Downstream Ports: %u\n",
        BYTE1(o->OpRegs->HcRevision) >> 4,
        BYTE1(o->OpRegs->HcRevision) & 0xF,
        BYTE1(o->OpRegs->HcRhDescriptorA)); // bits 7:0 provide Number Downstream Ports (NDP)

    if (!((BYTE1(o->OpRegs->HcRevision)) == 0x10 || BYTE1(o->OpRegs->HcRevision) == 0x11))
    {
        textColor(ERROR);
        printf("Revision not valid!");
    }
    textColor(TEXT);

    o->OpRegs->HcInterruptDisable = OHCI_INT_MIE;

    if (o->OpRegs->HcControl & OHCI_CTRL_IR) // SMM driver is active because the InterruptRouting bit is set
    {
        o->OpRegs->HcCommandStatus |= OHCI_STATUS_OCR; // ownership change request

        // monitor the IR bit to determine when the ownership change has taken effect
        uint16_t i;
        for (i=0; (o->OpRegs->HcControl & OHCI_CTRL_IR) && (i < 1000); i++)
        {
            sleepMilliSeconds(1);
        }

        if (i < 1000)
        {
            // Once the IR bit is cleared, the HC driver may proceed to the setup of the HC.
            textColor(SUCCESS);
            printf("\nOHCI takes control from SMM after %u loops.", i);
        }
        else
        {
            textColor(ERROR);
            printf("\nOwnership change request did not work. SMM has still control.");

            o->OpRegs->HcControl &= ~OHCI_CTRL_IR; // we try to reset the IR bit
            sleepMilliSeconds(200);

            if (o->OpRegs->HcControl & OHCI_CTRL_IR) // SMM driver is still active
            {
                printf("\nOHCI taking control from SMM did not work."); // evil
            }
            else
            {
                textColor(SUCCESS);
                printf("\nSuccess in taking control from SMM.");
            }
        }
        textColor(TEXT);
    }
    else // InterruptRouting bit is not set
    {
        if ((o->OpRegs->HcControl & OHCI_CTRL_HCFS) != OHCI_USB_RESET)
        {
            // there is an active BIOS driver, if the InterruptRouting bit is not set
            // and the HostControllerFunctionalState (HCFS) is not USBRESET
            printf("\nThere is an active BIOS OHCI driver");

            if ((o->OpRegs->HcControl & OHCI_CTRL_HCFS) != OHCI_USB_OPERATIONAL)
            {
                // If the HostControllerFunctionalState is not USBOPERATIONAL, the OS driver should set the HCFS to USBRESUME
                printf("\nActivate RESUME");
                o->OpRegs->HcControl &= ~OHCI_CTRL_HCFS; // clear HCFS bits
                o->OpRegs->HcControl |= OHCI_USB_RESUME; // set specific HCFS bit

                // and wait the minimum time specified in the USB Specification for assertion of resume on the USB
                sleepMilliSeconds(10);
            }
        }
        else // HCFS is USBRESET
        {
            // Neither SMM nor BIOS
            sleepMilliSeconds(10);
        }
    }

    // setup of the Host Controller
    printf("\n\nSetup of the HC\n");

    // The HC Driver should now save the contents of the HcFmInterval register ...
    uint32_t saveHcFmInterval = o->OpRegs->HcFmInterval;

    // ... and then issue a software reset
    o->OpRegs->HcCommandStatus |= OHCI_STATUS_RESET;
    sleepMilliSeconds(50);

    // After the software reset is complete (a maximum of 10 ms), the Host Controller Driver
    // should restore the value of the HcFmInterval register
    o->OpRegs->HcFmInterval = saveHcFmInterval;
    ohci_toggleFrameInterval(o);

    /*
    The HC is now in the USBSUSPEND state; it must not stay in this state more than 2 ms
    or the USBRESUME state will need to be entered for the minimum time specified
    in the USB Specification for the assertion of resume on the USB.
    */

    if ((o->OpRegs->HcControl & OHCI_CTRL_HCFS) == OHCI_USB_SUSPEND)
    {
        o->OpRegs->HcControl &= ~OHCI_CTRL_HCFS; // clear HCFS bits
        o->OpRegs->HcControl |= OHCI_USB_RESUME; // set specific HCFS bit
        sleepMilliSeconds(100);
    }

    /////////////////////
    // initializations //
    /////////////////////

    // HCCA
    /*
    Initialize the device data HCCA block to match the current device data state;
    i.e., all virtual queues are run and constructed into physical queues on the HCCA block
    and other fields initialized accordingly.
    */
    o->hcca = malloc(sizeof(ohci_HCCA_t), OHCI_HCCA_ALIGN, "ohci HCCA"); // HCCA must be minimum 256-byte aligned
    memset(o->hcca, 0, sizeof(ohci_HCCA_t));

    /*
    Initialize the Operational Registers to match the current device data state;
    i.e., all virtual queues are run and constructed into physical queues for HcControlHeadED and HcBulkHeadED
    */

    // Pointers and indices to ED, TD and TD buffers are part of ohci_t
    ohci_resetMempool(o, USB_CONTROL);
    o->lastTT = USB_CONTROL;

    // ED pool: NUM_ED EDs
    for (uint32_t i=0; i<NUM_ED; i++)
    {
        o->pED[i] = malloc(sizeof(ohciED_t), OHCI_DESCRIPTORS_ALIGN, "ohci_ED");
    }

    for (uint32_t i=0; i<NUM_ED; i++)
    {
        if ((i == NUM_ED_BULK - 1) || (i == NUM_ED - 1))
        {
            o->pED[i]->nextED = 0; // no next ED, end of control or bulk list
        }
        else
        {
            o->pED[i]->nextED = paging_getPhysAddr(o->pED[i+1]);
            o->pED[i]->sKip = 1; //TEST
        }
    }
    o->OpRegs->HcControlHeadED = o->OpRegs->HcControlCurrentED = paging_getPhysAddr(o->pED[NUM_ED_CONTROL]);
    o->OpRegs->HcBulkHeadED    = o->OpRegs->HcBulkCurrentED    = paging_getPhysAddr(o->pED[NUM_ED_BULK]);

    // TD pool: NUM_TD TDs and buffers
    for (uint32_t i=0; i<NUM_TD; i++)
    {
        o->pTDbuff[i]         = malloc(512, 512, "ohci_TDbuffer");
        o->pTD[i]             = malloc(sizeof(ohciTD_t), OHCI_DESCRIPTORS_ALIGN, "ohci_TD");
        o->pTD[i]->curBuffPtr = paging_getPhysAddr(o->pTDbuff[i]);
        o->pTDphys[i]         = paging_getPhysAddr(o->pTD[i]);
    }

    // Set the HcHCCA to the physical address of the HCCA block
    o->OpRegs->HcHCCA = paging_getPhysAddr(o->hcca);

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nHCCA (phys. address): %X", o->OpRegs->HcHCCA);
  #endif

    // Set HcInterruptEnable to have all interrupt enabled except Start-of-Frame detect
    o->OpRegs->HcInterruptDisable = OHCI_INT_SF   | // start of frame
                                    OHCI_INT_MIE;   // deactivates interrupts
    o->OpRegs->HcInterruptStatus  = ~0;
    o->OpRegs->HcInterruptEnable  = OHCI_INT_SO   | // scheduling overrun
                                    OHCI_INT_WDH  | // write back done head
                                    OHCI_INT_RD   | // resume detected
                                    OHCI_INT_UE   | // unrecoverable error
                                    OHCI_INT_FNO  | // frame number overflow
                                    OHCI_INT_RHSC | // root hub status change
                                    OHCI_INT_OC   | // ownership change
                                    OHCI_INT_MIE;   // activates interrupts

    o->OpRegs->HcControl &= ~(OHCI_CTRL_CLE | OHCI_CTRL_PLE | OHCI_CTRL_IE | OHCI_CTRL_BLE);  // de-activate bulk, periodical and isochronous transfers

    o->OpRegs->HcControl |= OHCI_CTRL_RWE; // activate RemoteWakeup

    // Set HcPeriodicStart to a value that is 90% of the value in FrameInterval field of the HcFmInterval register
    // When HcFmRemaining reaches this value, periodic lists gets priority over control/bulk processing
    o->OpRegs->HcPeriodicStart = (o->OpRegs->HcFmInterval & 0x3FFF) * 90/100;

    /*
    The counter value represents the largest amount of data in bits which can be sent or received by the HC in a single
    transaction at any given time without causing scheduling overrun.
    */
    o->OpRegs->HcFmInterval &= ~OHCI_CTRL_FSLARGESTDATAPACKET; // clear FSLargestDataPacket
    o->OpRegs->HcFmInterval |= BIT(30); // ???
    ohci_toggleFrameInterval(o);

    /*
    LSThreshold contains a value which is compared to the FrameRemaining field prior to initiating a Low Speed transaction.
    The transaction is started only if FrameRemaining >= this field.
    The value is calculated by HCD with the consideration of transmission and setup overhead.
    */
    o->OpRegs->HcLSThreshold = 0; // HCD allowed to change?

    printf("\nHcFrameInterval: %u", o->OpRegs->HcFmInterval & 0x3FFF);
    printf("  HcPeriodicStart: %u", o->OpRegs->HcPeriodicStart);
    printf("  FSMPS: %u bits", (o->OpRegs->HcFmInterval >> 16) & 0x7FFF);
    printf("  LSThreshhold: %u", o->OpRegs->HcLSThreshold & 0xFFF);

    // ControlBulkServiceRatio (CBSR)
    o->OpRegs->HcControl |= OHCI_CTRL_CBSR;  // No. of Control EDs Over Bulk EDs Served = 4 : 1

    /*
    The HCD then begins to send SOF tokens on the USB by writing to the HcControl register with
    the HostControllerFunctionalState set to USBOPERATIONAL and the appropriate enable bits set.
    The Host Controller begins sending SOF tokens within one ms
    (if the HCD needs to know when the SOFs it may unmask the StartOfFrame interrupt).
    */

    printf("\n\nHC will be set to USB Operational.\n");

    o->OpRegs->HcControl &= ~OHCI_CTRL_HCFS;      // clear HCFS bits
    o->OpRegs->HcControl |= OHCI_USB_OPERATIONAL; // set specific HCFS bit

    o->OpRegs->HcRhStatus |= OHCI_RHS_LPSC;           // SetGlobalPower: turn on power to all ports
    o->rootPorts = min(OHCIPORTMAX, BYTE1(o->OpRegs->HcRhDescriptorA)); // NumberDownstreamPorts

    o->OpRegs->HcRhDescriptorA &= ~OHCI_RHA_DT; // DeviceType: This bit specifies that the Root Hub is not a compound device.
                                                // The Root Hub is not permitted to be a compound device. This field should always read/write 0.
    o->OpRegs->HcRhDescriptorB = 0; // DR: devices removable; PPCM: PortPowerControlMask is set to global power switching mode

    // duration HCD has to wait before accessing a powered-on port of the Root Hub.
    // It is implementation-specific. Duration is calculated as POTPGT * 2 ms.
    o->powerWait = max(20, 2 * BYTE4(o->OpRegs->HcRhDescriptorA));

    textColor(IMPORTANT);
    printf("\n\nFound %u Rootports. Power wait: %u ms\n", o->rootPorts, o->powerWait);
    textColor(TEXT);

    o->ports = malloc(sizeof(ohci_port_t)*o->rootPorts, 0, "ohci_port_t");
    for (uint8_t j = 0; j < o->rootPorts; j++)
    {
        o->ports[j].connected = false;
        o->ports[j].ohci = o;
        o->ports[j].port.type = &USB_OHCI;
        o->ports[j].port.data = o->ports + j;
        o->ports[j].port.insertedDisk = 0;
        snprintf(o->ports[j].port.name, 14, "OHCI-Port %u", j+1);
        attachPort(&o->ports[j].port);
    }
    o->enabledPortFlag = true;
    for (uint8_t j = 0; j < o->rootPorts; j++)
    {
        o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PRS | OHCI_PORT_CCS | OHCI_PORT_PES;
        sleepMilliSeconds(50);
    }
}


/*******************************************************************************************************
*                                                                                                      *
*                                              PORTS                                                   *
*                                                                                                      *
*******************************************************************************************************/

void ohci_portCheck(ohci_t* o)
{
    for (uint8_t j = 0; j < o->rootPorts; j++)
    {
        console_setProperties(CONSOLE_SHOWINFOBAR|CONSOLE_AUTOSCROLL|CONSOLE_AUTOREFRESH); // protect console against info area

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_CCS) // connected
        {
             ohci_showPortstatus(o,j);
        }
        else
        {
            if (o->ports[j].connected == true)
            {
                ohci_showPortstatus(o,j);
            }
        }
    }
}

void ohci_showPortstatus(ohci_t* o, uint8_t j)
{
    if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_CSC)
    {
        textColor(IMPORTANT);
        printf("\nport[%u]: ", j+1);
        textColor(TEXT);

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_LSDA)
        {
            printf("LowSpeed");
        }
        else
        {
            printf("FullSpeed");
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_CCS)
        {
            textColor(SUCCESS);
            printf(" dev. attached  -");
            o->ports[j].connected = true;
            ohci_resetPort(o, j);           ///// <--- reset on attached /////
        }
        else
        {
            printf(" device removed -");

            if(o->ports[j].port.insertedDisk && o->ports[j].port.insertedDisk->type == &USB_MSD)
            {
                usb_destroyDevice(o->ports[j].port.insertedDisk->data);
                removeDisk(o->ports[j].port.insertedDisk);
                o->ports[j].port.insertedDisk = 0;
                o->ports[j].connected = false;
                showPortList();
                showDiskList();
                beep(1000, 100);
                beep(800, 80);
            }
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PES)
        {
            textColor(SUCCESS);
            printf(" enabled  -");
            if (o->enabledPortFlag && (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PPS) && (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_CCS)) // powered, device attached
            {
                ohci_setupUSBDevice(o, j);
            }
        }
        else
        {
            textColor(IMPORTANT);
            printf(" disabled -");
        }
        textColor(TEXT);

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PSS)
            printf(" suspend   -");
        else
            printf(" not susp. -");

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_POCI)
        {
            textColor(ERROR);
            printf(" overcurrent -");
            textColor(TEXT);
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PRS)
            printf(" reset -");

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PPS)
            printf(" pow on  -");
        else
            printf(" pow off -");

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_CSC)
        {
            printf(" CSC -");
            o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_CSC;
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PESC)
        {
            printf(" enable Change -");
            o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PESC;
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PSSC)
        {
            printf(" resume compl. -");
            o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PSSC;
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_OCIC)
        {
            printf(" overcurrent Change -");
            o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_OCIC;
        }

        if (o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PRSC)
        {
            printf(" Reset Complete -");
            o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PRSC;
        }
    }
}

static void ohci_resetPort(ohci_t* o, uint8_t j)
{
    o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PRS; // reset

    uint32_t timeout=100;
    while ((o->OpRegs->HcRhPortStatus[j] & OHCI_PORT_PRS) != 0) // Reset-Bit still set to 1
    {
     #ifdef _OHCI_DIAGNOSIS_
        printf("\nwaiting for ohci port reset");
     #endif
        delay(20000);
        timeout--;
        if (timeout==0)
        {
            textColor(ERROR);
            printf("\nTimeout Error: ohci port reset bit still set to 1");
            textColor(TEXT);
            break;
        }
    }
    printf("\ntimeout: %u\n", timeout);

    o->OpRegs->HcRhPortStatus[j] |= OHCI_PORT_PES; // enable
    delay(100000);
}


/*******************************************************************************************************
*                                                                                                      *
*                                              ohci handler                                            *
*                                                                                                      *
*******************************************************************************************************/

static void ohci_handler(registers_t* r, pciDev_t* device)
{
    // Check if an OHCI controller issued this interrupt
    ohci_t* o = device->data;
    bool found = false;

    for (uint8_t i=0; i<OHCIMAX; i++)
    {
        if (o == ohci[i])
        {
            textColor(TEXT);
            found = true;
            break;
        }
    }

    if(!found || o == 0)
    {
      #ifdef _OHCI_DIAGNOSIS_
        printf("Interrupt did not came from OHCI device!\n");
      #endif
        return;
    }

    volatile uint32_t val = o->OpRegs->HcInterruptStatus;

    if(val==0)
    {
      #ifdef _OHCI_DIAGNOSIS_
        printf("Interrupt came from another OHCI device!\n");
      #endif
        return;
    }

    if (val & OHCI_INT_WDH) // write back done head
    {
        // printf("Write back done head.");

        // the value has to be stored before resetting OHCI_INT_WDH
        for (uint8_t i=0; i<NUM_TD; i++)
        {
            if ( (o->hcca->doneHead & ~0xF) == o->pTDphys[i])
            {
                // TODO: save o->pTDphys[i] // ??
                //

                /*
                textColor(SUCCESS);
                printf("\nDONEHEAD.");
                textColor(TEXT);
                printf(" toggle: %u  ", o->pTD[i]->toggle);
                */
            }
        }
    }

    o->OpRegs->HcInterruptStatus = val; // reset interrupts

    if (!((val & OHCI_INT_SF) || (val & OHCI_INT_RHSC)))
    {
        printf("\nUSB OHCI %u: ", o->num);
    }

    if (val & OHCI_INT_SO) // scheduling overrun
    {
        printf("Scheduling overrun.");
    }

    if (val & OHCI_INT_RD) // resume detected
    {
        printf("Resume detected.");
    }

    if (val & OHCI_INT_UE) // unrecoverable error
    {
        printf("Unrecoverable HC error.");
        o->OpRegs->HcCommandStatus |= OHCI_STATUS_RESET;
    }

    if (val & OHCI_INT_FNO) // frame number overflow
    {
        printf("Frame number overflow.");
    }

    if (val & OHCI_INT_RHSC) // root hub status change
    {
        ohci_portCheck(o);
    }

    if (val & OHCI_INT_OC) // ownership change
    {
        printf("Ownership change.");
    }
}


/*******************************************************************************************************
*                                                                                                      *
*                                          Setup USB-Device                                            *
*                                                                                                      *
*******************************************************************************************************/

void ohci_setupUSBDevice(ohci_t* o, uint8_t portNumber)
{
    disk_t* disk = malloc(sizeof(disk_t), 0, "disk_t"); // TODO: Handle non-MSDs
    disk->port = &o->ports[portNumber].port;
    disk->port->insertedDisk = disk;

    usb_device_t* device = usb_createDevice(disk);
    usb_setupDevice(device, portNumber+1);
}


/*******************************************************************************************************
*                                                                                                      *
*                                            Transactions                                              *
*                                                                                                      *
*******************************************************************************************************/

typedef struct
{
    ohciTD_t*   TD;
    void*       TDBuffer;
    void*       inBuffer;
    size_t      inLength;
} ohci_transaction_t;


void ohci_setupTransfer(usb_transfer_t* transfer)
{
    ohci_t* o = ((ohci_port_t*)transfer->HC->data)->ohci;

    if(o->lastTT != transfer->type)
    {
        ohci_resetMempool(o, transfer->type);
        o->lastTT = transfer->type;
    }

    o->OpRegs->HcControl &= ~(OHCI_CTRL_CLE | OHCI_CTRL_BLE); // de-activate control and bulk transfers
    o->OpRegs->HcCommandStatus &= ~OHCI_STATUS_CLF; // control list not filled
    o->OpRegs->HcCommandStatus &= ~OHCI_STATUS_BLF; // bulk list not filled

    // recycle bulk ED/TDs
    if ((o->indexED >= NUM_ED-2) ||
        (o->indexTD >= NUM_TD-2))
    {
        ohci_resetMempool(o, USB_BULK);
    }

    // recycle control ED/TDs
    if ((o->indexED == NUM_ED_BULK-2) || (o->indexED == NUM_ED_BULK-1) ||
        (o->indexTD == NUM_TD_BULK-2) || (o->indexTD == NUM_TD_BULK-1))
    {
        ohci_resetMempool(o, USB_CONTROL);
    }

    // endpoint descriptor
    transfer->data = o->pED[o->indexED];

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nsetupTransfer: indexED: %u", o->indexED);
  #endif
}

void ohci_setupTransaction(usb_transfer_t* transfer, usb_transaction_t* uTransaction, bool toggle, uint32_t tokenBytes, uint32_t type, uint32_t req, uint32_t hiVal, uint32_t loVal, uint32_t i, uint32_t length)
{
    ohci_transaction_t* oTransaction = uTransaction->data = malloc(sizeof(ohci_transaction_t), 0, "ohci_transaction_t");
    oTransaction->inBuffer = 0;
    oTransaction->inLength = 0;

    ohci_t* o = ((ohci_port_t*)transfer->HC->data)->ohci;

    oTransaction->TD = ohci_createTD_SETUP(o, transfer->data, 1, toggle, tokenBytes, type, req, hiVal, loVal, i, length, &oTransaction->TDBuffer);

  #ifdef _OHCI_DIAGNOSIS_
    usb_request_t* request = (usb_request_t*)oTransaction->TDBuffer;
    printf("\ntype: %u req: %u valHi: %u valLo: %u i: %u len: %u", request->type, request->request, request->valueHi, request->valueLo, request->index, request->length);
  #endif

    if (transfer->transactions->tail)
    {
        ohci_transaction_t* oLastTransaction = ((usb_transaction_t*)transfer->transactions->tail->data)->data;
        oLastTransaction->TD->nextTD = paging_getPhysAddr(oTransaction->TD); // build TD queue
    }
}

void ohci_inTransaction(usb_transfer_t* transfer, usb_transaction_t* uTransaction, bool toggle, void* buffer, size_t length)
{
    ohci_t* o = ((ohci_port_t*)transfer->HC->data)->ohci;
    ohci_transaction_t* oTransaction = uTransaction->data = malloc(sizeof(ohci_transaction_t), 0, "ohci_transaction_t");
    oTransaction->inBuffer = buffer;
    oTransaction->inLength = length;

    oTransaction->TDBuffer = o->pTDbuff[o->indexTD];
    oTransaction->TD = ohci_createTD_IO(o, transfer->data, 1, OHCI_TD_IN, toggle, length);

    if (transfer->transactions->tail)
    {
        ohci_transaction_t* oLastTransaction = ((usb_transaction_t*)transfer->transactions->tail->data)->data;
        oLastTransaction->TD->nextTD = paging_getPhysAddr(oTransaction->TD); // build TD queue
    }
}

void ohci_outTransaction(usb_transfer_t* transfer, usb_transaction_t* uTransaction, bool toggle, void* buffer, size_t length)
{
    ohci_t* o = ((ohci_port_t*)transfer->HC->data)->ohci;
    ohci_transaction_t* oTransaction = uTransaction->data = malloc(sizeof(ohci_transaction_t), 0, "ohci_transaction_t");
    oTransaction->inBuffer = 0;
    oTransaction->inLength = 0;

    oTransaction->TDBuffer = o->pTDbuff[o->indexTD];
    oTransaction->TD = ohci_createTD_IO(o, transfer->data, 1, OHCI_TD_OUT, toggle, length);

    if (buffer != 0 && length != 0)
    {
        memcpy(oTransaction->TDBuffer, buffer, length);
    }

    if (transfer->transactions->tail)
    {
        ohci_transaction_t* oLastTransaction = ((usb_transaction_t*)transfer->transactions->tail->data)->data;
        oLastTransaction->TD->nextTD = paging_getPhysAddr(oTransaction->TD); // build TD queue
    }
}

void ohci_issueTransfer(usb_transfer_t* transfer)
{
    /*  A transfer is completed when the Host Controller successfully transfers, to or from an endpoint, the byte pointed to by BufferEnd.
        Upon successful completion, the Host Controller sets CurrentBufferPointer to zero, sets ConditionCode to NOERROR,
        and retires the General TD to the Done Queue.
    */

    usb_outTransaction(transfer, 1, 0, 0); // dummy at the end of the TD chain (for different headPtr and tailPtr in the ED)

    ohci_t* o = ((ohci_port_t*)transfer->HC->data)->ohci;
    ohci_transaction_t* firstTransaction = ((usb_transaction_t*)transfer->transactions->head->data)->data;

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nohci_createED: devNum = %u endp = %u packetsize = %u", ((ohci_port_t*)transfer->HC->data)->num, transfer->endpoint, transfer->packetSize);
  #endif

    ohci_createED(transfer->data, paging_getPhysAddr(transfer->data), firstTransaction->TD, ((usb_device_t*)transfer->HC->insertedDisk->data)->num, transfer->endpoint, transfer->packetSize);

    if (transfer->type == USB_CONTROL)
    {
        o->OpRegs->HcControlCurrentED = paging_getPhysAddr(transfer->data);
    }

    if (transfer->type == USB_BULK)
    {
        o->OpRegs->HcBulkCurrentED = paging_getPhysAddr(transfer->data);
    }

  #ifdef _OHCI_DIAGNOSIS_
    textColor(MAGENTA);
    printf("\nHcControlCurrentED: %X", o->OpRegs->HcControlCurrentED);
    printf(" ED->skip = %u ED->Halted = %u", ((ohciED_t*)transfer->data)->sKip, ((ohciED_t*)transfer->data)->tdQueueHead & BIT(0));
    printf("\nHeadP = %X TailP = %X", ((ohciED_t*)transfer->data)->tdQueueHead,  ((ohciED_t*)transfer->data)->tdQueueTail);
    textColor(TEXT);

    for (uint8_t i=0; i<5; i++)
    {
        printf("\ni=%u\tED->TD:%X->%X TD->TD:%X->%X buf:%X",
                  i, paging_getPhysAddr(o->pED[i]), o->pED[i]->tdQueueHead,
                     paging_getPhysAddr(o->pTD[i]), o->pTD[i]->nextTD, o->pTD[i]->curBuffPtr);
    }
  #endif

    o->OpRegs->HcCommandStatus |= (OHCI_STATUS_CLF | OHCI_STATUS_BLF); // control and bulk lists filled

  #ifdef _OHCI_DIAGNOSIS_
    textColor(MAGENTA);
    printf("\nHcCommandStatus: %X", o->OpRegs->HcCommandStatus);
    textColor(TEXT);
  #endif

    for (uint8_t i = 0; i < NUMBER_OF_OHCI_RETRIES && !transfer->success; i++)
    {
      #ifdef _OHCI_DIAGNOSIS_
        printf("\ntransfer try = %u\n", i);
      #endif

        transfer->success = true;

        o->OpRegs->HcCommandStatus |= (OHCI_STATUS_CLF | OHCI_STATUS_BLF); // control and bulk lists filled
        o->pED[i]->tdQueueHead &= ~0x1; // reset Halted Bit
        o->OpRegs->HcControl |=  (OHCI_CTRL_CLE | OHCI_CTRL_BLE); // activate control and bulk transfers ////////////////////// S T A R T /////////////////

      #ifdef _OHCI_DIAGNOSIS_
        printf("\nNumber of TD elements (incl. dummy-TD): %u", list_getCount(transfer->transactions));
      #endif

        for (dlelement_t* elem = transfer->transactions->head; elem != 0; elem = elem->next)
        {
            while((o->OpRegs->HcFmRemaining & 0x3FFF) < 16000) { /* wait for nearly full frame time */ }

          #ifdef _OHCI_DIAGNOSIS_
            printf(" remaining time: %u  frame number: %u", o->OpRegs->HcFmRemaining & 0x3FFF, o->OpRegs->HcFmNumber);
          #endif

            delay(50000); // pause after transaction
        }

        delay(50000); // pause after transfer

        // check conditions - do not check the last dummy-TD
        for (dlelement_t* elem = transfer->transactions->head; elem && elem->next; elem = elem->next)
        {
            ohci_transaction_t* transaction = ((usb_transaction_t*)elem->data)->data;
            ohci_showStatusbyteTD(transaction->TD);

            transfer->success = transfer->success && (transaction->TD->cond == 0);
        }

      #ifdef _OHCI_DIAGNOSIS_
        if (!transfer->success)
        {
            printf("\nRetry transfer: %u", i+1);
        }
      #endif
    }

  #ifdef _OHCI_DIAGNOSIS_
    textColor(IMPORTANT);
    printf("\n\nED-Index: %u, Transfer->endpoint: %u, &o: %X", o->indexED, transfer->endpoint, o);
    printf("\nhcca->donehead: %X ", o->hcca->doneHead);
    textColor(TEXT);
    for (uint8_t i=0; i<5; i++)
    {
        printf("\ni=%u\tED->TD:%X->%X TD->TD:%X->%X buf:%X",
                  i, paging_getPhysAddr(o->pED[i]), o->pED[i]->tdQueueHead,
                     paging_getPhysAddr(o->pTD[i]), o->pTD[i]->nextTD, o->pTD[i]->curBuffPtr);
    }
  #endif

    for (dlelement_t* elem = transfer->transactions->head; elem != 0; elem = elem->next)
    {
        ohci_transaction_t* transaction = ((usb_transaction_t*)elem->data)->data;

        if (transaction->inBuffer != 0 && transaction->inLength != 0)
        {
            memcpy(transaction->inBuffer, transaction->TDBuffer, transaction->inLength);
        }
        free(transaction);
    }
    if (transfer->success)
    {
      #ifdef _OHCI_DIAGNOSIS_
        textColor(SUCCESS);
        printf("\nTransfer successful.");
        textColor(TEXT);
      #endif
    }
    else
    {
        textColor(ERROR);
        printf("\nTransfer failed.");
        textColor(TEXT);
    }

    o->indexED++;
}


/*******************************************************************************************************
*                                                                                                      *
*                                            ohci ED TD functions                                      *
*                                                                                                      *
*******************************************************************************************************/

ohciTD_t* ohci_createTD_SETUP(ohci_t* o, ohciED_t* oED, uintptr_t next, bool toggle, uint32_t tokenBytes, uint32_t type, uint32_t req, uint32_t hiVal, uint32_t loVal, uint32_t i, uint32_t length, void** buffer)
{
    ohciTD_t* oTD = (ohciTD_t*)o->pTD[o->indexTD];

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nohci_createTD_SETUP: ED = %u  TD = %u toggle: %u", o->indexED, o->indexTD, oTD->toggle);
  #endif

    if (next != 0x1)
    {
        oTD->nextTD = paging_getPhysAddr((void*)next);
    }
    else
    {
        oTD->nextTD = BIT(0);
    }

    oTD->direction    = OHCI_TD_SETUP;
    oTD->toggle       = toggle;
    oTD->toggleFromTD = 1;
    oTD->cond         = OHCI_TD_NOCC; // to be executed
    oTD->delayInt     = OHCI_TD_NOINT;
    oTD->errCnt       = 0;
    oTD->bufRounding  = 1;

    usb_request_t* request = *buffer = o->pTDbuff[o->indexTD];
    request->type     = type;
    request->request  = req;
    request->valueHi  = hiVal;
    request->valueLo  = loVal;
    request->index    = i;
    request->length   = length;

    oTD->curBuffPtr   = paging_getPhysAddr(request);
    oTD->buffEnd      = oTD->curBuffPtr + sizeof(usb_request_t) - 1; // physical address of the last byte in the buffer

    oED->tdQueueTail = paging_getPhysAddr(oTD);

    o->indexTD++;
    return (oTD);
}

ohciTD_t* ohci_createTD_IO(ohci_t* o, ohciED_t* oED, uintptr_t next, uint8_t direction, bool toggle, uint32_t tokenBytes)
{
    ohciTD_t* oTD = o->pTD[o->indexTD];

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nohci_createTD_IO: ED = %u  TD = %u toggle: %u", o->indexED, o->indexTD, oTD->toggle);
  #endif

    if (next != 0x1)
    {
        oTD->nextTD = paging_getPhysAddr((void*)next);
    }
    else
    {
        oTD->nextTD = BIT(0);
    }
    oTD->direction    = direction;
    oTD->toggle       = toggle;
    oTD->toggleFromTD = 1;
    oTD->cond         = OHCI_TD_NOCC; // to be executed
    oTD->delayInt     = OHCI_TD_NOINT;
    oTD->errCnt       = 0;
    oTD->bufRounding  = 1;

    if(tokenBytes)
    {
        oTD->curBuffPtr  = paging_getPhysAddr(o->pTDbuff[o->indexTD]);
        oTD->buffEnd     = oTD->curBuffPtr + tokenBytes - 1; // BufferEnd contains physical address of the last byte in the buffer
    }
    else
    {
        oTD->curBuffPtr  = paging_getPhysAddr(o->pTDbuff[o->indexTD]);
        oTD->buffEnd     = oTD->curBuffPtr;
    }

    oED->tdQueueTail = paging_getPhysAddr(oTD);

    o->indexTD++;
    return (oTD);
}


void ohci_createED(ohciED_t* head, uint32_t horizPtr, ohciTD_t* firstTD, uint32_t device, uint32_t endpoint, uint32_t packetSize)
{
    //head->nextED  = horizPtr; // ==> freeze
  #ifdef _OHCI_DIAGNOSIS_
    printf("\nnext ED: %X", head->nextED);
  #endif

    head->endpNum = endpoint;

  #ifdef _OHCI_DIAGNOSIS_
    printf("\nendpoint: %u", head->endpNum);
  #endif

    head->devAddr = device;

  #ifdef _OHCI_DIAGNOSIS_
    printf("  device: %u", head->devAddr);
  #endif

    head->mps     = MPS_FULLSPEED; // packetSize;
    head->dir     = OHCI_ED_TD ;   // 00b Get direction From TD
    head->speed   = 0;  // speed of the endpoint: full-speed (0), low-speed (1)
    head->format  = 0;  // format of the TDs: Control, Bulk, or Interrupt Endpoint (0); Isochronous Endpoint (1)

    if (firstTD == 0)
    {
        head->tdQueueHead = 0x1; // no TD in queue
        printf("\nno TD in queue");
    }
    else
    {
        head->tdQueueHead = paging_getPhysAddr((void*)firstTD) & ~0xD; // head TD in queue // Flags 0 0 C H
      #ifdef _OHCI_DIAGNOSIS_
        printf("\nohci_createED: %X tdQueueHead = %X tdQueueTail = %X", paging_getPhysAddr(head), head->tdQueueHead, head->tdQueueTail); // Tail is read-only
      #endif
    }

    head->sKip = 0;  // 1: HC continues on to next ED w/o attempting access to the TD queue or issuing any USB token for the endpoint
}


////////////////////
// analysis tools //
////////////////////

uint8_t ohci_showStatusbyteTD(ohciTD_t* TD)
{
    textColor(ERROR);

    switch (TD->cond) // TD status // bits 10 and 11 reserved
    {
        case 0:  /*textColor(SUCCESS); printf("Successful Completion.");*/                                           break;
        case 1:  printf("\nLast data packet from endpoint contained a CRC error.");                                  break;
        case 2:  printf("\nLast data packet from endpoint contained a bit stuffing violation.");                     break;
        case 3:  printf("\nLast packet from endpoint had data toggle PID that did not match the expected value.");   break;
        case 4:  printf("\nTD was moved to the Done Queue because the endpoint returned a STALL PID.");              break;
        case 5:  printf("\nDevice: no response to token (IN) or no handshake (OUT)");                                break;
        case 6:  printf("\nCheck bits on PID from endpoint failed on data PID (IN) or handshake (OUT).");            break;
        case 7:  printf("\nReceive PID was not valid when encountered or PID value is not defined.");                break;
        case 8:  printf("\nDATAOVERRUN: Too many data returned by the endpoint.");                                   break;
        case 9:  printf("\nDATAUNDERRUN: Endpoint returned less than MPS.");                                         break;
        case 12: printf("\nBUFFEROVERRUN");                                                                          break;
        case 13: printf("\nBUFFERUNDERRUN");                                                                         break;
        case 14:
        case 15: printf("\nNOT ACCESSED");                                                                           break;
    }

    textColor(TEXT);

    return TD->cond;
}

static void ohci_resetMempool(ohci_t* o, usb_transferType_t usbType)
{
    if (usbType == USB_CONTROL)
    {
        o->indexED = NUM_ED_CONTROL;
        o->indexTD = NUM_TD_CONTROL;
    }

    if (usbType == USB_BULK)
    {
        o->indexED = NUM_ED_BULK;
        o->indexTD = NUM_TD_BULK;
    }
}

static void ohci_toggleFrameInterval(ohci_t* o)
{
    if (o->OpRegs->HcFmInterval & BIT(31)) // check FRT
    {
        o->OpRegs->HcFmInterval &= ~BIT(31); // clear FRT
    }
    else
    {
        o->OpRegs->HcFmInterval |= BIT(31); // set FRT
    }
}


/*
* Copyright (c) 2011-2013 The PrettyOS Project. All rights reserved.
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
