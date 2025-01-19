#        libmouse
#        Copyright (C) 2025 Cat (Ivan Epifanov)
#
#        Permission is hereby granted, free of charge, to any person obtaining
#        a copy of this software and associated documentation files (the "Software"),
#        to deal in the Software without restriction, including without limitation
#        the rights to use, copy, modify, merge, publish, distribute, sublicense,
#        and/or sell copies of the Software, and to permit persons
#        to whom the Software is furnished to do so, subject to the following conditions:
#
#        The above copyright notice and this permission notice
#        shall be included in all copies or substantial portions of the Software.
#
#        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
#        INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#        FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#        IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
#        DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
#        ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
#        OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(libmouse)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -nostdlib")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

add_executable(libmouse
  main.c
)

target_link_libraries(libmouse
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  SceSysmemForKernel_stub
  SceThreadmgrForDriver_stub
  SceDebugForDriver_stub
  SceUsbdForDriver_stub
  SceUsbServForDriver_stub
  SceKernelSuspendForDriver_stub
)

vita_create_self(libmouse.skprx libmouse CONFIG exports.yml UNSAFE)

vita_create_stubs(stubs libmouse ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml KERNEL)

add_custom_target(libmouse_all
  ALL
  DEPENDS liblibmouse_stub.a
  DEPENDS liblibmouse_stub_weak.a
)

install(DIRECTORY ${CMAKE_BINARY_DIR}/stubs/
  DESTINATION lib
  FILES_MATCHING PATTERN "*.a"
)

install(FILES libmouse.h
  DESTINATION include
)
