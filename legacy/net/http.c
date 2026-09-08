#include "http.h"
#include "socket.h"
#include "log.h"
#include "memory.h"

#define HTTP_PORT 8080U
#define HTTP_CLIENT_MAX 4U
#define HTTP_REQUEST_MAX 1024U
#define HTTP_BODY_LEN 1400U
typedef struct { int handle; uint16_t request_len, header_len, header_sent, body_len, body_sent; uint8_t responding; char request[HTTP_REQUEST_MAX]; char header[160]; char body[HTTP_BODY_LEN]; } http_client_t;
static int listener; static http_client_t clients[HTTP_CLIENT_MAX];
static int ends(const char *s, uint16_t n) { return n >= 4U && s[n-4]=='\r' && s[n-3]=='\n' && s[n-2]=='\r' && s[n-1]=='\n'; }
static void make_response(http_client_t *c, int status)
{
    const char *reason = status == 200 ? "OK" : (status == 404 ? "Not Found" : (status == 405 ? "Method Not Allowed" : "Bad Request"));
    const char *prefix = "<html><body><h1>TestOS HTTP Server</h1><p>Guest-served page.</p>";
    uint16_t i, p = 0, b = 0;
    if (status == 200) { while (prefix[p] && b < HTTP_BODY_LEN) c->body[b++] = prefix[p++]; while (b < HTTP_BODY_LEN - 14U) { c->body[b] = (char)('A' + (b % 26U)); b++; } memcpy(&c->body[b], "</body></html>\n", 14U); b = HTTP_BODY_LEN; }
    else { const char *m = status == 404 ? "Not found\n" : (status == 405 ? "Method not allowed\n" : "Bad request\n"); while (m[b]) { c->body[b] = m[b]; b++; } }
    c->body_len = b; c->header_len = 0; c->header[c->header_len++]='H'; c->header[c->header_len++]='T'; c->header[c->header_len++]='T'; c->header[c->header_len++]='P'; c->header[c->header_len++]='/'; c->header[c->header_len++]='1'; c->header[c->header_len++]='.'; c->header[c->header_len++]='0'; c->header[c->header_len++]=' ';
    { char code[4]; code[0]=(char)('0'+status/100); code[1]=(char)('0'+(status/10)%10); code[2]=(char)('0'+status%10); code[3]=0; for(i=0;i<3;i++)c->header[c->header_len++]=code[i]; }
    c->header[c->header_len++]=' '; for(i=0;reason[i];i++) c->header[c->header_len++]=reason[i]; memcpy(&c->header[c->header_len], "\r\nContent-Type: text/html\r\nContent-Length: ", 43U); c->header_len += 43U;
    { char digits[5]; uint16_t v=b; digits[0]=(char)('0'+v/1000U); digits[1]=(char)('0'+(v/100U)%10U); digits[2]=(char)('0'+(v/10U)%10U); digits[3]=(char)('0'+v%10U); for(i=0;i<4;i++) if(i || digits[i]!='0' || i==3) c->header[c->header_len++]=digits[i]; }
    memcpy(&c->header[c->header_len], "\r\nConnection: close\r\n\r\n", 23U); c->header_len += 23U; c->body_sent = 0; c->header_sent = 0; c->responding = 1;
}
void http_init(void)
{ uint32_t i; memset(clients,0,sizeof(clients)); listener=socket_create(); if(listener>0 && socket_bind(listener,HTTP_PORT)>0 && socket_listen(listener,HTTP_CLIENT_MAX)>0) klog(KLOG_INFO,"HTTP","Ready on port 8080"); else klog(KLOG_ERROR,"HTTP","Failed to listen"); (void)i; }
void http_poll(void)
{ uint32_t i; int h,n; for(i=0;i<HTTP_CLIENT_MAX;i++) if(!clients[i].handle && listener>0) { h=socket_accept(listener); if(h>0) { memset(&clients[i], 0, sizeof(clients[i])); clients[i].handle=h; } }
  for(i=0;i<HTTP_CLIENT_MAX;i++) { http_client_t *c=&clients[i]; if(!c->handle) continue; if(!c->responding) { n=socket_recv(c->handle,&c->request[c->request_len],(uint16_t)(HTTP_REQUEST_MAX-c->request_len)); if(n>0) { c->request_len=(uint16_t)(c->request_len+n); if(ends(c->request,c->request_len)) { if(c->request_len>=5 && c->request[0]=='G'&&c->request[1]=='E'&&c->request[2]=='T'&&c->request[3]==' '&&c->request[4]=='/') make_response(c,(c->request_len>5&&c->request[5]==' ')?200:404); else if(c->request_len>=4&&c->request[0]=='P'&&c->request[1]=='O'&&c->request[2]=='S'&&c->request[3]=='T') make_response(c,405); else make_response(c,400); } else if(c->request_len==HTTP_REQUEST_MAX) make_response(c,400); } else if(n<0) { socket_close(c->handle); c->handle=0; continue; } }
    if(c->responding) { if(c->header_sent<c->header_len) n=socket_send(c->handle,&c->header[c->header_sent],(uint16_t)(c->header_len-c->header_sent)); else n=socket_send(c->handle,&c->body[c->body_sent],(uint16_t)(c->body_len-c->body_sent)); if(n>0) { if(c->header_sent<c->header_len)c->header_sent=(uint16_t)(c->header_sent+n); else c->body_sent=(uint16_t)(c->body_sent+n); } if(c->header_sent==c->header_len&&c->body_sent==c->body_len && socket_close(c->handle)>0)c->handle=0; } }
}
