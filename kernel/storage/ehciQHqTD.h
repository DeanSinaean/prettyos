#ifndef EHCI_QH_QTD_H
#define EHCI_QH_QTD_H

#include "os.h"
#include "ehci.h"

#define OUT   0
#define IN    1
#define SETUP 2


typedef struct
{
    uint8_t status;

    uint8_t  pid          :  2;
    uint8_t  errorCounter :  2;
    uint8_t  currPage     :  3;
    uint8_t  interrupt    :  1;
    uint16_t bytes        : 15;
    uint16_t dataToggle   :  1;
} __attribute__((packed)) ehci_qtdToken_t;

typedef struct
{
    uint32_t        next;
    uint32_t        nextAlt;
    ehci_qtdToken_t token;
    uint32_t        buffer0;
    uint32_t        buffer1;
    uint32_t        buffer2;
    uint32_t        buffer3;
    uint32_t        buffer4;
    uint32_t        extend0;
    uint32_t        extend1;
    uint32_t        extend2;
    uint32_t        extend3;
    uint32_t        extend4;
} __attribute__((packed)) ehci_qtd_t;

typedef struct ehci_qhd
{
    uint32_t   horizontalPointer;
    uint32_t   deviceAddress       :  7;
    uint32_t   inactive            :  1;
    uint32_t   endpoint            :  4;
    uint32_t   endpointSpeed       :  2;
    uint32_t   dataToggleControl   :  1;
    uint32_t   H                   :  1;
    uint32_t   maxPacketLength     : 11;
    uint32_t   controlEndpointFlag :  1;
    uint32_t   nakCountReload      :  4;
    uint8_t    interruptScheduleMask;
    uint8_t    splitCompletionMask;
    uint16_t   hubAddr             :  7;
    uint16_t   portNumber          :  7;
    uint16_t   mult                :  2;
    uint32_t   current;
    ehci_qtd_t qtd;
} __attribute__((packed)) ehci_qhd_t;

typedef struct
{
    ehci_qtd_t* qTD;
    void*       qTDBuffer;
    void*       inBuffer;
    size_t      inLength;
} ehci_transaction_t;


void ehci_createQH(ehci_qhd_t* address, uint32_t horizPtr, ehci_qtd_t* firstQTD, uint8_t H, uint32_t device, uint32_t endpoint, uint32_t packetSize);
ehci_qtd_t* ehci_createQTD_SETUP(uintptr_t next, bool toggle, uint32_t tokenBytes, uint32_t type, uint32_t req, uint32_t hiVal, uint32_t loVal, uint32_t index, uint32_t length, void** buffer);
ehci_qtd_t* ehci_createQTD_IO(uintptr_t next, uint8_t direction, bool toggle, uint32_t tokenBytes, void** buffer);

void ehci_addToAsyncScheduler(ehci_t* e, usb_transfer_t* transfer, uint8_t velocity);
void ehci_initializeAsyncScheduler(ehci_t* e);

uint8_t ehci_showStatusbyteQTD(ehci_qtd_t* qTD);


#endif
