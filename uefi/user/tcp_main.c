#include "ulib.h"

static unsigned ustrlen_local(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static int streq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        uwrite("usage: tcp client <ip> <port> <msg> | tcp listen <port>\n");
        return 1;
    }

    if (streq(argv[1], "client")) {
        unsigned int ip;
        unsigned short port;
        int sock;
        int n;
        unsigned ticks;
        char buf[256];

        if (argc < 5) {
            uwrite("usage: tcp client <ip> <port> <msg>\n");
            return 1;
        }
        ip = parse_ip(argv[2]);
        if (!ip && uresolve(argv[2], &ip) != 0) {
            uwrite("tcp: bad host\n");
            return 1;
        }
        port = parse_port(argv[3]);
        if (!port) {
            uwrite("tcp: bad port\n");
            return 1;
        }
        sock = usocket();
        if (sock < 0 || uconnect(sock, ip, port) < 0) {
            uwrite("tcp: connect failed\n");
            return 1;
        }
        for (ticks = 0; ticks < 500; ticks++) {
            n = usend(sock, argv[4], (unsigned short)ustrlen_local(argv[4]));
            if (n > 0) break;
            if (n < -1) { uwrite("tcp: send error\n"); uclose(sock); return 1; }
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
            if (n == -1) break;
            if (n < -1) break;
            uyield();
        }
        uclose(sock);
        return 0;
    }

    if (streq(argv[1], "listen")) {
        unsigned short port;
        int sock;
        int client;
        char buf[256];
        int n;

        if (argc < 3) {
            uwrite("usage: tcp listen <port>\n");
            return 1;
        }
        port = parse_port(argv[2]);
        sock = usocket();
        if (sock < 0 || ubind(sock, port) < 0 || ulisten(sock, 2) < 0) {
            uwrite("tcp: listen failed\n");
            return 1;
        }
        uwrite("tcp: listening\n");
        for (;;) {
            client = uaccept(sock);
            if (client <= 0) {
                uyield();
                continue;
            }
            for (;;) {
                n = urecv(client, buf, sizeof(buf));
                if (n > 0) {
                    (void)usend(client, buf, (unsigned short)n);
                } else if (n == 0) {
                    uyield();
                } else {
                    break;
                }
            }
            uclose(client);
            break;
        }
        uclose(sock);
        return 0;
    }

    uwrite("usage: tcp client|listen ...\n");
    return 1;
}
