/*
 * Entry point for the Microwriter / CyKey keyboard emulation.
 *
 * The key-code tables defined here are based on what my CyKey does and what I can
 * remember (or look up!) about what the original Microwriter actually did.
 *
 * https://en.wikipedia.org/wiki/Microwriter
 * https://www.sites.google.com/site/cykeybellaire/home
 *
 * This code runs on a Raspberry Pico RP2040 board and uses the tinyusb stack
 * to implement a USB HID keyboard device.
 * (Which is actually more convenient than the IR-dongle that my CyKey had...)
 *
 * The actual keyboard has 8 switches, laid out in broadly the same fashion as
 * the AgendA and CyKey.
 *
 *  The 8 key switches are mapped into a byte as follows:
    ---------------------------------
msb | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | lsb
    ---------------------------------
    | R | N | C | T | I | M | R | P |
    | e | u | a | h | n | i | i | i |
    | p | m | p | u | d | d | n | n |
    | t |   | s | m | e |   | g | k |
    |   |   |   | b | x |   |   | y |
    ---------------------------------
 *
 * Note: GPIO pins 2..9 are used for the 8 bits, since GPIO 0,1 are used for
 * the serial port.
 *
 * Note : The layout is not "symmetrical" with 9 keys like the CyKey and so
 * it does not support the mirrored "left-hand" mode that the CyKey has.
 * Though doing so would only need one more key switch and GPIO line, and if
 * "mirror" mode was selected then the keymap could be bit-reversed and shifted
 * down on read to produce the current 8-bit mask. So do-able...
 *
 */

// Basics to get the pico going...
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"
#include <string.h>
#include <ctype.h>

// tinyusb parts...
#include <bsp/board.h>
#include <tusb.h>

// local parts
#include "kb-main.h"

/* Are we emitting serial debug? */
#define SER_DBG_ON  1  // serial debug on
//#undef SER_DBG_ON      // serial debug off

// Keyboard mapping and decode tables
#define FNK (10)  // Base of the "Function Key" range
#define SPC ' '   // 32 - ASCII space - used to delimit the "private" range

// Internal "private" codes for function keys, etc.
#define DEL  (1)  // DELETE
#define _UP  (2)  // Cursor UP
#define FWD  (3)  // Cursor Forward (RIGHT)
#define PUP  (4)  // Page UP
#define INS  (5)  // INSERT
#define CTR  (6)  // CTRL modifier
#define KPE  (7)  // Keypad Enter key code
#define TAB  '\t' // TAB key (9)
#define RTN  '\n' // Return key (10)

#define F01  (FNK + 1) // 11
#define F02  (FNK + 2)
#define F03  (FNK + 3)
#define F04  (FNK + 4)
#define F05  (FNK + 5) // 15
#define F06  (FNK + 6)
#define F07  (FNK + 7)
#define F08  (FNK + 8)
#define F09  (FNK + 9)
#define F10  (FNK + 10) // 20
#define F11  (FNK + 11)
#define F12  (FNK + 12) // 22
#define A_C  (23)  // Internal code for A/C - Used to generate Alt+Ctrl+<next key press>
#define HOM  (24)  // HOME
#define BCK  (25)  // Cursor BACK (LEFT)
#define DND  (26)  // Document END
#define DWN  (27)  // Cursor DOWN
#define PDN  (28)  // Page DOWN
#define _EC  (29)  // ESC
#define BSP  (30)  // 30 - Backspace
#define ALT  (31)  // 31 - ALT modifier

// convert "internal" codes into USB HID keycodes
static uint8_t const int_codes_table [32] = {
    0,
    HID_KEY_DELETE,
    HID_KEY_ARROW_UP,
    HID_KEY_ARROW_RIGHT,
    HID_KEY_PAGE_UP,
    HID_KEY_INSERT,
    HID_KEY_CONTROL_LEFT, // Can be a modifier
    HID_KEY_KEYPAD_ENTER,
    0, // 8 - unused
    HID_KEY_TAB,
    HID_KEY_ENTER,
    HID_KEY_F1,
    HID_KEY_F2,
    HID_KEY_F3,
    HID_KEY_F4,
    HID_KEY_F5,
    HID_KEY_F6,
    HID_KEY_F7,
    HID_KEY_F8,
    HID_KEY_F9,
    HID_KEY_F10,
    HID_KEY_F11,
    HID_KEY_F12,
    0, // 23 - Alt + Ctrl special modifier "A_C" code
    HID_KEY_HOME,
    HID_KEY_ARROW_LEFT,
    HID_KEY_END,
    HID_KEY_ARROW_DOWN,
    HID_KEY_PAGE_DOWN,
    HID_KEY_ESCAPE,
    HID_KEY_BACKSPACE,
    HID_KEY_ALT_LEFT // Can be a modifier
    };

