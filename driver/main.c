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

#include "libmouse_private.h"

#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/debug.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/threadmgr/event_flags.h>
#include <psp2kern/usbd.h>
#include <psp2kern/usbserv.h>
#include <string.h>


#define USB_ENDPOINT_OUT 0x02
#define USB_ENDPOINT_IN 0x81

#define EVF_SEND 1
#define EVF_RECV 2
#define EVF_CTRL 4

SceUID transfer_ev;
static struct device_context ctx;

static uint8_t started = 0;
static uint8_t in_plugged = 0;
static uint8_t out_plugged = 0;
static int transferred;

int libmouse_probe(int device_id);
int libmouse_attach(int device_id);
int libmouse_detach(int device_id);

static const SceUsbdDriver libmouseDriver = {
    .name   = "libmouse",
    .probe  = libmouse_probe,
    .attach = libmouse_attach,
    .detach = libmouse_detach,
};

#ifndef NDEBUG
#define _error_return(code, str)                                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    ksceDebugPrintf("%s\n", str);                                                                                      \
    EXIT_SYSCALL(state);                                                                                               \
    return code;                                                                                                       \
  } while (0);

#define trace(...) ksceDebugPrintf(__VA_ARGS__)

#else
#define _error_return(code, str)                                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    EXIT_SYSCALL(state);                                                                                               \
    return code;                                                                                                       \
  } while (0);

#define trace(...)

#endif

static int _init_ctx()
{
  ctx.in_pipe_id        = 0;
  ctx.out_pipe_id       = 0;
  ctx.control_pipe_id   = 0;

  return 0;
}

static int libmouse_sysevent_handler(int resume, int eventid, void *args, void *opt)
{
  if (resume && started)
  {
    ksceUsbServMacSelect(2, 0); // re-set host mode
  }
  return 0;
}

static void _callback_control(int32_t result, int32_t count, void *arg)
{
  trace("config cb result: %08x, count: %d\n", result, count);
  ksceKernelSetEventFlag(transfer_ev, EVF_CTRL);
}

static void _callback_send(int32_t result, int32_t count, void *arg)
{
  trace("send cb result: %08x, count: %d\n", result, count);
  if (result == 0)
    *(int *)arg = count;
  ksceKernelSetEventFlag(transfer_ev, EVF_SEND);
}

static void _callback_recv(int32_t result, int32_t count, void *arg)
{
  trace("recv cb result: %08x, count: %d\n", result, count);
  *(int *)arg = count;
  ksceKernelSetEventFlag(transfer_ev, EVF_RECV);
}

int _control_transfer(int rtype, int req, int val, int idx, void *data, int len)
{
  SceUsbdDeviceRequest _dr;
  _dr.bmRequestType = rtype; // (0x02 << 5)
  _dr.bRequest      = req;
  _dr.wValue        = val;
  _dr.wIndex        = idx;
  _dr.wLength       = len;

  int ret = ksceUsbdControlTransfer(ctx.control_pipe_id, &_dr, data, _callback_control, NULL);
  if (ret < 0)
    return ret;
  trace("waiting ef (cfg)\n");
  ksceKernelWaitEventFlag(transfer_ev, EVF_CTRL, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);
  return 0;
}

int _send(unsigned char *request, unsigned int length)
{
  transferred = 0;

  // transfer
  trace("sending 0x%08x\n", request);
  int ret = ksceUsbdBulkTransfer(ctx.out_pipe_id, request, length, _callback_send, &transferred);
  trace("send 0x%08x\n", ret);
  if (ret < 0)
    return ret;
  // wait for eventflag
  trace("waiting ef (send)\n");
  ksceKernelWaitEventFlag(transfer_ev, EVF_SEND, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);

  return transferred;
}

int _recv(unsigned char *result, unsigned int length)
{
  transferred = 0;

  // transfer
  trace("sending (recv) 0x%08x, len 64\n", result);
  int ret = ksceUsbdBulkTransfer(ctx.in_pipe_id, result, length, _callback_recv, &transferred);
  trace("send (recv) 0x%08x\n", ret);
  if (ret < 0)
    return ret;
  // wait for eventflag
  trace("waiting ef (recv)\n");
  ksceKernelWaitEventFlag(transfer_ev, EVF_RECV, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);

  return transferred;
}

/*
 *  Driver
 */

