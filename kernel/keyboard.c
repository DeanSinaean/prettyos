/*
*  license and disclaimer for the use of this source code as per statement below
*  Lizenz und Haftungsausschluss f�r die Verwendung dieses Sourcecodes siehe unten
*/

#include "keyboard.h"
#include "util.h"
#include "task.h"
#include "irq.h"

#if KEYMAP  == GER
#include "keyboard_GER.h"
#else //US-Keyboard if nothing else is defined
#include "keyboard_US.h"
#endif

static const KEY_t scancodeToKey_default[] =
{
//  0  1  2  3  4  5  6  7
    0, KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, // 0
    KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACK, KEY_TAB,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, // 1
    KEY_O, KEY_P, KEY_OSQBRA, KEY_CSQBRA, KEY_ENTER, KEY_LCTRL, KEY_A, KEY_S,
    KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMI, // 2
    KEY_APPOS, KEY_ACC, KEY_LSHIFT, KEY_BACKSL, KEY_Z, KEY_X, KEY_C, KEY_V,
    KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RSHIFT, KEY_KPMULT, // 3
    KEY_LALT, KEY_SPACE, KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NUM, KEY_SCROLL, KEY_KP7, // 4
    KEY_KP8, KEY_KP9, KEY_KPMIN, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KPPLUS, KEY_KP1,
    KEY_KP2, KEY_KP3, KEY_KP0, KEY_KPDOT, 0, 0, KEY_GER_ABRA, KEY_F11, // 5
    KEY_F12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0,
};

static const KEY_t scancodeToKey_E0[] =
{
//  0  1  2  3  4  5  6  7
    0, 0, 0, 0, 0, 0, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, KEY_PRINT, // 3
    KEY_ALTGR, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 4
    KEY_ARRU, 0, 0, KEY_ARRL, 0, KEY_ARRR, 0, 0,
    KEY_ARRD, 0, 0, 0, 0, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0,
};

static bool pressedKeys[__KEY_LAST]; // for monitoring pressed keys


static void keyboard_handler(registers_t* r);

void keyboard_install()
{
    memset(pressedKeys, false, __KEY_LAST*sizeof(bool));
    irq_installHandler(IRQ_KEYBOARD, keyboard_handler); // Installs 'keyboard_handler' to IRQ_KEYBOARD

    while (inportb(0x64) & 1) // wait until buffer is empty
    {
        inportb(0x60);
    }
}

static uint8_t getScancode()
{
    volatile uint8_t scancode = 0;

    if (inportb(0x64)&1)
        scancode = inportb(0x60);   // 0x60: get scan code from the keyboard

    // ACK: toggle bit 7 at port 0x61
    uint8_t port_value = inportb(0x61);
    outportb(0x61, port_value |  0x80); // 0->1
    outportb(0x61, port_value &~ 0x80); // 1->0

    return(scancode);
}

static KEY_t scancodeToKey(uint8_t scancode, bool* make)
{
    KEY_t key = __KEY_INVALID;

    static uint8_t prevScancode = 0; // Stores the previous scancode. For E1 codes it stores always the first byte (0xE1).
    static uint8_t byteCounter = 1; // Only needed for E1 codes

    *make = !(scancode & 0x80); // make code

    if(scancode == 0xE0) // First byte of E0 code
    {
        prevScancode = 0xE0;
    }
    else if(scancode == 0xE1) // First byte of E1 code
    {
        prevScancode = 0xE1;
        byteCounter = 1;
    }
    else
    {
        if(prevScancode == 0xE0) // Second byte of E0 code
        {
            prevScancode = 0; // Last scancode is not interesting in this case
            key = scancodeToKey_E0[scancode & 0x7F];
            pressedKeys[key] = !(scancode & 0x80);
        }
        else if(prevScancode == 0xE1) // Second or third byte of E1 code. HACK: We assume, that all E1 codes mean the pause key
        {
            byteCounter++;
            if(byteCounter == 3)
                return(KEY_PAUSE);
        }
        else // Default code
        {
            prevScancode = 0; // Last scancode is not interesting in this case
            key = scancodeToKey_default[scancode & 0x7F];
            pressedKeys[key] = !(scancode & 0x80);
        }
    }
    return(key);
}

static char keyToASCII(KEY_t key)
{
    uint8_t retchar = 0; // The character that returns the scan code to ASCII code

    // Fallback mechanism
    if (pressedKeys[KEY_ALTGR])
    {
        if (pressedKeys[KEY_LSHIFT] || pressedKeys[KEY_RSHIFT])
        {
            retchar = keyToASCII_shiftAltGr[key];
        }
        if (!(pressedKeys[KEY_LSHIFT] || pressedKeys[KEY_RSHIFT]) || retchar == 0) // if shift is not pressed or if there is no key specified for ShiftAltGr (so retchar is still 0)
        {
            retchar = keyToASCII_altGr[key];
        }
    }
    if (!pressedKeys[KEY_ALTGR] || retchar == 0) // if AltGr is not pressed or if retchar is still 0
    {
        if (pressedKeys[KEY_LSHIFT] || pressedKeys[KEY_RSHIFT])
        {
            retchar = keyToASCII_shift[key];
        }
        if (!(pressedKeys[KEY_LSHIFT] || pressedKeys[KEY_RSHIFT]) || retchar == 0) // if shift is not pressed or if retchar is still 0
        {
            retchar = keyToASCII_default[key];
        }
    }

    // filter special key combinations
    if (pressedKeys[KEY_LALT]) // Console-Switching
    {
        if (retchar == 'm')
        {
            console_display(KERNELCONSOLE_ID);
            return(0);
        }
        if (ctoi(retchar) != -1)
        {
            console_display(1+ctoi(retchar));
            return 0;
        }
    }

    if(key == KEY_PRINT || key == KEY_F12) // Save content of video memory. F12 is alias for PrintScreen due to problems in some emulators
    {
        takeScreenshot();
    }

    return(retchar);
}

static void keyboard_handler(registers_t* r)
{
    // Get scancode
    uint8_t scancode = getScancode();
    bool make = false;

    // Find out key. Issue events.
    KEY_t key = scancodeToKey(scancode, &make);
    if(key == __KEY_INVALID)
        return;

    if(make)
        for(dlelement_t* e = console_displayed->tasks->head; e != 0; e = e->next)
            event_issue(((task_t*)(e->data))->eventQueue, EVENT_KEY_DOWN, &key, sizeof(KEY_t));
    else
    {
        for(dlelement_t* e = console_displayed->tasks->head; e != 0; e = e->next)
            event_issue(((task_t*)(e->data))->eventQueue, EVENT_KEY_UP, &key, sizeof(KEY_t));
        return;
    }

    // Find out ASCII representation of key. Issue events.
    char ascii = keyToASCII(key);
    if(ascii != 0)
        for(dlelement_t* e = console_displayed->tasks->head; e != 0; e = e->next)
            event_issue(((task_t*)(e->data))->eventQueue, EVENT_TEXT_ENTERED, &ascii, sizeof(char));
}

char getch()
{
    char ret = 0;
    EVENT_t ev = event_poll(&ret, 1, EVENT_NONE);

    while(ev != EVENT_TEXT_ENTERED)
    {
        if(ev == EVENT_NONE)
            waitForEvent(0);
        ev = event_poll(&ret, 1, EVENT_NONE);
    }
    return(ret);
}

bool keyPressed(KEY_t Key)
{
    return(pressedKeys[Key]);
}

/*
* Copyright (c) 2009-2011 The PrettyOS Project. All rights reserved.
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
