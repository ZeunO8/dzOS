cmake_minimum_required(VERSION 3.20)

set(USRSPC ${CMAKE_CURRENT_LIST_DIR})
set(INC ${USRSPC}/include)
set(SRC ${USRSPC}/src)
set(DZOS_XXD_DIR ${CMAKE_SOURCE_DIR}/xxd)
file(MAKE_DIRECTORY ${DZOS_XXD_DIR})

if(NOT DEFINED USERSPACE_LINKER_SCRIPT)
    set(USERSPACE_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/link.ld)
endif()

if(NOT DEFINED USERSPACE_EXTRA_OBJS)
    set(USERSPACE_EXTRA_OBJS "")
endif()

find_program(LD_LLD_EXE NAMES ld.lld.exe ld.lld HINTS "C:/Program Files/LLVM/bin" "${LLVM_TOOLS_BINARY_DIR}")
if(NOT LD_LLD_EXE)
    message(FATAL_ERROR "ld.lld not found — ensure LLVM tools are installed and on PATH.")
endif()

macro(add_userspace_prog NAME)
    set(options "")
    set(oneValueArgs "FS_PATH")
    set(multiValueArgs "SOURCES")
    cmake_parse_arguments(SPC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SPC_SOURCES)
        message(FATAL_ERROR "add_userspace_prog(${NAME}): no SOURCES provided")
    endif()

    if(DEFINED SPC_FS_PATH)
        set(_FS_PATH ${SPC_FS_PATH})
    else()
        set(_FS_PATH /${NAME})
    endif()

    set(_BIN ${CMAKE_BINARY_DIR}/${NAME}.bin)
    set(_XXD_OUT ${DZOS_XXD_DIR}/${NAME}.c)

    add_custom_command(
        OUTPUT ${_BIN}
        COMMAND ${CMAKE_C_COMPILER} --target=x86_64-unknown-elf -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -O2 -g -c ${SPC_SOURCES} -o ${NAME}.o
        COMMAND ${LD_LLD_EXE} -flavor gnu -T ${USERSPACE_LINKER_SCRIPT} -o ${_BIN} ${NAME}.o ${USERSPACE_EXTRA_OBJS} --static -nostdlib
        DEPENDS ${SPC_SOURCES} ${USERSPACE_LINKER_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "[userspace] Building ELF binary ${NAME}.bin using ld.lld.exe (Windows-safe)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${_XXD_OUT}
        COMMAND ${CMAKE_COMMAND} -E echo "Generating xxd .c for ${NAME}"
        COMMAND xxd -n userspace_prog_${NAME} -i ${_BIN} > ${_XXD_OUT}
        COMMAND ${CMAKE_COMMAND} -DOUT=${_XXD_OUT} -DNAME=${NAME} -DFS_PATH=${_FS_PATH} -P ${USRSPC}/append_file.cmake
        DEPENDS ${_BIN} ${USRSPC}/append_file.cmake
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "[userspace] Create xxd .c file and append fs_path (${_FS_PATH})"
        VERBATIM
    )

    add_custom_target(${NAME}_bin ALL DEPENDS ${_BIN})
    add_custom_target(${NAME}_xxd ALL DEPENDS ${_XXD_OUT})

    list(APPEND USERSPACE_PROGRAM_BUILD_PATHS ${_BIN})
    list(APPEND USERSPACE_PROGRAM_FS_PATHS ${_FS_PATH})
    list(APPEND USERSPACE_DEPENDENCIES ${NAME}_xxd)

    set(USERSPACE_PROGRAM_BUILD_PATHS ${USERSPACE_PROGRAM_BUILD_PATHS})
    set(USERSPACE_PROGRAM_FS_PATHS ${USERSPACE_PROGRAM_FS_PATHS})
    set(USERSPACE_DEPENDENCIES ${USERSPACE_DEPENDENCIES})
endmacro()

add_userspace_prog(init SOURCES ${SRC}/init.c FS_PATH /init)

unset(SRC)
unset(INC)
set(USERSPACE_DIR ${USRSPC})
unset(USRSPC)