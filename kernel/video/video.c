/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "util.h"
#include "console.h"
#include "task.h"
#include "video.h"
#include "kheap.h"
#include "timer.h"
#include "filesystem/fat.h"

uint16_t* vidmem = (uint16_t*)0xB8000;

char infoBar[3][81]; // Infobar with 3 lines and 80 columns

static const uint8_t LINES      = 50;
static const uint8_t USER_BEGIN =  2; // Reserving  Titlebar + Separation
static const uint8_t USER_END   = 48; // Reserving Statusbar + Separation
 
position_t cursor;
uint8_t attrib = 0x0F; // white text on black ground

void clear_screen()
{
    memsetw(vidmem, 0x00, COLUMNS * LINES);
    update_cursor();
}

uint8_t AsciiToCP437(uint8_t ascii)
{
    switch (ascii)
    {
        case 0xE4:  return 0x84;  // �
        case 0xF6:  return 0x94;  // �
        case 0xFC:  return 0x81;  // �
        case 0xDF:  return 0xE1;  // �
        case 0xA7:  return 0x15;  // �
        case 0xB0:  return 0xF8;  // �
        case 0xC4:  return 0x8E;  // �
        case 0xD6:  return 0x99;  // �
        case 0xDC:  return 0x9A;  // �
        case 0xB2:  return 0xFD;  // �
        case 0xB3:  return 0x00;  // � <-- not available
        case 0x80:  return 0xEE;  // � <-- Greek epsilon used
        case 0xB5:  return 0xE6;  // �
        default:    return ascii; // to be checked for more deviations
    }
}

static void kputch(char c)
{
    uint8_t uc = AsciiToCP437((uint8_t)c); // no negative values

    uint16_t* pos;
    uint32_t att = attrib << 8;

    switch (uc) {
        case 0x08: // backspace: move the cursor one space backwards and delete
            if (cursor.x)
            {
                --cursor.x;
                kputch(' ');
                --cursor.x;
            }
            else if (cursor.y>0)
            {
                cursor.x=COLUMNS-1;
                --cursor.y;
                kputch(' ');
                cursor.x=COLUMNS-1;
                --cursor.y;
            }
            break;
        case 0x09: // tab: increment csr_x (divisible by 8)
            cursor.x = (cursor.x + 8) & ~(8 - 1);
            break;
        case '\r': // cr: cursor back to the margin
            cursor.x = 0;
            break;
        case '\n': // newline: like 'cr': cursor to the margin and increment csr_y
            cursor.x = 0; ++cursor.y;
            break;
        default:
            if (uc != 0)
            {
                pos = vidmem + (cursor.y * COLUMNS + cursor.x);
                *pos = uc | att; // character AND attributes: color
                ++cursor.x;
            }
            break;
    }

    if (cursor.x >= COLUMNS) // cursor reaches edge of the screen's width, a new line is inserted
    {
        cursor.x = 0;
        ++cursor.y;
    }
}

static void kputs(const char* text)
{
    for (; *text; kputch(*text), ++text);
}

void kprintf(const char* message, uint32_t line, uint8_t attribute, ...)
{
    attrib = attribute;
    cursor.x = 0; cursor.y = line;

    va_list ap;
    va_start(ap, attribute);
    char buffer[32]; // Larger is not needed at the moment

    for (; *message; message++)
    {
        switch (*message)
        {
            case '%':
                switch (*(++message))
                {
                    case 'u':
                        utoa(va_arg(ap, uint32_t), buffer);
                        kputs(buffer);
                        break;
                    case 'f':
                        ftoa(va_arg(ap, double), buffer);
                        kputs(buffer);
                        break;
                    case 'i': case 'd':
                        itoa(va_arg(ap, int32_t), buffer);
                        kputs(buffer);
                        break;
                    case 'X': /// TODO: make it standardized
                        i2hex(va_arg(ap, int32_t), buffer, 8);
                        kputs(buffer);
                        break;
                    case 'x':
                        i2hex(va_arg(ap, int32_t), buffer, 4);
                        kputs(buffer);
                        break;
                    case 'y':
                        i2hex(va_arg(ap, int32_t), buffer, 2);
                        kputs(buffer);
                        break;
                    case 's':
                        kputs(va_arg (ap, char*));
                        break;
                    case 'c':
                        kputch((int8_t)va_arg(ap, int32_t));
                        break;
                    case 'v':
                        attrib = (attribute >> 4) | (attribute << 4);
                        kputch(*(++message));
                        attrib = attribute;
                        break;
                    case '%':
                        kputch('%');
                        break;
                    default:
                        --message;
                        break;
                }
                break;
            default:
                kputch(*message);
                break;
        }
    }
}

