cmake_minimum_required(VERSION 3.20)
project(sh3pc C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Who the port is by: the launcher's credits screen, the menu title and the NACP.
set(PORT_AUTHOR "phoenixd123" CACHE STRING "Name shown in the launcher's credits")
set(PORT_NAME "SH3-PC" CACHE STRING "Title shown in the launcher")
# The version the updater compares GitHub's newest release with.
set(PORT_VERSION "1.11.2" CACHE STRING "Version the launcher reports and updates from")

file(GLOB GEN CONFIGURE_DEPENDS src/recomp/gen/*.c)
file(GLOB RT CONFIGURE_DEPENDS src/runtime/*.c src/win32/*.c src/d3d8/*.c src/game/*.c src/mods/*.c)
# miniz: the built-in zip reader (imports on the Deck, and the updater's unpacking).
file(GLOB MINIZ CONFIGURE_DEPENDS third_party/miniz/*.c)

add_executable(sh3 ${GEN} ${RT} ${MINIZ})
target_include_directories(sh3 PRIVATE third_party/miniz)
target_include_directories(sh3 PRIVATE src/recomp/gen)
# Lifted code relies on wrapping arithmetic and type punning through guest memory.
target_compile_options(sh3 PRIVATE -fwrapv -fno-strict-aliasing)
set_source_files_properties(${GEN} PROPERTIES COMPILE_OPTIONS "-w")
# The launcher's images are built into the binary (src/d3d8/launcher.c).
file(SHA1 ${CMAKE_SOURCE_DIR}/assets/splash.bmp SPLASH_SHA1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/splash.bmp)
set_source_files_properties(src/d3d8/launcher.c PROPERTIES COMPILE_DEFINITIONS
                            "SRC_DIR=\"${CMAKE_SOURCE_DIR}\";SPLASH_SHA1=\"${SPLASH_SHA1}\";PORT_AUTHOR=\"${PORT_AUTHOR}\";PORT_NAME=\"${PORT_NAME}\";PORT_VERSION=\"${PORT_VERSION}\""
                            OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/splash.bmp;${CMAKE_SOURCE_DIR}/assets/logo.bmp;${CMAKE_SOURCE_DIR}/assets/font.bmp")

if(NINTENDO_SWITCH)
  # Built with the devkitPro toolchain (tools/build-switch.sh); OpenGL comes through EGL and mesa.
  target_include_directories(sh3 PRIVATE ${DEVKITPRO}/portlibs/switch/include)
  target_link_libraries(sh3 PRIVATE SDL2 EGL glapi drm_nouveau stdc++ pthread m)
  nx_generate_nacp(sh3.nacp NAME "Silent Hill 3" AUTHOR "${PORT_AUTHOR}" VERSION "${PORT_VERSION}")
  nx_create_nro(sh3 OUTPUT sh3-pc.nro NACP sh3.nacp NOICON)
elseif(WIN32)
  # Windows: mingw-w64 (MSYS2) or clang, with SDL2's mingw development package. The executable keeps main() and
  # its console for sh3.log, so SDL's WinMain wrapper (SDL2main) is not linked and SDL_MAIN_HANDLED is defined
  # (see rt_window_open). The import libraries are the ones SDL2 itself needs.
  find_package(SDL2 REQUIRED)
  target_compile_definitions(sh3 PRIVATE SDL_MAIN_HANDLED _CRT_SECURE_NO_WARNINGS)
  # rt.h defines WINAPI as its bridge macro, and windows.h defines it as __stdcall: the last one wins, on purpose.
  target_compile_options(sh3 PRIVATE -Wno-macro-redefined)
  target_link_libraries(sh3 PRIVATE SDL2::SDL2 m user32 gdi32 winmm imm32 ole32 oleaut32 version uuid advapi32
                        setupapi shell32 comdlg32 dinput8 winhttp)
else()
  # GL is loaded at run time through SDL (src/d3d8/gl.c), so nothing links libGL.
  find_package(SDL2 REQUIRED)
  target_link_libraries(sh3 PRIVATE SDL2::SDL2 m pthread)
endif()
