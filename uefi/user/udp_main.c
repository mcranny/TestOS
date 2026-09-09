#include "ulib.h"

static unsigned ustrlen_local(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static unsigned short parse_port(const char *s)
{
    unsigned v = 0;
    if (!s || *s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10U + (unsigned)(*s - '0');
        if (v > 65535U) return 0;
        s++;
    }
    return (unsigned short)v;
}

static unsigned int parse_ip(const char *s)
{
    unsigned octets[4];
    unsigned oi = 0;
    unsigned val = 0;
    int saw = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10U + (unsigned)(*s - '0');
            if (val > 255U) return 0;
            saw = 1;
            s++;
        } else if (*s == '.' && saw && oi < 3U) {
            octets[oi++] = val;
            val = 0;
            saw = 0;
            s++;
        } else {
            return 0;
        }
    }
    if (!saw || oi != 3U) return 0;
    octets[3] = val;
    return (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
}

/* Minimal UDP utility: uses TCP sockets API is not available for UDP yet.
 * For v0.13 we print guidance to use shell `udp` for datagrams, and offer
 * a TCP-based echo client as a connectivity check alias.
 */
int main(int argc, char **argv)
{
    unsigned int ip;
    unsigned short port;
    int sock;
    int n;
    unsigned ticks;
    char buf[256];

    if (argc < 4) {
        uwrite("usage: udp <ip> <port> <msg>\n");
        uwrite("(userland UDP uses TCP echo path; prefer shell udp for datagrams)\n");
        return 1;
    }

    ip = parse_ip(argv[1]);
    if (!ip && uresolve(argv[1], &ip) != 0) {
        uwrite("udp: bad host\n");
        return 1;
    }
    port = parse_port(argv[2]);
    if (!port) {
        uwrite("udp: bad port\n");
        return 1;
    }

    /* Connect to TCP echo (12346) when port is echo-like; otherwise try connect anyway. */
    sock = usocket();
    if (sock < 0 || uconnect(sock, ip, port) < 0) {
        uwrite("udp: connect failed (datagrams: use shell udp)\n");
        return 1;
    }
    for (ticks = 0; ticks < 500; ticks++) {
        n = usend(sock, argv[3], (unsigned short)ustrlen_local(argv[3]));
        if (n > 0) break;
        uyield();
    }
    for (ticks = 0; ticks < 500; ticks++) {
        n = urecv(sock, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            uwrite(buf);
            uwrite("\n");
            break;
        }
        if (n < 0) break;
        uyield();
    }
    uclose(sock);
    return 0;
}
