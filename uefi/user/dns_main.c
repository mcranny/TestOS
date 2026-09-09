#include "ulib.h"

int main(int argc, char **argv)
{
    unsigned int ip = 0;

    if (argc < 2) {
        uwrite("usage: dns <hostname>\n");
        return 1;
    }
    if (uresolve(argv[1], &ip) != 0) {
        uwrite("dns: lookup failed\n");
        return 1;
    }
    uwrite(argv[1]);
    uwrite(" -> ");
    uwrite_ip(ip);
    uwrite("\n");
    return 0;
}
