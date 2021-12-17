# picowriter
Use a Raspberry Pico to emulate a MicroWriter keyboard

The key-code tables defined here are based on what my CyKey does and what I can
remember (or look up!) about what the original Microwriter actually did.

Microwriter described here: [Microwriter](https://en.wikipedia.org/wiki/Microwriter)

CyKey from Bellaire Electronics described here: [CyKey](https://www.sites.google.com/site/cykeybellaire/home)

This code runs on a Raspberry Pico RP2040 board and uses the tinyusb stack
to implement a USB HID keyboard device.
(Which is actually more convenient than the IR-dongle that my CyKey had...)
The actual keyboard has 8 switches, laid out in broadly the same fashion as
the AgendA and CyKey.

The 8 key switches are mapped into a byte as follows:

```
    ---------------------------------
msb | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | lsb
    ---------------------------------
    | R | N | C | T | I | M | R | P |
    | e | u | a | h | n | i | i | i |
    | p | m | p | u | d | d | n | n |
    | t |   | s | m | e |   | g | k |
    |   |   |   | b | x |   |   | y |
    ---------------------------------
```

Note: GPIO pins 2..9 are used for the 8 bits, since GPIO 0,1 are used for
the serial port.


Note : The layout is not "symmetrical" like the CyKey and so it does not
support the mirrored "left-hand" mode that the CyKey has. Though doing so
would only need one more key switch and GPIO line, and if "mirror" mode
is selected then the keymap would be bit-reversed and shifted on read to
produce the current 8-bit mask. So do-able...
