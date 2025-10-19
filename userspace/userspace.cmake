set(USRSPC ${CMAKE_CURRENT_LIST_DIR})
set(INC ${USRSPC}/include)
set(SRC ${USRSPC}/src/)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/xxd)

macro(add_userspace_prog NAME)
    set(options "")
    set(oneValueArgs "FS_PATH")
    set(multiValueArgs "SOURCES")
    cmake_parse_arguments(SPC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${NAME} ${SPC_SOURCES})

    target_compile_options(${NAME} PRIVATE
        --target=x86_64-unknown-elf
        -fno-stack-protector -fno-pic
        -fno-pie -mno-red-zone -nostdlib
    )

    set_optimizations(${NAME})

    target_link_options(${NAME} PRIVATE
        -static -nostdlib
    )

    target_link_libraries(${NAME} PRIVATE c)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${DZOS_KERNEL_INC_DIR} ${LIBC_INCLUDE_DIR} ${INC})
    target_link_options(${NAME} PRIVATE
        -Wl,--whole-archive $<TARGET_FILE:c> -Wl,--no-whole-archive
    )

    if(DEFINED SPC_FS_PATH)
        set(FS_PATH ${SPC_FS_PATH})
    else()
        set(FS_PATH /${NAME})
    endif()

    # Output binary and generated C file
    set(BIN_PATH $<TARGET_FILE:${NAME}>)
    set(XXD_OUT ${CMAKE_SOURCE_DIR}/xxd/${NAME}.c)

    list(APPEND USERSPACE_PROGRAM_BUILD_PATHS ${BIN_PATH})
    list(APPEND USERSPACE_PROGRAM_FS_PATHS ${FS_PATH})
    list(APPEND USERSPACE_DEPENDENCIES ${NAME})

    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Generating xxd .c for ${NAME}"
        COMMAND xxd -n "userspace_prog_${NAME}" -i ${BIN_PATH} > ${XXD_OUT}
        COMMAND ${CMAKE_COMMAND} -DOUT=${XXD_OUT} -DNAME=${NAME} -DFS_PATH=${FS_PATH} -P ${USRSPC}/append_file.cmake
        COMMENT "Create xxd .c file and append fs_path"
    )

    unset(FS_PATH)
endmacro()

add_userspace_prog(init SOURCES ${SRC}/init.c)

unset(SRC)
unset(INC)
set(USERSPACE_DIR ${USRSPC})
unset(USRSPC)