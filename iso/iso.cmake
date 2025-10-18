add_custom_command(TARGET ${DZOS_KERNEL_FINAL_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/isoroot
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/isoroot/boot
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${DZOS_KERNEL_NAME} ${CMAKE_BINARY_DIR}/isoroot/boot/${DZOS_KERNEL_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/dzOS.bmp ${CMAKE_BINARY_DIR}/isoroot/boot/dzOS.bmp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/limine/limine.conf ${CMAKE_BINARY_DIR}/isoroot/boot/limine/limine.conf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/limine/limine-bios-cd.bin ${CMAKE_BINARY_DIR}/isoroot/limine-bios-cd.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/limine/limine-uefi-cd.bin ${CMAKE_BINARY_DIR}/isoroot/limine-uefi-cd.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/limine/limine-bios.sys ${CMAKE_BINARY_DIR}/isoroot/limine-bios.sys
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/limine/BOOTX64.EFI ${CMAKE_BINARY_DIR}/isoroot/BOOTX64.EFI
    COMMENT "Copy files to ${CMAKE_BINARY_ROOT}/isoroot"
)

add_custom_target(os-iso ALL
    DEPENDS ${DZOS_KERNEL_FINAL_TARGET}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/os-image.iso
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} xorriso -as mkisofs 
        -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table 
        --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image 
        --protective-msdos-label isoroot -o os-image.iso
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR} limine bios-install os-image.iso || true
    COMMENT "Create hybrid ISO and install limine"
)

add_custom_target(qemu
    DEPENDS os-iso
    COMMAND qemu-system-x86_64 -cdrom ${CMAKE_BINARY_DIR}/os-image.iso -m 512M -k en_us -boot d -serial stdio -device nvme,drive=nvme0,serial=deadbeef -drive file=nvme.img,format=raw,if=none,id=nvme0
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    add_custom_target(qemu-debug
        DEPENDS os-iso
        COMMAND ${CMAKE_COMMAND} -E echo "Launching QEMU in debug mode..."
        COMMAND qemu-system-x86_64 -cdrom ${CMAKE_BINARY_DIR}/os-image.iso -k en_us -boot d -m 512M -s -S -no-reboot -no-shutdown -serial stdio -device nvme,drive=nvme0,serial=deadbeef -drive file=nvme.img,format=raw,if=none,id=nvme0
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
    )
endif()