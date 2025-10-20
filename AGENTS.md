# dzOS Agent Guide

## Scope
These instructions apply to the entire repository. If you add additional guidance for a
subdirectory, place another `AGENTS.md` in that folder; its rules should extend or override
what is written here.

## Project overview
- dzOS is a hobby operating system that boots via the Limine bootloader and provides both
  kernelspace and userspace components. The kernel lives under `kernel/src/`, while simple
  userspace programs live under `userspace/src/`.
- Build tooling is CMake + Ninja, and the preferred compiler toolchain is Clang/LLVM (as
  noted in the README). The default workflow is:
  1. `cmake -GNinja -B build -DCMAKE_BUILD_TYPE=Debug`
  2. `cmake --build build`
  3. Optional run: `cmake --build build --target qemu`
  4. Optional debug: `cmake --build build --target qemu-debug`
- The kernel depends on a number of custom runtime helpers located in `kernel/src/common`
  (e.g., `ktprintf`, `memset_*` helpers, spinlocks, etc.)—reuse those utilities rather than
  reimplementing standard library functionality.

## Coding style expectations
- **Match the surrounding style.** The codebase is primarily C (with a few assembly files).
  Follow the conventions used in the file you are editing. In general:
  - Indent using spaces (existing C sources typically use two-space indentation inside
    blocks; keep the file consistent).
  - Place function braces on the next line (Allman style) unless the file consistently
    uses same-line braces—consistency beats dogma.
  - Prefer explicit integer types from `<stdint.h>`/`<stddef.h>` for fixed-width values.
  - Avoid C++ features in kernel code unless you first confirm the kernel build system
    already supports them.
- Keep headers lean: prefer forward declarations where possible and avoid introducing
  cyclic dependencies between headers.
- When touching kernel code, remember it runs without the standard C library. Use the
  helpers in `common/`, `mem/`, `drivers/`, etc., instead of calling unavailable libc
  routines.
- For userspace programs under `userspace/src/`, use the syscall stubs declared in
  `libc/include/usyscalls.h` (e.g., `write`, `read`) rather than inventing new syscall
  numbers.

## Testing and validation
- Always ensure the kernel and userspace still compile: run `cmake --build build` before
  submitting changes. If you introduce new targets or configuration options, document the
  required commands in your change description.
- If your change affects boot flow, drivers, or system initialization, try to boot the
  kernel in QEMU (`cmake --build build --target qemu`) to verify it still boots to the
  framebuffer shell output. Capture relevant serial/log output when diagnosing failures.

## Documentation & comments
- Update or extend the README or in-tree documentation if you change build steps,
  dependencies, or introduce notable features.
- Favor descriptive comments over large ASCII art banners. Document why a change is
  necessary, especially in low-level code (interrupt handlers, paging, etc.).
- Keep the AGENT instructions current—if you make structural changes, update this file or
  add scoped instructions alongside the modified code.

## Pull request hygiene
- Group related changes together and avoid mixing refactors with functional changes unless
  the refactor is required.
- When adding new files, place them in the appropriate subsystem (e.g., `kernel/src/mem`
  for memory management, `kernel/src/device` for hardware-facing code, `userspace/src`
  for simple init programs).
- Preserve existing copyright/license headers when copying or modifying files.