# kernel.cmake

add_library(kernel_objs_c OBJECT ${KERNEL_C_SOURCES} ${FLANTERM_SOURCES})
target_compile_options(kernel_objs_c PRIVATE 
    --target=x86_64-unknown-elf 
    -ffreestanding 
    -fno-builtin
    -nostdlib
    -mcmodel=kernel 
    -mno-red-zone 
    -Wc23-extensions
)
set_optimizations(kernel_objs_c)
target_include_directories(kernel_objs_c PRIVATE ${DZOS_KERNEL_DIR} ${DZOS_KERNEL_SRC_DIR} ${DZOS_XXD_DIR} ${flanterm_SOURCE_DIR}/src)

add_library(kernel_objs_s OBJECT ${KERNEL_ASM_SOURCES})
target_compile_options(kernel_objs_s PRIVATE 
    -ffreestanding 
    -nostdlib
    -x assembler-with-cpp 
    --target=x86_64-unknown-elf 
    -mcmodel=kernel 
    -mno-red-zone
)
target_include_directories(kernel_objs_s PRIVATE ${DZOS_KERNEL_DIR} ${DZOS_KERNEL_SRC_DIR} ${DZOS_XXD_DIR})

foreach(USRSPC_NAME IN LISTS USERSPACE_DEPENDENCIES)
    add_dependencies(kernel_objs_c ${USRSPC_NAME})
    add_dependencies(kernel_objs_s ${USRSPC_NAME})
endforeach()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "WIN32 detected: using ld.lld.exe manually for ELF cross-link")

    add_custom_target(kernel_link ALL
        # Build a flat list of object paths using CMake commands only
        COMMAND ${CMAKE_COMMAND} -E echo "$<TARGET_OBJECTS:kernel_objs_s>" > ${CMAKE_BINARY_DIR}/objlist_raw.txt
        COMMAND ${CMAKE_COMMAND} -E echo "$<TARGET_OBJECTS:kernel_objs_c>" >> ${CMAKE_BINARY_DIR}/objlist_raw.txt
        COMMAND ${CMAKE_COMMAND} -E echo "" >> ${CMAKE_BINARY_DIR}/objlist_raw.txt

        # Inline PowerShell cleanup — no external scripts
        COMMAND powershell -Command "(Get-Content ${CMAKE_BINARY_DIR}/objlist_raw.txt) -join ' ' -replace ';', ' ' | Out-File -Encoding ASCII ${CMAKE_BINARY_DIR}/objlist_fixed.txt"

        # Link kernel ELF using GNU-style ld.lld
        COMMAND ld.lld.exe -flavor gnu -T "${LINKER_SCRIPT}" -static -nostdlib --allow-shlib-undefined @${CMAKE_BINARY_DIR}/objlist_fixed.txt --image-base=0xFFFFFFFF80000000 -o ${CMAKE_BINARY_DIR}/${DZOS_KERNEL_NAME}

        DEPENDS ${LINKER_SCRIPT} kernel_objs_c kernel_objs_s
        COMMENT "[kernel] Linking ${DZOS_KERNEL_NAME} with ld.lld.exe (GNU flavor, Windows-safe cross build)"
        VERBATIM
    )

else()
    add_executable(${DZOS_KERNEL_NAME} $<TARGET_OBJECTS:kernel_objs_c> $<TARGET_OBJECTS:kernel_objs_s>)
    set_target_properties(${DZOS_KERNEL_NAME} PROPERTIES LINK_FLAGS "-T ${LINKER_SCRIPT} -nostdlib -static")
endif()