# Project M.o.U.S.E (Midi over Usb Support Effort)

Driver and sample programs for midi in on PSTV/PSVITA

## Driver
libmouse.skprx is a simple MidiStreaming device usb driver that provides methods for reading and writing raw data  

### Building
```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```

### Using
- Include libmouse.h
- Link with `liblibmouse_stub_weak.a` (note the liblib)

### Api
- `int libmouse_usb_start()` - starts usb in/out driver
- `int libmouse_usb_stop()` - stops usb in/out driver
- `int libmouse_usb_in_attached()` - returns 1 if there's midi-in device attached
- `int libmouse_usb_out_attached()` returns 1 if there's midi-out device attached
- `int libmouse_usb_read(uint8_t *buf, int size)` - tries to read usb data. blocking. max buf size is 64 bytes. returns number of bytes read
- `int libmouse_usb_write(uint8_t *buf, int size)` - tries to write usb data. blocking. max buf size is 64 bytes. returns number of bytes written

### TODO
- implement udcd device driver to turn vita into usb midi device

## Apps

### Midi in

Simple demo app that uses TinySoundFont to render data coming over usb

#### Building
Build and install driver first.
```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```
