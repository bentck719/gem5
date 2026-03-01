#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <x86intrin.h>

int main(int argc, char *argv[])
{
    char *addr = (char *)mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    _mm_clwb(addr);
    munmap(addr, 4096);
    return 0;
}