void writeInfo(uint8_t line, char* args, ...)
{
    va_list ap;
    va_start(ap, args);
    vsnprintf(infoBar[line], 81, args, ap);
    refreshUserScreen();
}

void refreshUserScreen()
{
    // Printing titlebar
    kprintf("PrettyOS [Version %s]                                                            ", 0, 0x0C, version);

    if (displayedConsole == KERNELCONSOLE_ID)
    {
        cursor.x = COLUMNS - 5;
        kputs("Shell");
    }
    else
    {
        char Buffer[70];
        snprintf(Buffer, 70, "Console %u: %s", displayedConsole, reachableConsoles[displayedConsole]->name);
        cursor.x = COLUMNS - strlen(Buffer);
        cursor.y = 0;
        kputs(Buffer);
    }
    kprintf("--------------------------------------------------------------------------------", 1, 7); // Separation
    if(reachableConsoles[displayedConsole]->showInfobar)
    {
        // copying content of visible console to the video-ram
        memcpy(vidmem + USER_BEGIN * COLUMNS, reachableConsoles[displayedConsole]->vidmem, COLUMNS * (USER_LINES-4) * 2);
        memsetw(vidmem + (USER_BEGIN + USER_LINES - 3) * COLUMNS, 0, 3 * COLUMNS); // Clearing info-area
        kprintf("--------------------------------------------------------------------------------", 44, 7); // Separation
        kprintf(infoBar[0], 45, 14);
        kprintf(infoBar[1], 46, 14);
        kprintf(infoBar[2], 47, 14);
    }
    else
    {
        // copying content of visible console to the video-ram
        memcpy(vidmem + USER_BEGIN * COLUMNS, reachableConsoles[displayedConsole]->vidmem, COLUMNS * USER_LINES*2);
    }
    kprintf("--------------------------------------------------------------------------------", 48, 7); // Separation

    cursor.y = reachableConsoles[displayedConsole]->cursor.y;
    cursor.x = reachableConsoles[displayedConsole]->cursor.x;
    update_cursor();
}

diskType_t* ScreenDest = &FLOPPYDISK; // HACK
void mt_screenshot()
{
    printf("Screenshot (Thread)\n");
    create_thread(&screenshot);
}

extern disk_t* disks[DISKARRAYSIZE]; // HACK
void screenshot()
{
    int32_t NewLine = 0;

    // buffer for video screen
    uint8_t* videoscreen = malloc(4000+98, 0, "vidscr-scrshot"); // only signs, no attributes, 49 times CR LF at line end

    for (uint16_t i=0; i<4000;i++)
    {
        uint16_t j=i+2*NewLine;
        videoscreen[j] = *(char*)(vidmem+i); // only signs, no attributes
        if ((i%80 == 79) && (i!=3999)) // for last row no NewLine
        {
            videoscreen[j+1]= 0xD; // CR
            videoscreen[j+2]= 0xA; // LF
            NewLine++;
        }
    }

    char Pfad[20];
    for(int i = 0; i < DISKARRAYSIZE; i++) // HACK
    {
        if(disks[i] && disks[i]->type == ScreenDest && (disks[i]->partition[0]->subtype == FAT12 || disks[i]->partition[0]->subtype == FAT16 || disks[i]->partition[0]->subtype == FAT32))
        {
            snprintf(Pfad, 20, "%u:/screen.txt", i+1);
            break;
        }
    }

    file_t* file = fopen(Pfad, "a+");
    if (file) // check for NULL pointer, otherwise #PF
    {
        fwrite((void*)videoscreen, 1, 4098, file);
        fclose(file);
    }
    else
    {
        printf("\nError: file could not be opened!");
    }
    free(videoscreen);

    /*
    //rename test
    waitForKeyStroke();
    uint32_t error = rename(Pfad,"scrnew.txt");
    printf("\nrename test: error: %u", error);

    
    // remove test
    waitForKeyStroke();
    error = remove(Pfad);
    printf("\nremove test: error: %u", error);
    waitForKeyStroke();
    */
}

/*
* Copyright (c) 2009-2010 The PrettyOS Project. All rights reserved.
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