#include "ulib.h"

static unsigned ustrlen_local(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static int starts_with(const char *s, const char *p)
{
    unsigned i = 0;
    while (p[i]) {
        if (s[i] != p[i]) return 0;
        i++;
    }
    return 1;
}

int main(int argc, char **argv)
{
    char host[64];
    char path[96];
    char outpath[96];
    unsigned int ip = 0;
    unsigned short port = 80;
    unsigned i = 0;
    unsigned hi = 0;
    unsigned pi = 0;
    int sock;
    int n;
    unsigned ticks;
    char req[192];
    unsigned ri = 0;
    char buf[1500];
    char body[1400];
    unsigned body_len = 0;
    int header_done = 0;
    unsigned header_end = 0;
    const char *url;

    if (argc < 3) {
        uwrite("usage: wget <host[/path]|http://host/path> <file>\n");
        return 1;
    }
    url = argv[1];
    i = 0;
    if (starts_with(url, "http://")) {
        i = 7;
    }
    while (url[i] && url[i] != '/' && url[i] != ':' && hi + 1 < sizeof(host)) {
        host[hi++] = url[i++];
    }
    host[hi] = '\0';
    if (url[i] == ':') {
        i++;
        port = 0;
        while (url[i] >= '0' && url[i] <= '9') {
            port = (unsigned short)(port * 10 + (url[i] - '0'));
            i++;
        }
        if (!port) port = 80;
    }
    path[0] = '/';
    path[1] = '\0';
    if (url[i] == '/') {
        pi = 0;
        while (url[i] && pi + 1 < sizeof(path)) {
            path[pi++] = url[i++];
        }
        path[pi] = '\0';
    }

    {
        unsigned o = 0;
        while (argv[2][o] && o + 1 < sizeof(outpath)) {
            outpath[o] = argv[2][o];
            o++;
        }
        outpath[o] = '\0';
    }

    if (uresolve(host, &ip) != 0) {
        uwrite("wget: resolve failed\n");
        return 1;
    }

    sock = usocket();
    if (sock < 0 || uconnect(sock, ip, port) < 0) {
        uwrite("wget: connect failed\n");
        return 1;
    }

    {
        const char *p1 = "GET ";
        const char *p2 = " HTTP/1.0\r\nHost: ";
        const char *p3 = "\r\nConnection: close\r\n\r\n";
        for (i = 0; p1[i] && ri + 1 < sizeof(req); i++) req[ri++] = p1[i];
        for (i = 0; path[i] && ri + 1 < sizeof(req); i++) req[ri++] = path[i];
        for (i = 0; p2[i] && ri + 1 < sizeof(req); i++) req[ri++] = p2[i];
        for (i = 0; host[i] && ri + 1 < sizeof(req); i++) req[ri++] = host[i];
        for (i = 0; p3[i] && ri + 1 < sizeof(req); i++) req[ri++] = p3[i];
    }

    for (ticks = 0; ticks < 1600; ticks++) {
        n = usend(sock, req, (unsigned short)ri);
        if (n > 0) break;
        if (n < -1) {
            uwrite("wget: send failed\n");
            uclose(sock);
            return 1;
        }
        uyield();
    }
    if (ticks >= 1600) {
        uwrite("wget: send timeout\n");
        uclose(sock);
        return 1;
    }

    for (ticks = 0; ticks < 1500; ticks++) {
        n = urecv(sock, buf, sizeof(buf));
        if (n > 0) {
            int j;
            for (j = 0; j < n; j++) {
                if (!header_done) {
                    if (body_len < sizeof(body)) body[body_len++] = buf[j];
                    if (body_len >= 4 &&
                        body[body_len - 4] == '\r' && body[body_len - 3] == '\n' &&
                        body[body_len - 2] == '\r' && body[body_len - 1] == '\n') {
                        header_done = 1;
                        header_end = body_len;
                    }
                } else if (body_len < sizeof(body)) {
                    body[body_len++] = buf[j];
                }
            }
        } else if (n == -1) {
            break;
        } else if (n < -1) {
            break;
        } else {
            uyield();
        }
    }
    uclose(sock);

    if (!header_done) {
        uwrite("wget: no response\n");
        return 1;
    }

    if (ufswrite(outpath, &body[header_end], body_len - header_end) < 0) {
        uwrite("wget: write failed\n");
        return 1;
    }
    uwrite("wget: saved ");
    uwrite_u32(body_len - header_end);
    uwrite(" bytes\n");
    (void)ustrlen_local;
    return 0;
}
