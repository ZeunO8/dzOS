cmake_minimum_required(VERSION 3.22)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-v")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

project(dzOS LANGUAGES C CXX ASM)

set(DZOS_KERNEL_NAME kernel.elf)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(DZOS_KERNEL_FINAL_TARGET kernel_link)
else()
    set(DZOS_KERNEL_FINAL_TARGET ${DZOS_KERNEL_NAME})
endif()

include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/cmake/flanterm.cmake)

add_compile_options(-ffreestanding -fno-pic -fno-builtin -nostdlib -fno-stack-protector)

function(set_optimizations TGT)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        target_compile_options(${TGT} PRIVATE -O0 -g)
    elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        target_compile_options(${TGT} PRIVATE -O3)
    elseif("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
        target_compile_options(${TGT} PRIVATE -O2 -g)
    endif()
endfunction()

include(kernel/kernel_init.cmake)
include(libc/libc.cmake)
include(userspace/userspace.cmake)
include(kernel/kernel.cmake)
include(iso/iso.cmake)