int libmouse_probe(int device_id)
{
  SceUsbdDeviceDescriptor *device;
  SceUsbdInterfaceDescriptor *interface;
  trace("probing device: %x\n", device_id);
  device = (SceUsbdDeviceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, 0, SCE_USBD_DESCRIPTOR_DEVICE);
  uint8_t found = 0;
  if (device)
  {
    trace("vendor: %04x\n", device->idVendor);
    trace("product: %04x\n", device->idProduct);
    // bDeviceClass should be == 0, = defined at Interface level
    // in config bNumInterfaces should be 2: Audio Control Interface and Midi Streaming
    // search for Midi Streaming

    if ( device->bDeviceClass == 0) // defined at interface level
    {
      interface = (SceUsbdInterfaceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, device, SCE_USBD_DESCRIPTOR_INTERFACE);
      while (interface)
      {
        if (interface->bInterfaceClass == 0x1 && interface->bInterfaceSubclass == 0x3) // Audio, MidiStreaming
        {
            found = 1;
            break;
        }
        interface = (SceUsbdInterfaceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, interface, SCE_USBD_DESCRIPTOR_INTERFACE);
      }
    }

    if (found)
    {
        trace("found midi device!\n");
        return SCE_USBD_PROBE_SUCCEEDED;
    }
  }
  return SCE_USBD_PROBE_FAILED;
}

int libmouse_attach(int device_id)
{
  trace("attaching device: %x\n", device_id);

  SceUsbdDeviceDescriptor *device;
  SceUsbdInterfaceDescriptor *interface;
  device = (SceUsbdDeviceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, 0, SCE_USBD_DESCRIPTOR_DEVICE);
  // if proper device

  uint8_t found = 0;
  if (device)
  {
    trace("vendor: %04x\n", device->idVendor);
    trace("product: %04x\n", device->idProduct);
    // bDeviceClass should be == 0, = defined at Interface level
    // in config bNumInterfaces should be 2: Audio Control Interface and Midi Streaming
    // search for Midi Streaming

    if ( device->bDeviceClass == 0) // defined at interface level
    {
      interface = (SceUsbdInterfaceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, device, SCE_USBD_DESCRIPTOR_INTERFACE);
      while (interface)
      {
        if (interface->bInterfaceClass == 0x1 && interface->bInterfaceSubclass == 0x3) // Audio, MidiStreaming
        {
            found = 1;
            break;
        }
        interface = (SceUsbdInterfaceDescriptor *)ksceUsbdScanStaticDescriptor(device_id, interface, SCE_USBD_DESCRIPTOR_INTERFACE);
      }
    }
  }

  if (found)
  {
    SceUsbdConfigurationDescriptor *cdesc;
    if ((cdesc = (SceUsbdConfigurationDescriptor *)ksceUsbdScanStaticDescriptor(device_id, device, SCE_USBD_DESCRIPTOR_CONFIGURATION)) == NULL)
      return SCE_USBD_ATTACH_FAILED;

    SceUsbdEndpointDescriptor *endpoint;
    trace("scanning endpoints\n");
    endpoint
        = (SceUsbdEndpointDescriptor *)ksceUsbdScanStaticDescriptor(device_id, interface, SCE_USBD_DESCRIPTOR_ENDPOINT);

    while (endpoint)
    {
      trace("got EP: %02x\n", endpoint->bEndpointAddress);
      if (endpoint->bEndpointAddress == USB_ENDPOINT_IN)
      {
        trace("opening in pipe\n");
        ctx.in_pipe_id = ksceUsbdOpenPipe(device_id, endpoint);
        trace("= 0x%08x\n", ctx.in_pipe_id);
      }
      if (endpoint->bEndpointAddress == USB_ENDPOINT_OUT)
      {
        trace("opening out pipe\n");
        ctx.out_pipe_id = ksceUsbdOpenPipe(device_id, endpoint);
        trace("= 0x%08x\n", ctx.out_pipe_id);
      }
      endpoint = (SceUsbdEndpointDescriptor *)ksceUsbdScanStaticDescriptor(device_id, endpoint, SCE_USBD_DESCRIPTOR_ENDPOINT);
    }

    ctx.control_pipe_id = ksceUsbdOpenPipe(device_id, NULL);
    // set default config
    int r = ksceUsbdSetConfiguration(ctx.control_pipe_id, cdesc->bConfigurationValue, _callback_control, NULL);
#ifdef NDEBUG
    (void)r;
#endif
    trace("ksceUsbdSetConfiguration = 0x%08x\n", r);
    trace("waiting ef (cfg)\n");
    ksceKernelWaitEventFlag(transfer_ev, EVF_CTRL, SCE_EVENT_WAITCLEAR_PAT | SCE_EVENT_WAITAND, NULL, 0);

    if ((ctx.in_pipe_id > 0 || ctx.out_pipe_id) && ctx.control_pipe_id)
    {
      if (ctx.in_pipe_id > 0)
          in_plugged = 1;
      if (ctx.out_pipe_id > 0)
          out_plugged = 1;
      return SCE_USBD_ATTACH_SUCCEEDED;
    }
  }
  return SCE_USBD_ATTACH_FAILED;
}

