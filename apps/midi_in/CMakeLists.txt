cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(mouse)
include("${VITASDK}/share/vita.cmake" REQUIRED)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

set(VITA_APP_NAME "MoUSE")
set(VITA_TITLEID  "MUSE00001")
set(VITA_VERSION  "01.00")


add_executable(${PROJECT_NAME}
  src/main.c
)

target_link_libraries(${PROJECT_NAME}
  SDL2::SDL2-static
  SDL2_image::SDL2_image-static
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  taihen_stub
  SceVshBridge_stub
  SceAppMgr_stub
  SceShellSvc_stub
  libmouse_stub_weak
)

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys sce_sys
  FILE data data
)
