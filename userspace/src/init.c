#include "string.h"
#include "stdio.h"
#include <usyscalls.h>
#include <stdint.h>

int main() {
    int i = 737;
    int j = 2;
    uint64_t start = time();
    uint64_t last = start;
#define WRITE(STR) write(1, STR, strlen(STR))
    WRITE("Okay\n"); // prints
    printf("Pre loop /init\n"); // prints

    for (; i < 739120210; i++) {
        j *= 2;
        if ((i % 10000000) == 0) {
            uint64_t now = time();
            int nowi = now / 1000;
            printf("now (%i)\n", nowi); // crashes in stdio
        }
    }

    printf("Post loop /init\n");
    return 0;
}
