#include "string.h"
#include "stdio.h"
#include <usyscalls.h>
#include <stdint.h>

int main(int argc, char** argv) {
    int i = 737;
    int j = 2;
    uint64_t start = time();
    uint64_t last = start;
#define WRITE(STR) write(1, STR, strlen(STR))
    WRITE("Okay\n"); // prints
    printf("Pre loop /init\n"); // prints

    for (; i < 239120210; i++) {
        j *= 2;
        if ((i % 80000000) == 0) {
            uint64_t now = time();
            printf("now (%llu)\n", now); // crashes in stdio
        }
    }

    printf("Post loop /init\n");
    return 0;
}