int libmouse_detach(int device_id)
{
  ctx.in_pipe_id  = 0;
  ctx.out_pipe_id = 0;
  in_plugged         = 0;
  ksceKernelSetEventFlag(transfer_ev, EVF_RECV);
  ksceKernelSetEventFlag(transfer_ev, EVF_SEND);
  return -1;
}

/*
 *  PUBLIC
 */

int libmouse_usb_start()
{
  uint32_t state;
  ENTER_SYSCALL(state);

  trace("starting libmouse\n");
  if (started)
  {
    _error_return(-1, "Already started");
  }

  // reset ctx
  _init_ctx();

  started = 1;
  int ret = ksceUsbServMacSelect(2, 0);
#ifdef NDEBUG
  (void)ret;
#endif
  trace("MAC select = 0x%08x\n", ret);
  ret = ksceUsbdRegisterDriver(&libmouseDriver);
  trace("ksceUsbdRegisterDriver = 0x%08x\n", ret);
  EXIT_SYSCALL(state);
  return 1;
}

int libmouse_usb_stop()
{
  uint32_t state;
  ENTER_SYSCALL(state);
  if (!started)
  {
    _error_return(-1, "Not started");
  }

  started = 0;
  in_plugged = 0;
  if (ctx.in_pipe_id)
    ksceUsbdClosePipe(ctx.in_pipe_id);
  if (ctx.out_pipe_id)
    ksceUsbdClosePipe(ctx.out_pipe_id);
  if (ctx.control_pipe_id)
    ksceUsbdClosePipe(ctx.control_pipe_id);
  ksceUsbdUnregisterDriver(&libmouseDriver);
  ksceUsbServMacSelect(2, 1);

  ksceKernelSetEventFlag(transfer_ev, EVF_RECV);
  ksceKernelSetEventFlag(transfer_ev, EVF_SEND);

  EXIT_SYSCALL(state);

  return 1;
  // TODO: restore udcd?
}

int libmouse_usb_in_attached()
{
  return (started && in_plugged);
}

int libmouse_usb_out_attached()
{
  return (started && out_plugged);
}

int libmouse_usb_read(uint8_t *buf, int size)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !in_plugged)
    _error_return(-3, "USB device unavailable");

  if (size > 64)
  {
    _error_return(-1, "bad packet size");
    return -1;
  }

  int ret = _recv(ctx.readbuffer, size);
  if (ret < 0)
  {
    trace("recv failed: 0x%08x\n", ret);
    EXIT_SYSCALL(state);
    return ret;
  }

  ksceKernelMemcpyKernelToUser(buf, ctx.readbuffer, ret);

  EXIT_SYSCALL(state);
  return ret;
}

int libmouse_usb_write(uint8_t *buf, int size)
{
  uint32_t state;
  ENTER_SYSCALL(state);

  if (!started || !out_plugged)
    _error_return(-3, "USB device unavailable");

  if (size > 64)
  {
    _error_return(-1, "bad packet size");
    return -1;
  }

  ksceKernelMemcpyUserToKernel(ctx.writebuffer, buf, size);

  int ret = _send(ctx.readbuffer, size);
  if (ret < 0)
  {
    trace("send failed: 0x%08x\n", ret);
    EXIT_SYSCALL(state);
    return ret;
  }

  EXIT_SYSCALL(state);
  return ret;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
  trace("libmouse starting\n");
  ksceKernelRegisterSysEventHandler("zlibmouse_sysevent", libmouse_sysevent_handler, NULL);
  transfer_ev = ksceKernelCreateEventFlag("libmouse_transfer", 0, 0, NULL);
  trace("ef: 0x%08x\n", transfer_ev);

//  libmouse_start_in();

  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
  libmouse_usb_stop();
  return SCE_KERNEL_STOP_SUCCESS;
}
