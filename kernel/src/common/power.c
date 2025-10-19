#include "common/power.h"

#include "common/printf.h"
#include "cpu/asm.h"

static void attempt_acpi_shutdown(void) {
  // QEMU/Bochs ACPI power-off
  outw(0x604, 0x2000);
  outw(0xB004, 0x2000);
  outw(0x4004, 0x3400);
  outw(0x600, 0x2000);
}

__attribute__((noreturn)) void system_shutdown(void) {
  cli();
  ktprintf("No runnable processes remain. Shutting down...\n");

  attempt_acpi_shutdown();

  // Fallback: halt the CPU if ACPI shutdown is not available
  halt();
}
