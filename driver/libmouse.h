/*
        libmouse
        Copyright (C) 2025 Cat (Ivan Epifanov)

        Permission is hereby granted, free of charge, to any person obtaining
        a copy of this software and associated documentation files (the "Software"),
        to deal in the Software without restriction, including without limitation
        the rights to use, copy, modify, merge, publish, distribute, sublicense,
        and/or sell copies of the Software, and to permit persons
        to whom the Software is furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice
        shall be included in all copies or substantial portions of the Software.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
        INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
        IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
        DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
        ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
        OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef __LIBMOUSE_H__
#define __LIBMOUSE_H__

#include <psp2/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int libmouse_usb_start();
  int libmouse_usb_stop();
  int libmouse_usb_in_attached();
  int libmouse_usb_out_attached();
  int libmouse_usb_read(uint8_t *buf, int size); // max 64 bytes
  int libmouse_usb_write(uint8_t *buf, int size); // max 64 bytes

  // TODO
  // int libmouse_udcd_start(uint8_t type);
  // int libmouse_udcd_write(uint8_t *buf, int size); // max 64 bytes
  // int libmouse_udcd_stop();

#ifdef __cplusplus
}
#endif

#endif // __LIBMOUSE_H__
