# dzOS

**dzOS** `(DirectZ OS)` is my attempt at building an operating system kernel.

It uses the Limine bootloader.

### Features

 - [x] Instruction support
   - [x] RTC (Real-Time-Clock) - Fast ASM PIT Calibrate
   - [x] FPU (Floating Point Unit)
   - [x] SSE
 - [x] GDT (Global Descriptor Table)
 - [x] IDT (Interrupt Descriptor Table)
 - [x] IOAPIC/LAPIC support
 - [x] Interrupts
 - [x] Virtual Memory mapping (4KB Page Size)
 - [x] dynamic kernel memory allocation (`kmalloc`/`kfree`) that manages space efficiently
 - [x] ELF binary loader
 - [x] **Kernel**space + **User**space
 - [x] Device detection + Driver registration
   - [x] PS/2
   - [x] PCI
 - [x] Basic NVMe support
 - [x] simple `dzFS` filesystem
 - [x] syscall and interrupt support
 - [x] Basic serial IO support
 - [x] Uses the Limine provided framebuffer, and flanterm terminal, to print kernel init to the framebuffer

### Building and Running

To get started with dzOS, make sure you have the following tools in your PATH

 - clang/clang++
 - xxd
 - xorriso
 - qemu
 - ninja-build

Then to configure and build..

```bash
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=[Debug/Release]
cmake --build build
```

#### If you want to simply run, use

```bash
cmake --build build --target qemu
```

#### Or if you want to debug via GDB

**first run:**
```bash
cmake --build build --target qemu-debug
```

**then in another terminal run:**
```bash
gdb build/isoroot/boot/kernel.elf
(gdb) b kmain
(gdb) target remote localhost:1234
(gdb) continue
```

### Host support

As of latest master branch, only ***unix-like*** OS can build *dzOS*, so, if your host is ***Windows***, it is recommended to use ***WSL***.

### Resources used for development

 - [OSDev wiki](https://wiki.osdev.org/)
 - [CrowOS](https://github.com/HirbodBehnam/CrowOS/) - Great starting reference; **dzOS** has changed significantly since starting with code from **CrowOS**
 - [Limine Bootloader](https://github.com/limine-bootloader/)
 - [Intel Software Developers Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
 - Various other developers via Discord
 - AI tools such as ChatGPT/Claude/Gemini