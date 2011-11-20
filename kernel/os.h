#ifndef OS_H
#define OS_H

#include "util/types.h"


// Additional features (Should be enabled per default)
#define _BOOTSCREEN_         // Enables the bootscreen displayed at startup
#define _PCI_VEND_PROD_LIST_ // http://www.pcidatabase.com/pci_c_header.php - Increases the size of the kernel significantly
#define _SERIAL_LOG_         // Enables log information over the COM-Ports
#define _EHCI_ENABLE_        // EHCI driver will be installed and used on the disadvantage of UHCI/OHCI if supported by the attached device
#define _OHCI_ENABLE_        // OHCI driver will be installed
#define _UHCI_ENABLE_        // UHCI driver will be installed

// Additional debug output (Should be disabled per default)
/// #define _DIAGNOSIS_            // General diagnosis output - activates prints to the screen about some details and memory use
/// #define _TASKING_DIAGNOSIS_    // Diagnosis output about tasking and scheduler
/// #define _PAGING_DIAGNOSIS_     // Diagnosis output about PD, PT etc.
/// #define _MALLOC_FREE_LOG_      // Shows information about malloc/free and heap expansion
/// #define _BROKENFREE_DIAGNOSIS_ // Shows file and line when a broken free occurs
/// #define _MEMLEAK_FIND_         // Counter of all (successful) malloc and free calls showing memory leaks in info bar2
/// #define _VM_DIAGNOSIS_         // Information about the vm86 task, but critical
/// #define _VBE_DEBUG_            // Debug output of the VBE driver
/// #define _DEVMGR_DIAGNOSIS_     // E.g. sectorRead, sectorWrite
/// #define _CACHE_DIAGNOSIS_      // Read cache logger
/// #define _HCI_DIAGNOSIS_        // Debug usb host controller (UHCI, OHCI, EHCI)
/// #define _OHCI_DIAGNOSIS_       // Debug OHCI
/// #define _UHCI_DIAGNOSIS_       // Debug UHCI
/// #define _EHCI_DIAGNOSIS_       // Debug EHCI
/// #define _USB_DIAGNOSIS_        // Debug USB transfers
/// #define _USB_TRANSFER_DIAGNOSIS_ // Debug USB transfers and transactions
/// #define _FLOPPY_DIAGNOSIS_     // Information about the floppy(-motor)
/// #define _RAMDISK_DIAGNOSIS_    // Information about the ramdisk
/// #define _FAT_DIAGNOSIS_        // Only as transition state during implementation of FAT 12/16/32
/// #define _FAT_DETAIL_DIAGNOSIS_ // cluster2sector, read
/// #define _NETWORK_DATA_         // Information about networking packets
/// #define _NETWORK_DIAGNOSIS_    // Information about the network adapters
/// #define _ARP_DEBUG_            // Information about ARP
/// #define _DHCP_DEBUG_           // Information about DHCP
/// #define _ICMP_DEBUG_           // Analysis of ICMP information besides echo request/response
/// #define _UDP_DEBUG_            // Information about UDP
/// #define _TCP_DEBUG_            // Information about TCP
/// #define _NETBIOS_DEBUG_        // NetBIOS packet analysis

// output of the serial log to COMx:
#define SER_LOG_TCP    1
#define SER_LOG_HEAP   1
#define SER_LOG_VM86   2
#define SER_LOG_FAT    1

extern const char* const version; // PrettyOS version string
extern struct todoList* kernel_idleTasks;


#ifdef _DIAGNOSIS_
void textColor(uint8_t color);
size_t vprintf(const char*, va_list);
#endif

static inline void kdebug(uint8_t color, const char* args, ...)
{
    #ifdef _DIAGNOSIS_
    if(color != 0x00)
    {
        textColor(color);
    }
    va_list ap;
    va_start(ap, args);
    vprintf(args, ap);
    va_end(ap);
    if(color != 0x00)
    {
        textColor(0x0F);
    }
    #endif
}


#endif
