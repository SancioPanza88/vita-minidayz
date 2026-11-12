cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{VITASDK} AND NOT MDZ_LINUX)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()

project(vita_minidayz C)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(VITA_APP_NAME "Mini DayZ")
set(VITA_TITLEID  "MDAYZ0001")
set(VITA_VERSION  "01.00")

set(QJS ${CMAKE_SOURCE_DIR}/third_party/quickjs)
add_library(quickjs STATIC ${QJS}/quickjs.c ${QJS}/libregexp.c ${QJS}/libunicode.c ${QJS}/dtoa.c)
target_include_directories(quickjs PUBLIC ${QJS})
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
target_compile_options(quickjs PRIVATE -O2 -w -fno-strict-aliasing)

set(SRC src/main.c src/natives.c src/gl.c src/audio.c src/input.c src/vorbis.c)

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  add_executable(${PROJECT_NAME} ${SRC} src/plat_vita.c)
  target_include_directories(${PROJECT_NAME} PRIVATE src third_party/stb)
  target_compile_options(${PROJECT_NAME} PRIVATE -O2 -Wall -Wno-unused-function -mtune=cortex-a9 -mfpu=neon)
  target_link_libraries(${PROJECT_NAME}
    quickjs
    vitaGL vitashark SceShaccCgExt mathneon taihen_stub
    SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
    SceCtrl_stub SceTouch_stub SceAudio_stub SceAppUtil_stub SceAppMgr_stub
    ScePower_stub SceCommonDialog_stub SceSysmodule_stub SceIofilemgr_stub
    SceLibKernel_stub SceKernelThreadMgr_stub
    stdc++ pthread m c)
  vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
  # Game data (./game, some names contain spaces) is added to the VPK afterwards with zip,
  # see tools/ci_build.sh / the workflow.
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE src/js/prelude.js res/prelude.js
    FILE src/js/glconst.js res/glconst.js
    FILE src/js/xml.js res/xml.js
    FILE assets/fonts/LiberationSans-Regular.ttf res/LiberationSans-Regular.ttf
    FILE ${CMAKE_SOURCE_DIR}/build_assets/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${CMAKE_SOURCE_DIR}/build_assets/sce_sys/pic0.png sce_sys/pic0.png
    FILE ${CMAKE_SOURCE_DIR}/build_assets/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE ${CMAKE_SOURCE_DIR}/build_assets/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE assets/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  )
else()
  # Linux test host (headless EGL + GLES2 from Mesa)
  find_library(EGL_LIB NAMES EGL libEGL.so.1 PATHS /usr/lib/x86_64-linux-gnu)
  find_library(GLES_LIB NAMES GLESv2 libGLESv2.so.2 PATHS /usr/lib/x86_64-linux-gnu)
  add_executable(minidayz ${SRC} src/plat_linux.c)
  target_include_directories(minidayz PRIVATE src third_party/stb third_party/khronos)
  target_compile_options(minidayz PRIVATE -O2 -g -Wall -Wno-unused-function)
  target_compile_definitions(minidayz PRIVATE MDZ_PROFILE)
  target_link_libraries(minidayz quickjs ${EGL_LIB} ${GLES_LIB} m pthread)
endif()