#define GBP (163) // Old 1252 code for £ sign
#define CER (128) // Old 1252 code for Euro sign
#define WIN (129) // WIN key (as a modifier)
#define WN2 (130) // WIN key (as a key)

/*  The 8 key switches are mapped into a byte as follows:
    ---------------------------------
msb | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | lsb
    ---------------------------------
    | R | N | C | T | I | M | R | P |
    | e | u | a | h | n | i | i | i |
    | p | m | p | u | d | d | n | n |
    | t |   | s | m | e |   | g | k |
    |   |   |   | b | x |   |   | y |
    ---------------------------------
*/

#define THUMB_BIT      0x10
#define CAPS_BIT       0x20
#define NUM_BIT        0x40
#define RPT_BIT        0x80
#define MODIFIERS_MASK 0xF0
#define FINGERS_MASK   0x0F

// The tinyusb ASCII -> HID code table
static uint8_t const conv_table[128][2] =  { HID_ASCII_TO_KEYCODE };

// Lookup tables for the basic finger keys (not thumb) in each "shift" state
// The basic codes for the 4 "finger" keys
static char basic_codes [16] = { 0 , 'u', 's', 'g',
                                'o', 'q', 'n', 'b',
                                'e', 'v', 't', ',',
                                'a', RTN, '.', 'm'};
// With the Thumb modifier
static char thumb_codes [16] = {' ', 'h', 'k', 'j',
                                'c', 'z', 'y', 'x',
                                'i', 'l', 'r', 'w',
                                'd','\'', 'f', 'p'};
// With the Num modifier
static char numbr_codes [16] = {'1', '6', '$', '7',
                                '0', KPE, '#', '8',
                                '2', GBP, '+', '9',
                                '3', '-', '4', '5'};
// With the Num-shift modifier
static char nShft_codes [16] = { 0 , '_', '[', '>',
                                '(', '/', '-', '{',
                                '=', '!', TAB, ',',
                                '+', RTN, '.', '*'};
// With the e-Shift modifier
static char eShft_codes [16] = { 0 , '^', ']', '<',
                                ')','\\', '~', '}',
                                F11, '|', F12, ';',
                                '@', RTN, ':', A_C};

// With the e-Shift Thumb modifier
static char eThmb_codes [16] = {F01, F06, '&', F07,
                                F10, '%', '?', F08,
                                F02, CER, '-', F09,
                                F03, '"', F04, F05};
// "Command" codes
static char cmd_codes [16]   = { 0 , HOM, BCK, DND,
                                KPE, DWN, PDN, _EC,
                                BSP, ALT, TAB, DEL,
                                BSP, _UP, FWD, PUP};
// "Countermand" codes
static char cntrc_codes [16] = { 0 ,  0 ,  0 , HOM,
                                 0 , _UP, PUP, WN2,
                                INS, CTR,  0 , WIN,
                                DEL,  0 , BCK,  0 };

#ifdef SER_DBG_ON
// enable additional serial i/o chatter
static int verbose_debug = 0;
#endif // SER_DBG_ON

// circular buffer for key-codes, pending sending...
#define KC_SZ 8
#define KC_MSK (KC_SZ - 1)
static uint32_t kc_buf [KC_SZ];
static uint32_t kc_in  = 0;
static uint32_t kc_out = 0;

// Used by main() to queue up payloads for sending to the USB hid_task()
static void kc_put (uint32_t uv)
{
    uint32_t next = (kc_in + 1) & KC_MSK;
    if (next == kc_out)
    {
        // queue full, skip this character
        return;
    }
    kc_buf [kc_in] = uv;
    kc_in = next;
}

