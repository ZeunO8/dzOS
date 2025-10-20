#include "string.h"
#include "stdio.h"
#include <usyscalls.h>
#include <stdint.h>
#define WRITE(STR) write(1, STR, strlen(STR))
#define WRITEC(C) write(1, &C, 1)
int main(int argc, char** argv) {
    int i = 737;
    int j = 2;
    double start = (double)time();
    double last = start;
    printf("/init start\n");
    int k = 0;
    for (; i < 239120210; i++) {
        j *= 2;
        if ((i % 80000000) == 0) {
            double now = (double)time();
            double start_ = (now - start) / 1000000.0; // seconds
            double diff = (now - last) / 1000000.0;     // seconds
            printf("now(%i):start(%lf s),diff(%lf s)\n", ++k, start_, diff);
            last = now;
        }
    }
    printf("/init finished\n");
    return 0;
}
