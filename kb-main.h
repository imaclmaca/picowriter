/*
 * Header file for the Microwriter / CyKey keyboard emulation.
 */

#ifndef _KB_MAIN_H_
#define _KB_MAIN_H_

#ifdef __cplusplus
 extern "C" {
#endif

// Define the polling rate for the USB HID service
#define PW_POLL  10  // default to 10ms polling rate

// Used to pass a key-combo from the keyboard thread to the USB thread.
// Uses a pico FIFO to pass a unit32_t. This word has 4 "codes" packed into
// as "modifiers", "k1", "k2", "k3"
// At most this supports a 3-key combo, which gamers might find derisory
// but is plenty for emulating the Microwriter!
typedef union
{
    uint32_t u_msg;
    uint8_t  p [4];
} msg_blk;

// defined in kb-main.c
extern uint32_t kc_get (void);

// Defined in usb-stack.c
extern void led_blinking_task(void);
extern void hid_task(void);

// Defined in usb_descriptors.c
void set_serial_string (char const *ser);

#ifdef __cplusplus
 }
#endif

#endif /* _KB_MAIN_H_ */

/* End of File */