// Used by hid_task() in usb-stack.c to read payloads to send on the USB
uint32_t kc_get (void)
{
    if (kc_in == kc_out)
    {
        return 0;
    }
    uint32_t uv = kc_buf [kc_out];
    kc_out = (kc_out + 1) & KC_MSK;
    return uv;
}

#ifdef SER_DBG_ON
// Testing support - make each sequence into printable ASCII for debug
static char make_printable (const unsigned char cc)
{
    unsigned char cr = cc;
    if (cr == RTN)
    {
        // No special action
    }
    else if (cr == KPE)
    {
        cr = RTN; // emit as RTN
    }
    else if (cr == BSP)
    {
        printf ("\b "); // erase previous
        cr = '\b';
    }
    else if (cr < SPC)
    {
        cr = '.'; // elide unprintable characters
    }
    else if (cr == CER)
    {
        cr = '*';
    }
    else if ((cr == WIN) || (cr == WN2)) // trap WIN key modifier
    {
        cr = 'W';
    }

    return cr;
} // make_printable
#endif // SER_DBG_ON

// Used to track whether a local shift (caps lock, basically) is currently in force
static unsigned char LCL_SHFT = 0;

// Compose key sequences into USB HID keyboard payloads.
// This runs as a worker thread on the second core of the pico (core-1)
static void make_usb_key (const unsigned char cc)
{
    uint8_t Mods = 0;
    uint8_t Kcode = 0;
    uint8_t start_mods = 0;
    static uint8_t pending_mods = 0;
    msg_blk code;
    code.u_msg = 0;

    if (cc < SPC)
    {
        // Some sort of internal key - determine which...
        Kcode = int_codes_table [cc];

        if ((Kcode == HID_KEY_CONTROL_LEFT) || (Kcode == HID_KEY_ALT_LEFT) || (cc == A_C))
        {
            // Start a modifier sequence
            if (Kcode)
            {
                start_mods = Kcode;
                Kcode = 0;
            }
            else
            {
                start_mods = A_C;
            }
        }
    }
    else if (cc < 128)
    {
        if (conv_table[cc][0])
        {
            Mods = KEYBOARD_MODIFIER_LEFTSHIFT;
        }
        Kcode = conv_table[cc][1];
    }
    else if (cc == CER) // Euro symbol €
    {
        Mods = HID_KEY_ALT_RIGHT;
        Kcode = HID_KEY_4; // AlrGr + 4 works for UK layouts...
    }
    else if (cc == GBP) // GBP symbol $
    {
        Mods = KEYBOARD_MODIFIER_LEFTSHIFT;
        Kcode = HID_KEY_3; // Shift-3 is correct for UK layouts
    }
    else if (cc == WIN) // This is WIN as a modifier
    {
        // Mods = KEYBOARD_MODIFIER_LEFTGUI;
        Kcode = 0;
        start_mods = HID_KEY_GUI_LEFT;
    }
    else if (cc == WN2) // This is WIN as a key on its own
    {
        Mods = KEYBOARD_MODIFIER_LEFTGUI;
        Kcode = HID_KEY_GUI_LEFT;
    }

    if (start_mods)
    {
        pending_mods = start_mods;
        Kcode = 0;  // ensure nothing is sent this cycle
    }
    else if (pending_mods)
    {
        if (pending_mods == A_C)
        {
            code.p[3] = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTALT;
            code.p[2] = HID_KEY_CONTROL_LEFT;
            code.p[1] = HID_KEY_ALT_LEFT;
            code.p[0] = Kcode;
        }
        else if (pending_mods == HID_KEY_CONTROL_LEFT)
        {
            code.p[3] = KEYBOARD_MODIFIER_LEFTCTRL;
            code.p[2] = HID_KEY_CONTROL_LEFT;
            code.p[1] = Kcode;
        }
        else if (pending_mods == HID_KEY_ALT_LEFT)
        {
            code.p[3] = KEYBOARD_MODIFIER_LEFTALT;
            code.p[2] = HID_KEY_ALT_LEFT;
            code.p[1] = Kcode;
        }
        else if (pending_mods == HID_KEY_GUI_LEFT)
        {
            code.p[3] = KEYBOARD_MODIFIER_LEFTGUI;
            code.p[2] = HID_KEY_GUI_LEFT;
            code.p[1] = Kcode;
        }
        pending_mods = 0;
    }
    else // send the current key
    {
        code.p[3] = Mods;
        code.p[2] = Kcode;
    }

    // If there is a key press ready, pass it to the main thread for processing / sending
    if (Kcode)
    {
        if (multicore_fifo_wready ())
        {
            multicore_fifo_push_blocking (code.u_msg);
        }
    }
} // make_usb_key

