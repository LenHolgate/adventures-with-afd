# Adventures with \Device\Afd

Some simple code to learn about how to access the Windows network stack
using polling and \Device\Afd as used by https://lenholgate.com/blog/2023/04/adventures-with-afd.html

Inspired by https://2023.notgull.net/device-afd/

Based on code found in:
 * https://github.com/piscisaureus/wepoll
 * https://github.com/smol-rs/polling
 * https://github.com/c-ares/c-ares/blob/main/src/lib/ares_event_win32.c

