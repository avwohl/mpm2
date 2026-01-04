cmake_minimum_required(VERSION 3.16)
project(mpm2_emu VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig)

# Find qkz80 Z80 emulator library
# Priority: pkg-config -> sister directory (cpmemu) -> /usr/local
set(QKZ80_FOUND FALSE)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(QKZ80 qkz80)
    if(QKZ80_FOUND)
        message(STATUS "Found qkz80 via pkg-config")
    endif()
endif()

if(NOT QKZ80_FOUND)
    # Check sister directory
    set(SISTER_QKZ80 "${CMAKE_SOURCE_DIR}/../cpmemu/src")
    if(EXISTS "${SISTER_QKZ80}/qkz80.h" AND EXISTS "${SISTER_QKZ80}/libqkz80.a")
        set(QKZ80_FOUND TRUE)
        set(QKZ80_INCLUDE_DIRS "${SISTER_QKZ80}")
        set(QKZ80_LIBRARY_DIRS "${SISTER_QKZ80}")
        set(QKZ80_LIBRARIES "qkz80")
        message(STATUS "Found qkz80 in sister directory: ${SISTER_QKZ80}")
    endif()
endif()

if(NOT QKZ80_FOUND)
    # Check /usr/local
    find_path(QKZ80_INC qkz80.h HINTS /usr/local/include/qkz80 /usr/local/include)
    find_library(QKZ80_LIB qkz80 HINTS /usr/local/lib)
    if(QKZ80_INC AND QKZ80_LIB)
        set(QKZ80_FOUND TRUE)
        set(QKZ80_INCLUDE_DIRS "${QKZ80_INC}")
        set(QKZ80_LIBRARIES "${QKZ80_LIB}")
        message(STATUS "Found qkz80 in /usr/local")
    endif()
endif()

if(NOT QKZ80_FOUND)
    message(FATAL_ERROR "qkz80 library not found. Options:\n"
        "  1. Clone cpmemu alongside this repo and run 'make' in cpmemu/src\n"
        "  2. Install qkz80 system-wide with 'make install' in cpmemu/src\n"
        "  3. Set QKZ80_ROOT to the qkz80 installation directory")
endif()

# SSH library - libssh
find_library(LIBSSH_LIB ssh HINTS /opt/homebrew/lib /usr/local/lib)
find_path(LIBSSH_INCLUDE libssh/libssh.h HINTS /opt/homebrew/include /usr/local/include)

if(LIBSSH_LIB AND LIBSSH_INCLUDE)
    set(HAVE_LIBSSH TRUE)
    message(STATUS "Found libssh: ${LIBSSH_LIB}")
else()
    message(WARNING "libssh not found - building without SSH support")
    message(STATUS "  macOS: brew install libssh")
    message(STATUS "  Linux: sudo apt install libssh-dev")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/console.cpp
    src/z80_runner.cpp
    src/mpm_cpu.cpp
    src/xios.cpp
    src/banked_mem.cpp
    src/disk.cpp
)

if(HAVE_LIBSSH)
    list(APPEND SOURCES src/ssh_session_libssh.cpp)
endif()

# Main executable
add_executable(mpm2_emu ${SOURCES})

target_include_directories(mpm2_emu PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${QKZ80_INCLUDE_DIRS}
)

if(QKZ80_LIBRARY_DIRS)
    target_link_directories(mpm2_emu PRIVATE ${QKZ80_LIBRARY_DIRS})
endif()

target_link_libraries(mpm2_emu PRIVATE
    Threads::Threads
    ${QKZ80_LIBRARIES}
)

if(HAVE_LIBSSH)
    target_compile_definitions(mpm2_emu PRIVATE HAVE_LIBSSH)
    target_include_directories(mpm2_emu PRIVATE ${LIBSSH_INCLUDE})
    target_link_libraries(mpm2_emu PRIVATE ${LIBSSH_LIB})
endif()

# Compiler warnings
target_compile_options(mpm2_emu PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>
)

# Boot image creation tool
add_executable(mkboot tools/mkboot.cpp)
target_compile_options(mkboot PRIVATE
    -Wall -Wextra -Wpedantic
)

# Install targets
install(TARGETS mpm2_emu mkboot RUNTIME DESTINATION bin)