// Used to simplify handling shift states on basic ASCII codes
static char make_upper (const char cc)
{
    char cr = cc;
    if ((cr >= 'a') && (cr <= 'z'))
    {
        cr = toupper (cr);
    }
    return cr;
} // make_upper

static unsigned char CAPS = 0;   // 0 = OFF, 1 - transient, 2 - Lock
static unsigned char NUM_LK = 0; // 0 = OFF, 1 - transient, 2 - Lock
static unsigned char SHFTE  = 0; // 0 = OFF, 1 - transient, does not lock

// Decodes the key combinations into something like ASCII we can use for the USB HID messages
static char decode_bits (const unsigned char bits)
{
    const unsigned char Fset = bits & FINGERS_MASK;
    const unsigned char Mods = bits & MODIFIERS_MASK;

#ifdef SER_DBG_ON
    if (verbose_debug)
    {
        printf ("\n0x%02X - 0x%02X 0x%02X (%d, %d, %d) -- ", bits, Mods, Fset, CAPS, NUM_LK, SHFTE);
    }
#endif // SER_DBG_ON

    if ((Mods == 0) && (Fset)) // no modifier bits are set, but some keys are pressed
    {
        if (SHFTE)
        {
            SHFTE = 0; // clear a transient SHFTE eShift
            return eShft_codes [Fset];
        }
        if (NUM_LK)
        {
            if (NUM_LK == 1) NUM_LK = 0; // clear a transient NUM_LK Shift
            return nShft_codes [Fset];
        }
        if (CAPS)
        {
            if (CAPS == 1) CAPS = 0; // clear a transient Caps Shift
            return make_upper (basic_codes [Fset]);
        }
        return basic_codes [Fset];
    }
    else if (Mods == THUMB_BIT) // Thumb is the only modifier set
    {
        if (SHFTE)
        {
            SHFTE = 0; // clear a transient SHFTE Shift
            return eThmb_codes [Fset];
        }
        if (NUM_LK)
        {
            if (NUM_LK == 1) NUM_LK = 0; // clear a transient NUM_LK Shift
            return numbr_codes [Fset];
        }
        if (CAPS)
        {
            if (CAPS == 1) CAPS = 0; // clear a transient Caps Shift
            return make_upper (thumb_codes [Fset]);
        }
        return thumb_codes [Fset];
    }
    else if (Mods == NUM_BIT) // Numbers is the only modifier set
    {
        if (SHFTE)
        {
            SHFTE = 0; // clear a transient SHFTE Shift
            //return eThmb_codes [Fset];
            return cntrc_codes [Fset]; // SHIFT-E followed by NUM is a countermand
        }
        return numbr_codes [Fset];
    }
    else if (bits == CAPS_BIT) // Only the Caps key is pressed, no other keys
    {
        LCL_SHFT = 1; // Record that a shift was pressed
        if (CAPS >= 2) // already locked, so next push clears it
        {
            CAPS = 0;
        }
        else // Set the CAPS key; if already set, then "lock" it
        {
            ++CAPS;
        }
    }
    else if (Mods == CAPS_BIT) // Only the Caps modifier is set but SOME finger keys pressed  - command codes
    {
        // Generate command code
        return cmd_codes [Fset];
    }
    else if (bits == (THUMB_BIT | NUM_BIT)) // Thumb and NUM pressed together, no other keys
    {
        if (NUM_LK >= 2) // already locked, so next push clears it
        {
            NUM_LK = 0;
        }
        else // Set the NUM_LK key; if already set, then "lock" it
        {
            ++NUM_LK;
        }
    }
    else if (bits == (THUMB_BIT | CAPS_BIT)) // Thumb and CAPS pressed together, no other keys - clear shift
    {
        CAPS = 0;
        NUM_LK = 0;
        SHFTE  = 0;
    }
    else if (bits == (NUM_BIT | CAPS_BIT)) // NUM and CAPS pressed together, no other keys - eShift
    {
        SHFTE  = 1;
    }
    else if (Mods == (NUM_BIT | CAPS_BIT)) // NUM and CAPS pressed together, SOME finger keys pressed  - countermands
    {
        // Generate countermands keycode
        return cntrc_codes [Fset];
    }
    return 0;
} // decode_bits

