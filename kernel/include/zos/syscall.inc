#ifdef __ASSEMBLER__
GEN_SYS read READ
GEN_SYS write WRITE
GEN_SYS open OPEN
GEN_SYS close CLOSE
GEN_SYS sbrk SBRK
GEN_SYS exec EXEC
GEN_SYS exit EXIT
GEN_SYS wait WAIT
GEN_SYS lseek LSEEK
GEN_SYS time TIME
GEN_SYS sleep SLEEP
GEN_SYS ioctl IOCTL
GEN_SYS rename RENAME
GEN_SYS unlink UNLINK
GEN_SYS mkdir MKDIR
GEN_SYS chdir CHDIR
GEN_SYS readdir READDIR
#elif defined(GEN_SYS_0U) && defined(GEN_SYS_1U) && defined(GEN_SYS_1UV) && defined(GEN_SYS_2U) && defined(GEN_SYS_3U) && defined(GEN_SYS_FN) && defined(GEN_SYS_RFN1)
GEN_SYS_3U(int, read, READ, int, void*, size_t);
GEN_SYS_3U(int, write, WRITE, int, const void*, size_t);
GEN_SYS_2U(int, open, OPEN, const char *, int);
GEN_SYS_1U(int, close, CLOSE, int);
GEN_SYS_2U(uint64_t, exec, EXEC, const char *, const char **);
GEN_SYS_1UV(void, exit, EXIT, int);
GEN_SYS_3U(int, lseek, LSEEK, int, int64_t, int);
GEN_SYS_1UV(void, sleep, SLEEP, uint64_t);
GEN_SYS_3U(int, ioctl, IOCTL, int, int, void*);
GEN_SYS_2U(int, rename, RENAME, const char *, const char *);
GEN_SYS_1U(int, unlink, UNLINK, const char *);
GEN_SYS_1U(int, mkdir, MKDIR, const char *);
GEN_SYS_1U(int, chdir, CHDIR, const char *);
GEN_SYS_3U(int, readdir, READDIR, int, void*, size_t);
GEN_SYS_0U(uint64_t, time, TIME);
GEN_SYS_1U(void*, sbrk, SBRK, int64_t);
GEN_SYS_1U(int, wait, WAIT, uint64_t);
#endif