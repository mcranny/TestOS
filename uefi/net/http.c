#include "http.h"
#include "socket.h"
#include "platform.h"
#include "lib/string.h"
#include "fs/tfs.h"
#include "fs/fs.h"

#define HTTP_PORT 8080U
#define HTTP_CLIENT_MAX 8U
#define HTTP_REQUEST_MAX 1024U
#define HTTP_BODY_LEN 1400U
#define HTTP_HEADER_MAX 256U
#define HTTP_PATH_MAX 96U

typedef struct {
    int handle;
    uint16_t request_len;
    uint16_t header_len;
    uint16_t header_sent;
    uint16_t body_len;
    uint16_t body_sent;
    uint8_t responding;
    uint8_t keep_alive;
    char request[HTTP_REQUEST_MAX];
    char header[HTTP_HEADER_MAX];
    char body[HTTP_BODY_LEN];
} http_client_t;

static int listener;
static http_client_t clients[HTTP_CLIENT_MAX];

static int ends(const char *s, uint16_t n)
{
    return n >= 4U && s[n - 4] == '\r' && s[n - 3] == '\n' &&
           s[n - 2] == '\r' && s[n - 1] == '\n';
}

static int header_append(http_client_t *c, const char *text, uint16_t length)
{
    uint16_t i;

    if (c == NULL || text == NULL) {
        return 0;
    }
    if ((uint32_t)c->header_len + (uint32_t)length >= HTTP_HEADER_MAX) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        c->header[c->header_len++] = text[i];
    }
    return 1;
}