/* The "main" task on the second core.
 * This manages the reading and initial decoding of the keyboard matrix. */
void keyboard_task (void)
{
    // signal to the primary thread that this worker thread is ready
    multicore_fifo_push_blocking (99);

    uint sum_bits = 0;

    // Forever - scan for key presses, ORing them all together.
    while (true)
    {
        // What keys are currently pressed?
        uint32_t all_bits = gpio_get_all();
        all_bits = ~all_bits; // keys are active low, invert the read
        all_bits = all_bits >> 2; // shift bits [9:2] down to become [7:0]
        all_bits = all_bits & 0xFF; // Mask, just in case...

        // OR all the bits together
        if (all_bits)
        {
            sum_bits |= all_bits;
        }
        // When ALL keys are released, decode the combo.
        else if ((sum_bits != 0) && (all_bits == 0))
        {
            // send a char code
            char cc = decode_bits (sum_bits);
            if (cc)
            {
#ifdef SER_DBG_ON
                printf ("%c", make_printable (cc));
#endif // SER_DBG_ON
                make_usb_key (cc);
            }
            // clear out the code for next pass
            sum_bits = 0;
        }

        sleep_ms (20);
    }
} // keyboard_task

// main - initialize the board, start tinyusb, start the worker thread
int main()
{
    board_init();

    // Try to grab the Pico board ID info.
    char id_string [(2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES) + 1]; // Should be 17 - PICO_UNIQUE_BOARD_ID_SIZE_BYTES == 8
    pico_get_unique_board_id_string (id_string, 17);
    set_serial_string (id_string);

#ifdef SER_DBG_ON
    pico_unique_board_id_t id_out;
    pico_get_unique_board_id (&id_out);
#endif // SER_DBG_ON

    // enable the board LED - we flash that to show USB state etc.
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Init the keyboard GPIO lines [9:2] for input with pull-ups
    int idx;
    for (idx = 2; idx <= 9; ++idx)
    {
        gpio_init (idx);
        gpio_set_dir(idx, GPIO_IN);
        gpio_pull_up (idx);
    }

    tusb_init(); // start tinyusb

#ifdef SER_DBG_ON
    stdio_init_all(); // start the pico stdio for debug support
    printf ("\n-- PicoWriter starting --\n");

    printf ("Device ID: %s\n", id_string);
    for (idx = 0; idx < 8; ++idx)
    {
        printf ("%02X ", id_out.id[idx]);
    }
    printf ("\nID done\n");
#endif // SER_DBG_ON

    // Start the keyboard scanner thread on core-1
    multicore_launch_core1 (keyboard_task);
    // Wait for it to start up
    uint32_t g = multicore_fifo_pop_blocking();

    // cursory check that core-1 started OK
    if (g == 99)
    {
#ifdef SER_DBG_ON
        printf ("Core-1 OK\n");
#endif // SER_DBG_ON
    }
    else
    {
#ifdef SER_DBG_ON
        printf ("Bad response from Core-1\n");
#endif // SER_DBG_ON
    }

    // forever - read keycodes from core-1 and pass them to the hid_task() for sending
    while (true)
    {
        if (multicore_fifo_rvalid ()) // data pending in FIFO
        {
            uint32_t uv = multicore_fifo_pop_blocking();
            // queue the key-down
            kc_put (uv);

#ifdef SER_DBG_ON
            // diagnostic - echo the keycode to the serial i/o
            printf ("  %08X \b\b\b\b\b\b\b\b\b\b\b", (unsigned)uv);
#endif // SER_DBG_ON
        }

        tud_task(); // tinyusb device task
        led_blinking_task(); // LED heartbeat (in usb-stack.c)
        hid_task(); // HID processing task (in usb-stack.c)
    }
    return 0;
} // main

// end of file
