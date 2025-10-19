#include <string.h>
#include <usyscalls.h>
int main()
{
#define PRINT_THIS "Good/one from Userspace...\n"
    write(1, PRINT_THIS, strlen(PRINT_THIS));
    int i = 737;
    int j = 2;
    for (; i < 739; i++) {
        j *= 2;
    }
    return 0;
}