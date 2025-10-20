#include <string.h>
#include <usyscalls.h>
int main()
{
#define PRINTF(STR) write(1, STR, strlen(STR))
    int i = 737;
    int j = 2;
    PRINTF("Pre loop /init\n");
    for (; i < 739120210; i++) {
        j *= 2;
    }
    PRINTF("Post loop /init\n");
    return 0;
}