static int header_append_u16(http_client_t *c, uint16_t value)
{
    char digits[5];
    uint16_t count = 0;
    uint16_t v = value;
    uint16_t i;

    if (v == 0) {
        digits[0] = '0';
        count = 1;
    } else {
        while (v > 0 && count < 5U) {
            digits[count++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
        for (i = 0; i < count / 2U; i++) {
            char tmp = digits[i];
            digits[i] = digits[count - 1U - i];
            digits[count - 1U - i] = tmp;
        }
    }
    return header_append(c, digits, count);
}

static int request_wants_keepalive(const char *req, uint16_t len)
{
    uint16_t i;
    for (i = 0; i + 22U < len; i++) {
        if ((req[i] == 'C' || req[i] == 'c') &&
            (req[i + 1] == 'o' || req[i + 1] == 'O') &&
            req[i + 10] == ':' ) {
            /* rough match Connection: */
            uint16_t j = (uint16_t)(i + 11U);
            while (j < len && (req[j] == ' ' || req[j] == '\t')) {
                j++;
            }
            if (j + 10U <= len &&
                (req[j] == 'k' || req[j] == 'K') &&
                (req[j + 1] == 'e' || req[j + 1] == 'E') &&
                (req[j + 4] == 'a' || req[j + 4] == 'A')) {
                return 1;
            }
        }
    }
    return 0;
}

static void extract_path(const char *req, uint16_t len, char *path, uint16_t path_cap)
{
    uint16_t i = 4; /* after "GET " */
    uint16_t n = 0;

    if (path == NULL || path_cap == 0) {
        return;
    }
    path[0] = '\0';
    if (len < 5 || req[0] != 'G') {
        return;
    }
    while (i < len && req[i] != ' ' && req[i] != '\r' && n + 1U < path_cap) {
        path[n++] = req[i++];
    }
    path[n] = '\0';
}

static void make_response(http_client_t *c, int status, const char *ctype, int keep)
{
    const char *reason;
    const char *prefix = "<html><body><h1>TestOS HTTP Server</h1><p>Guest-served page.</p>";
    uint16_t p = 0;
    uint16_t b = 0;
    char status_line[8];

    if (status == 200) {
        reason = "OK";
    } else if (status == 404) {
        reason = "Not Found";
    } else if (status == 405) {
        reason = "Method Not Allowed";
    } else {
        reason = "Bad Request";
    }

    if (status == 200 && c->body_len == 0) {
        while (prefix[p] != '\0' && b < HTTP_BODY_LEN) {
            c->body[b++] = prefix[p++];
        }
        while (b < HTTP_BODY_LEN - 14U) {
            c->body[b] = (char)('A' + (b % 26U));
            b++;
        }
        memcpy(&c->body[b], "</body></html>\n", 14U);
        b = HTTP_BODY_LEN;
        c->body_len = b;
        if (ctype == NULL) {
            ctype = "text/html";
        }
    } else if (status != 200) {
        const char *m = (status == 404) ? "Not found\n" :
                        (status == 405) ? "Method not allowed\n" : "Bad request\n";
        b = 0;
        while (m[b] != '\0') {
            c->body[b] = m[b];
            b++;
        }
        c->body_len = b;
        if (ctype == NULL) {
            ctype = "text/plain";
        }
    } else if (ctype == NULL) {
        ctype = "application/octet-stream";
    }

    c->header_len = 0;
    c->header_sent = 0;
    c->body_sent = 0;
    c->responding = 0;
    c->keep_alive = keep ? 1U : 0U;

    status_line[0] = (char)('0' + (status / 100));
    status_line[1] = (char)('0' + ((status / 10) % 10));
    status_line[2] = (char)('0' + (status % 10));
    status_line[3] = ' ';
    status_line[4] = '\0';

    {
        uint16_t reason_len = 0;
        uint16_t ctype_len = 0;
        while (reason[reason_len] != '\0') {
            reason_len++;
        }
        while (ctype[ctype_len] != '\0') {
            ctype_len++;
        }
        if (!header_append(c, "HTTP/1.0 ", 9U) ||
            !header_append(c, status_line, 4U) ||
            !header_append(c, reason, reason_len) ||
            !header_append(c, "\r\nContent-Type: ", 16U) ||
            !header_append(c, ctype, ctype_len) ||
            !header_append(c, "\r\nContent-Length: ", 18U) ||
            !header_append_u16(c, c->body_len) ||
            !header_append(c, keep ? "\r\nConnection: keep-alive\r\n\r\n" :
                                     "\r\nConnection: close\r\n\r\n",
                           keep ? 28U : 23U)) {
            klog(KLOG_ERROR, "HTTP", "Response header overflow");
            c->header_len = 0;
            c->body_len = 0;
            return;
        }
    }

    c->responding = 1;
}

static void handle_get(http_client_t *c)
{
    char path[HTTP_PATH_MAX];
    int keep = request_wants_keepalive(c->request, c->request_len);

    extract_path(c->request, c->request_len, path, HTTP_PATH_MAX);
    c->body_len = 0;

    if (path[0] == '/' && path[1] == '\0') {
        make_response(c, 200, "text/html", keep);
        return;
    }

    if (path[0] == '/' && tfs_is_mounted() && tfs_exists(path) && !tfs_is_directory(path)) {
        uint32_t got = 0;
        if (tfs_read(path, c->body, HTTP_BODY_LEN, &got) && got > 0) {
            c->body_len = (uint16_t)got;
            make_response(c, 200, "application/octet-stream", keep);
            return;
        }
    }

    make_response(c, 404, "text/plain", 0);
}

void http_init(void)
{
    memset(clients, 0, sizeof(clients));
    listener = socket_create();
    if (listener > 0 &&
        socket_bind(listener, HTTP_PORT) > 0 &&
        socket_listen(listener, HTTP_CLIENT_MAX) > 0) {
        klog(KLOG_INFO, "HTTP", "Ready on port 8080");
    } else {
        klog(KLOG_ERROR, "HTTP", "Failed to listen");
    }
}

void http_poll(void)
{
    uint32_t i;
    int h;
    int n;

    for (i = 0; i < HTTP_CLIENT_MAX; i++) {
        if (!clients[i].handle && listener > 0) {
            h = socket_accept(listener);
            if (h > 0) {
                memset(&clients[i], 0, sizeof(clients[i]));
                clients[i].handle = h;
            }
        }
    }

    for (i = 0; i < HTTP_CLIENT_MAX; i++) {
        http_client_t *c = &clients[i];
        if (!c->handle) {
            continue;
        }

        if (!c->responding) {
            n = socket_recv(
                c->handle,
                &c->request[c->request_len],
                (uint16_t)(HTTP_REQUEST_MAX - c->request_len)
            );
            if (n > 0) {
                c->request_len = (uint16_t)(c->request_len + n);
                if (ends(c->request, c->request_len)) {
                    if (c->request_len >= 5 &&
                        c->request[0] == 'G' && c->request[1] == 'E' &&
                        c->request[2] == 'T' && c->request[3] == ' ' &&
                        c->request[4] == '/') {
                        handle_get(c);
                    } else if (c->request_len >= 4 &&
                               c->request[0] == 'P' && c->request[1] == 'O' &&
                               c->request[2] == 'S' && c->request[3] == 'T') {
                        make_response(c, 405, "text/plain", 0);
                    } else {
                        make_response(c, 400, "text/plain", 0);
                    }
                } else if (c->request_len == HTTP_REQUEST_MAX) {
                    make_response(c, 400, "text/plain", 0);
                }
            } else if (n < 0) {
                socket_close(c->handle);
                c->handle = 0;
                continue;
            }
        }

        if (c->responding) {
            if (c->header_sent < c->header_len) {
                n = socket_send(
                    c->handle,
                    &c->header[c->header_sent],
                    (uint16_t)(c->header_len - c->header_sent)
                );
            } else {
                n = socket_send(
                    c->handle,
                    &c->body[c->body_sent],
                    (uint16_t)(c->body_len - c->body_sent)
                );
            }
            if (n > 0) {
                if (c->header_sent < c->header_len) {
                    c->header_sent = (uint16_t)(c->header_sent + n);
                } else {
                    c->body_sent = (uint16_t)(c->body_sent + n);
                }
            }
            if (c->header_sent == c->header_len && c->body_sent == c->body_len) {
                if (c->keep_alive) {
                    memset(c->request, 0, sizeof(c->request));
                    c->request_len = 0;
                    c->header_len = 0;
                    c->header_sent = 0;
                    c->body_len = 0;
                    c->body_sent = 0;
                    c->responding = 0;
                    c->keep_alive = 0;
                } else if (socket_close(c->handle) > 0) {
                    c->handle = 0;
                }
            }
        }
    }
}
