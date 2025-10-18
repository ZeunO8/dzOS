# Userspace build (Option 2):
# - Do NOT create add_executable() (prevents MSVC/lld-link from running)
# - Use clang in ELF cross mode to compile+link directly with ld.lld (ELF)
# - Then generate xxd .c that embeds the userspace binary for your kernel
# - Integrates with your kernel build via USERSPACE_DEPENDENCIES like before

cmake_minimum_required(VERSION 3.20)

set(USRSPC ${CMAKE_CURRENT_LIST_DIR})
set(INC ${USRSPC}/include)
set(SRC ${USRSPC}/src)

# Where xxd output .c files go
set(DZOS_XXD_DIR ${CMAKE_SOURCE_DIR}/xxd)
file(MAKE_DIRECTORY ${DZOS_XXD_DIR})

# Optional: linker script used for userspace programs
# You can override this from the parent CMakeLists using:
#   set(USERSPACE_LINKER_SCRIPT "/abs/path/to/link.ld")
if(NOT DEFINED USERSPACE_LINKER_SCRIPT)
    set(USERSPACE_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/link.ld)
endif()

# Optional: extra objects/archives to link into userspace programs (ELF only!)
# Example: set(USERSPACE_EXTRA_OBJS "${CMAKE_BINARY_DIR}/libc_userspace.a")
if(NOT DEFINED USERSPACE_EXTRA_OBJS)
    set(USERSPACE_EXTRA_OBJS "")
endif()

# Helper: join list with spaces for response files / commands
function(join_with_spaces out_var)
    set(result "")
    foreach(item IN LISTS ARGN)
        if(result STREQUAL "")
            set(result "${item}")
        else()
            set(result "${result} ${item}")
        endif()
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Macro to declare a userspace program without creating a native exe
# Usage:
#   add_userspace_prog(init SOURCES ${SRC}/init.c FS_PATH /init)
macro(add_userspace_prog NAME)
    set(options "")
    set(oneValueArgs "FS_PATH")
    set(multiValueArgs "SOURCES")
    cmake_parse_arguments(SPC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SPC_SOURCES)
        message(FATAL_ERROR "add_userspace_prog(${NAME}): no SOURCES provided")
    endif()

    # Files & paths
    if(DEFINED SPC_FS_PATH)
        set(_FS_PATH ${SPC_FS_PATH})
    else()
        set(_FS_PATH /${NAME})
    endif()

    set(_BIN        ${CMAKE_BINARY_DIR}/${NAME}.bin)
    set(_XXD_OUT    ${DZOS_XXD_DIR}/${NAME}.c)

    # Build command: clang in ELF cross mode; compile+link in one step (avoids native lld-link)
    # NOTE: ${SPC_SOURCES} may contain multiple files; clang handles that fine here
    add_custom_command(
        OUTPUT ${_BIN}
        COMMAND ${CMAKE_C_COMPILER}
                --target=x86_64-unknown-elf
                -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone
                -O2 -g -fuse-ld=lld
                -nostdlib -nostartfiles -static
                -Wl,-T,${USERSPACE_LINKER_SCRIPT}
                -o ${_BIN}
                ${SPC_SOURCES}
                ${USERSPACE_EXTRA_OBJS}
        DEPENDS ${SPC_SOURCES} ${USERSPACE_LINKER_SCRIPT} ${USERSPACE_EXTRA_OBJS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "[userspace] Building ELF binary ${NAME}.bin with clang+ld.lld (cross)"
        VERBATIM
    )

    # xxd embed step
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

    # Phony targets so Ninja has something to build
    add_custom_target(${NAME}_bin  ALL DEPENDS ${_BIN})
    add_custom_target(${NAME}_xxd  ALL DEPENDS ${_XXD_OUT})

    # Export variables so kernel side can depend on xxd generation (like your kernel.cmake does)
    list(APPEND USERSPACE_PROGRAM_BUILD_PATHS ${_BIN})
    list(APPEND USERSPACE_PROGRAM_FS_PATHS   ${_FS_PATH})
    list(APPEND USERSPACE_DEPENDENCIES       ${NAME}_xxd)
    set(USERSPACE_PROGRAM_BUILD_PATHS ${USERSPACE_PROGRAM_BUILD_PATHS} PARENT_SCOPE)
    set(USERSPACE_PROGRAM_FS_PATHS   ${USERSPACE_PROGRAM_FS_PATHS}   PARENT_SCOPE)
    set(USERSPACE_DEPENDENCIES       ${USERSPACE_DEPENDENCIES}       PARENT_SCOPE)

    # Includes for any code that builds userspace sources (not used by custom commands, but useful elsewhere)
    # You may add interface libs if needed.
endmacro()

# === Declare userspace programs here ===
add_userspace_prog(init SOURCES ${SRC}/init.c FS_PATH /init)

# Cleanup locals
unset(SRC)
unset(INC)
set(USERSPACE_DIR ${USRSPC})
unset(USRSPC)
