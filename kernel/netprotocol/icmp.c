/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "icmp.h"
#include "network/rtl8139.h"
#include "video/console.h"
#include "types.h"

extern uint8_t MAC_address[6];
extern uint8_t IP_address[4];

// Compute Internet Checksum for "count" bytes beginning at location "addr".
int internetChecksum(void *addr, size_t count)
{
    uint32_t sum = 0;
    uint8_t *data = addr;

    while (count > 1)
    {
        // This is the inner loop
        sum += (data[0] << 8) | data[1]; // Use Big Endian
        data += 2;
        count -= 2;
    }
    // Add left-over byte, if any
    if (count > 0)
        sum += data[0] << 8;
    // Fold 32-bit sum to 16 bits
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return ~sum & 0xFFFF;
}

void ICMPAnswerPing(void* data, uint32_t length)
{
    // icmpheader_t icmp;
    icmppacket_t icmp;
    icmppacket_t *rec = data;

    for (uint32_t i = 0; i < 6; i++)
    {
        icmp.eth.recv_mac[i]   = rec->eth.send_mac[i]; // arp->source_mac[i];
        icmp.eth.send_mac[i]   = MAC_address[i];
    }

    icmp.eth.type_len[0] = 0x08;
    icmp.eth.type_len[1] = 0x00;
/*
    icmp.ip.dest_ip[0]     = 192;
    icmp.ip.dest_ip[1]     = 168;
    icmp.ip.dest_ip[2]     = 10;
    icmp.ip.dest_ip[3]     = 5;
*/
    for (uint32_t i = 0; i < 4; i++)
    {
        // reply.arp.dest_ip[i]   = arp->source_ip[i];
        // reply.arp.source_ip[i] = IP_address[i];
        icmp.ip.dest_ip[i]   = rec->ip.source_ip[i];
        icmp.ip.source_ip[i] = IP_address[i];
    }

    icmp.ip.version = 4;
    icmp.ip.ipHeaderLength = sizeof(icmp.ip) / 4;
    icmp.ip.typeOfService = 0;
    icmp.ip.length = htons(sizeof(icmp.ip) + sizeof(icmp.icmp));
    icmp.ip.identification = 0;
    icmp.ip.fragmentation = htons(0x4000);
    icmp.ip.ttl = 128;
    icmp.ip.protocol = 1;
    icmp.ip.checksum = 0;

    icmp.ip.checksum = htons(internetChecksum(&icmp.ip, sizeof(icmp.ip)));

    icmp.icmp.type = ECHO_REPLY;
    icmp.icmp.code = 0;
    icmp.icmp.id = rec->icmp.id;
    icmp.icmp.seqnumber = rec->icmp.seqnumber;
    icmp.icmp.checksum = 0;

    icmp.icmp.checksum = htons(internetChecksum(&icmp.icmp, sizeof(icmp.icmp)));

    transferDataToTxBuffer(&icmp, sizeof(icmp));
    textColor(0x0D); printf("  ICMP Packet send!!! "); textColor(0x03);
    textColor(0x0D); printf("  ICMP Packet: dest_ip: %u.%u.%u.%u", icmp.ip.dest_ip[0], icmp.ip.dest_ip[1], icmp.ip.dest_ip[2], icmp.ip.dest_ip[3]); textColor(0x03);
}

/*
* Copyright (c) 2010 The PrettyOS Project. All rights reserved.
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