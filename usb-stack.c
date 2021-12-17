/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/*
 * This is basically just Ha Thach's USB HID example code, bodged up to support
 * my simple keyboard only mechanism.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

// local parts
#include "usb_descriptors.h"
#include "kb-main.h"

/* Blink pattern */
enum  {
  BLINK_NONE = 0,
  BLINK_NOT_MOUNTED,
  BLINK_MOUNTED,
  BLINK_SUSPENDED
};

#define BLINK_LEN   4
#define BLINK_MASK (BLINK_LEN - 1)
static uint32_t blink_phase = 0;

static const uint16_t blink_not_mounted [BLINK_LEN] = {80, 500, 80, 500}; // SHORT,long,SHORT,long
static const uint16_t blink_mounted [BLINK_LEN] = {80, 80, 80, 1900}; // SHORT,short,SHORT,long
static const uint16_t blink_suspended [BLINK_LEN] = {80, 1700, 80, 1700}; // SHORT,long,SHORT,long

// defined in kb-main.c
extern uint32_t kc_get (void);

// Used to track the LED flash state
static uint32_t blink_state = BLINK_NOT_MOUNTED;

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_state = BLINK_MOUNTED;
} // tud_mount_cb

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_state = BLINK_NOT_MOUNTED;
} // tud_umount_cb

// Invoked when USB is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup then
// within 7ms, device must draw an average current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_state = BLINK_SUSPENDED;
} // tud_suspend_cb

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
  blink_state = BLINK_MOUNTED;
} // tud_resume_cb

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  switch(report_id)
  {
    case REPORT_ID_KEYBOARD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_keyboard_key = false;

      if ( btn )
      {
        msg_blk code;
        code.u_msg = btn;
        uint8_t Mods = code.p[3];
        uint8_t keycode[6] = { 0 };
        keycode[0] = code.p[2];
        keycode[1] = code.p[1];
        keycode[2] = code.p[0];
        keycode[3] = 0;
        keycode[4] = 0;
        keycode[5] = 0;

        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, Mods, keycode); // KEY DOWN
        has_keyboard_key = true;
      }
      else
      {
        // send empty key report if previously has key pressed - KEY UP effectively
        if (has_keyboard_key)
        {
          tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
          has_keyboard_key = false;
        }
      }
    }
    break;

    /* The original example also provided these endpoints, but we do not use them here... */
    // All the other endpoints from the example code are skipped
    //case REPORT_ID_MOUSE:
    //case REPORT_ID_CONSUMER_CONTROL:
    //case REPORT_ID_GAMEPAD:
    default:
    break;
  }
} // send_hid_report

// Every 10ms, we will send 1 report for each HID profile (keyboard, mouse etc.)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < interval_ms) return; // not enough time has elapsed since last poll
  start_ms += interval_ms;

  uint32_t const btn = kc_get ();

  // Remote wakeup
  if ( tud_suspended() && btn )
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  }
  else
  {
    // Send the 1st element of the report chain, the rest will be sent by tud_hid_report_complete_cb()
    send_hid_report(REPORT_ID_KEYBOARD, btn);
  }
} // hid_task

// Invoked when sent REPORT successfully to host
// Application can use this to chain to the next report - though since we only have the keyboard now,
// that seems unlikely to actually do anything!
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint8_t len)
{
  (void) instance;
  (void) len;

  uint8_t next_report_id = report[0] + 1;

  if (next_report_id < REPORT_ID_COUNT)
  {
    send_hid_report(next_report_id, board_button_read());
  }
} // tud_hid_report_complete_cb

// Invoked when we receive a GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
} // tud_hid_get_report_cb

/* Invoked when we received SET_REPORT control request or
 * receive data on OUT endpoint ( Report ID = 0, Type = 0 )
 *
 * Here, this is only checking for the CapsLock message from the host,
 * which PicoWriter ignores at present - though it possibly could make
 * use of it.
 * All this does is change the board LED, in effect.
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock in this case - which we do not currently even use!
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        // Capslock On: disable blink, turn led on
        blink_state = BLINK_NONE;
        board_led_write(true);
      }else
      {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_state = BLINK_MOUNTED;
      }
    }
  }
} // tud_hid_set_report_cb

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static int led_state = 0;

  // blink is disabled - typically happens when CapsLock is set ON by tud_hid_set_report_cb()
  if (!blink_state) return;

  const uint16_t *seq = NULL;
  switch (blink_state)
  {
    case BLINK_NOT_MOUNTED:
    seq = blink_not_mounted;
    break;

    case BLINK_MOUNTED:
    seq = blink_mounted;
    break;

    default:
    seq = blink_suspended;
    break;
  }

  uint32_t delay_for = seq [blink_phase];

  // Blink every "delay_for" ms
  if ( (board_millis() - start_ms) < delay_for) return; // not enough time has elapsed
  start_ms += delay_for;
  blink_phase = (blink_phase + 1) & BLINK_MASK;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle LED state
} // led_blinking_task

// End of File //
