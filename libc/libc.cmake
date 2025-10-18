file(GLOB_RECURSE LIBC_C_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
file(GLOB_RECURSE LIBC_ASM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.S)
set(LIBC_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)

add_library(libc_objs_c OBJECT ${LIBC_C_SOURCES})
target_compile_options(libc_objs_c PRIVATE -O0 -g --target=x86_64-unknown-elf -mcmodel=kernel -mno-red-zone -Wc23-extensions)
target_include_directories(libc_objs_c PRIVATE ${DZOS_KERNEL_DIR} ${DZOS_KERNEL_SRC_DIR} ${LIBC_INCLUDE_DIR})

set_optimizations(libc_objs_c)

add_library(libc_objs_S OBJECT ${LIBC_ASM_SOURCES})
target_compile_options(libc_objs_S PRIVATE -ffreestanding -x assembler-with-cpp --target=x86_64-unknown-elf -mcmodel=kernel -mno-red-zone)
target_include_directories(libc_objs_S PRIVATE ${DZOS_KERNEL_DIR} ${DZOS_KERNEL_SRC_DIR} ${LIBC_INCLUDE_DIR})

add_library(c STATIC $<TARGET_OBJECTS:libc_objs_S> $<TARGET_OBJECTS:libc_objs_c>)