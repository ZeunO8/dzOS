#pragma once
#include "../include/syscall.h"

typedef long long ssize_t;

#define MAX_SYSCALLS 512
#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_EXIT  60
#define SYS_TEST 400

void init_syscall_table(void);

uint64_t syscall_c(uint64_t syscall_number, uint64_t a1, uint64_t a2,
                   uint64_t a3);

int write(int fd, const void* buf, size_t len);
int test(int i, int o);