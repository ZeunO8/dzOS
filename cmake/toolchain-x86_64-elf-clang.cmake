set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CLANG_TARGET "x86_64-unknown-elf")

# Use clang/clang++ cross-targeting the ELF target and lld as linker
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

set(CMAKE_C_FLAGS_INIT "--target=${CLANG_TARGET} -mcmodel=kernel -mno-red-zone -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -O2 -g")
set(CMAKE_CXX_FLAGS_INIT "--target=${CLANG_TARGET} -mcmodel=kernel -mno-red-zone -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -O2 -g -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "--target=${CLANG_TARGET}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-v")
# Tell CMake not to try to run built executables
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)