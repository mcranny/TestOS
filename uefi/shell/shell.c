#include "shell/shell.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/device.h"
#include "arch/io.h"
#include "fs/tfs.h"
#include "fs/fs.h"
#include "fs/path.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "task/process.h"
#include "user/exec.h"
#include "platform.h"
#include "version.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "udp.h"
#include "netif.h"
#include "route.h"
#include "arp.h"
#include "dhcp.h"
#include "dns.h"
#include "socket.h"
#include "tcp.h"
#include "mac.h"
#include "http.h"

#define LINE_MAX 256

static char cwd[FS_MAX_PATH] = "/";

static char fold_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int command_is(const char *line, const char *command)
{
    while (*command) {
        if (fold_lower(*line) != fold_lower(*command)) return 0;
        line++;
        command++;
    }
    return *line == '\0' || *line == ' ';
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ') s++;
    return s;
}

static void u32_to_dec(uint32_t value, char *out)
{
    char tmp[11];
    int n = 0;
    int i;
    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (value > 0) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void print_u32(uint32_t value)
{
    char buf[12];
    u32_to_dec(value, buf);
    console_puts(buf);
}

static void next_arg(const char **cursor, char *out, size_t out_size)
{
    const char *s = skip_spaces(*cursor);
    size_t i = 0;
    while (*s && *s != ' ' && i + 1 < out_size) {
        out[i++] = *s++;
    }
    out[i] = 0;
    *cursor = skip_spaces(s);
}

static int shell_parse_ipv4(const char *text, ipv4_addr_t *out);
static int shell_parse_ipv4_prefix(const char **text_inout, ipv4_addr_t *out);
static int shell_parse_u16(const char **text_inout, uint16_t *out);

static int resolve(const char *path, char *out)
{
    return path_resolve(cwd, path, out, FS_MAX_PATH);
}

static void system_reboot(void)
{
    uint8_t status;
    do {
        status = inb(0x64);
    } while (status & 0x02);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void shell_help(void)
{
    console_puts("Available commands:\n");
    console_puts("  help     - show this message\n");
    console_puts("  version  - show TestOS version\n");
    console_puts("  uptime   - show system uptime\n");
    console_puts("  clear    - clear the screen\n");
    console_puts("  echo     - print text\n");
    console_puts("  mem      - show memory information\n");
    console_puts("  heap     - show heap free bytes\n");
    console_puts("  pci      - list PCI devices\n");
    console_puts("  ls       - list directory\n");
    console_puts("  cd       - change directory\n");
    console_puts("  pwd      - print working directory\n");
    console_puts("  cat      - print a file\n");
    console_puts("  write    - write text to a file\n");
    console_puts("  mkdir    - create a directory\n");
    console_puts("  rm       - remove a file or empty directory\n");
    console_puts("  touch    - create an empty file\n");
    console_puts("  cp       - copy a file\n");
    console_puts("  mv       - move/rename a file\n");
    console_puts("  ./path   - execute a program\n");
    console_puts("  exec     - execute a program\n");
    console_puts("  calc     - run the calculator\n");
    console_puts("  sleep    - block for N timer ticks\n");
    console_puts("  ps       - list processes\n");
    console_puts("  kill     - terminate a process\n");
    console_puts("  netrx    - poll E1000 for received Ethernet frames\n");
    console_puts("  ifconfig - show/set interface configuration\n");
    console_puts("  ip       - alias for ifconfig\n");
    console_puts("  route    - show/set default gateway\n");
    console_puts("  arp      - show/delete ARP cache\n");
    console_puts("  dhcp     - renew DHCP lease\n");
    console_puts("  dns      - resolve a hostname (A record)\n");
    console_puts("  netstat  - network interface and socket status\n");
    console_puts("  ping     - ICMP echo request to an IPv4 address or host\n");
    console_puts("  udp      - send a UDP datagram\n");
    console_puts("  wget     - HTTP GET to a TFS file\n");
    console_puts("  fsck     - check filesystem consistency\n");
    console_puts("  reboot   - reboot the system\n");
    console_puts("  halt     - halt the CPU\n");
    console_puts("  panic    - halt the kernel\n");
}

static void shell_version(void)
{
    console_puts(TESTOS_NAME " " TESTOS_VERSION " (x86-64)\n");
}

static void shell_uptime(void)
{
    uint64_t ticks = timer_ticks();
    uint64_t hz = timer_hz();
    uint32_t seconds = (hz != 0) ? (uint32_t)(ticks / hz) : 0;
    console_puts("Uptime: ");
    print_u32(seconds);
    console_puts(" seconds (");
    print_u32((uint32_t)ticks);
    console_puts(" ticks)\n");
}

static void shell_mem(void)
{
    console_puts("Memory:\n  free frames: ");
    print_u32((uint32_t)pmm_free_frames());
    console_puts("\n  total frames: ");
    print_u32((uint32_t)pmm_total_frames());
    console_puts("\n");
}

static void shell_heap(void)
{
    console_puts("Heap free=");
    console_write_hex64(heap_get_free_bytes());
    console_puts(" used=");
    console_write_hex64(heap_get_used_bytes());
    console_puts("\n");
}

static void shell_pci(void)
{
    device_t *dev = device_get_list();
    if (!dev) {
        console_puts("No PCI devices\n");
        return;
    }
    while (dev) {
        console_puts(dev->name ? dev->name : "pci");
        console_puts("\n");
        dev = dev->next;
    }
}

static void shell_echo(const char *args)
{
    args = skip_spaces(args);
    if (*args) console_puts(args);
    console_puts("\n");
}

static void shell_ls(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!tfs_is_mounted()) {
        console_puts("ls: no filesystem\n");
        return;
    }
    if (resolve(*args ? args : ".", path) != 0) {
        console_puts("ls: bad path\n");
        return;
    }
    tfs_list_directory(path);
}

static void shell_cd(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!tfs_is_mounted()) {
        console_puts("cd: no filesystem\n");
        return;
    }
    if (resolve(*args ? args : "/", path) != 0 || !tfs_is_directory(path)) {
        console_puts("cd: failed\n");
        return;
    }
    {
        size_t i = 0;
        while (path[i] && i + 1 < sizeof(cwd)) {
            cwd[i] = path[i];
            i++;
        }
        cwd[i] = 0;
    }
}

static void shell_pwd(void)
{
    console_puts(cwd);
    console_puts("\n");
}

static void shell_mkdir(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_mkdir(path)) {
        console_puts("mkdir: failed\n");
    }
}

static void shell_rm(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_remove(path)) {
        console_puts("rm: failed\n");
    }
}

static void shell_touch(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_touch(path)) {
        console_puts("touch: failed\n");
    }
}

static void shell_cat(const char *args)
{
    char path[FS_MAX_PATH];
    char *buf;
    uint32_t n = 0;
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0) {
        console_puts("cat: failed\n");
        return;
    }
    buf = (char *)kmalloc(FS_MAX_FILE_SIZE + 1);
    if (!buf || !tfs_read(path, buf, FS_MAX_FILE_SIZE, &n)) {
        console_puts("cat: failed\n");
        if (buf) kfree(buf);
        return;
    }
    buf[n] = 0;
    console_puts(buf);
    if (n == 0 || buf[n - 1] != '\n') console_puts("\n");
    kfree(buf);
}

static void shell_write(const char *args)
{
    char path[FS_MAX_PATH];
    char name[FS_MAX_PATH];
    const char *rest = skip_spaces(args);
    uint32_t len = 0;
    next_arg(&rest, name, sizeof(name));
    while (rest[len]) len++;
    if (!name[0] || resolve(name, path) != 0 || !tfs_write(path, rest, len, 0)) {
        console_puts("write: failed\n");
    }
}

static void shell_cp(const char *args)
{
    char src[FS_MAX_PATH], dst[FS_MAX_PATH], sres[FS_MAX_PATH], dres[FS_MAX_PATH];
    char *buf;
    uint32_t n = 0;
    const char *rest = skip_spaces(args);
    next_arg(&rest, src, sizeof(src));
    next_arg(&rest, dst, sizeof(dst));
    if (!src[0] || !dst[0] || resolve(src, sres) || resolve(dst, dres)) {
        console_puts("cp: failed\n");
        return;
    }
    buf = (char *)kmalloc(FS_MAX_FILE_SIZE);
    if (!buf || !tfs_read(sres, buf, FS_MAX_FILE_SIZE, &n) || !tfs_write(dres, buf, n, 0)) {
        console_puts("cp: failed\n");
    }
    if (buf) kfree(buf);
}

static void shell_mv(const char *args)
{
    char src[FS_MAX_PATH], dst[FS_MAX_PATH], sres[FS_MAX_PATH], dres[FS_MAX_PATH];
    const char *rest = skip_spaces(args);
    next_arg(&rest, src, sizeof(src));
    next_arg(&rest, dst, sizeof(dst));
    if (!src[0] || !dst[0] || resolve(src, sres) || resolve(dst, dres) || !tfs_rename(sres, dres)) {
        console_puts("mv: failed\n");
    }
}

static void shell_sleep(const char *args)
{
    uint32_t n = 0;
    args = skip_spaces(args);
    while (*args >= '0' && *args <= '9') {
        n = n * 10 + (uint32_t)(*args - '0');
        args++;
    }
    process_sleep_ticks(n ? n : 1);
}

static void shell_ps(void)
{
    uint32_t i;
    console_puts("PID STATE NAME\n");
    for (i = 0; i < process_table_count(); i++) {
        process_t *p = process_at(i);
        const char *st;
        if (!p || p->state == PROC_UNUSED) continue;
        switch (p->state) {
        case PROC_READY: st = "ready"; break;
        case PROC_RUNNING: st = "run"; break;
        case PROC_SLEEPING: st = "sleep"; break;
        case PROC_ZOMBIE: st = "zombie"; break;
        default: st = "?"; break;
        }
        print_u32(p->pid);
        console_puts(" ");
        console_puts(st);
        console_puts(" ");
        console_puts(p->name);
        console_puts("\n");
    }
}

static void shell_kill(const char *args)
{
    uint32_t pid = 0;
    process_t *p;
    args = skip_spaces(args);
    while (*args >= '0' && *args <= '9') {
        pid = pid * 10 + (uint32_t)(*args - '0');
        args++;
    }
    if (!pid) {
        console_puts("kill: usage: kill <pid>\n");
        return;
    }
    p = process_find_by_pid(pid);
    if (p && p->protected) {
        console_puts("kill: cannot kill system process\n");
        return;
    }
    if (process_terminate(pid) != 0) {
        console_puts("kill: failed\n");
    }
}

static void shell_fsck(void)
{
    if (!tfs_is_mounted()) {
        console_puts("fsck: not mounted\n");
        return;
    }
    if (tfs_check() == 0) {
        console_puts("fsck: ok\n");
    } else {
        console_puts("fsck: errors\n");
    }
}

static void shell_exec(const char *line)
{
    char path[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    const char *args[8];
    char argbuf[8][64];
    int argc = 0;
    const char *rest = skip_spaces(line);

    if (rest[0] == '.' && rest[1] == '/') {
        rest += 2;
    } else if (command_is(rest, "exec")) {
        rest = skip_spaces(rest + 4);
    }

    next_arg(&rest, path, sizeof(path));
    if (!path[0]) {
        console_puts("exec: usage: ./prog [args]\n");
        return;
    }
    if (path[0] != '/') {
        char tmp[FS_MAX_PATH];
        tmp[0] = '/';
        {
            size_t i = 0;
            while (path[i] && i + 1 < sizeof(tmp) - 1) {
                tmp[i + 1] = path[i];
                i++;
            }
            tmp[i + 1] = 0;
        }
        {
            size_t i = 0;
            while (tmp[i] && i + 1 < sizeof(path)) {
                path[i] = tmp[i];
                i++;
            }
            path[i] = 0;
        }
    }
    if (resolve(path, resolved) != 0) {
        console_puts("exec: bad path\n");
        return;
    }
    args[argc++] = resolved;
    while (argc < 8 && *rest) {
        next_arg(&rest, argbuf[argc], sizeof(argbuf[argc]));
        if (!argbuf[argc][0]) break;
        args[argc] = argbuf[argc];
        argc++;
    }
    if (process_exec(resolved, argc, args) != 0) {
        console_puts("exec: failed\n");
    }
}

static void shell_calc(const char *args)
{
    const char *argv[8];
    char path[] = "/calc";
    char argbuf[6][64];
    int argc = 1;
    const char *rest = skip_spaces(args);
    argv[0] = path;
    while (argc < 7 && *rest) {
        next_arg(&rest, argbuf[argc], sizeof(argbuf[argc]));
        if (!argbuf[argc][0]) break;
        argv[argc] = argbuf[argc];
        argc++;
    }
    if (argc < 2) {
        console_puts("Usage: calc <expression>\n");
        return;
    }
    if (process_exec(path, argc, argv) != 0) {
        console_puts("calc: failed (is /calc seeded?)\n");
    }
}

static void shell_print_ip(ipv4_addr_t ip)
{
    char ip_str[16];
    ipv4_addr_format(ip, ip_str);
    console_puts(ip_str);
}

static void shell_print_mac(const mac_addr_t *mac)
{
    char mac_str[18];
    if (mac == NULL) {
        console_puts("(none)");
        return;
    }
    mac_format(mac, mac_str);
    console_puts(mac_str);
}

static void shell_ifconfig(const char *args)
{
    netif_t *nif;
    char name[NETIF_NAME_MAX];
    const char *cursor = skip_spaces(args);

    if (*cursor == '\0') {
        uint32_t i;
        for (i = 0; i < netif_count(); i++) {
            nif = netif_get_index(i);
            if (nif == NULL || !nif->active) {
                continue;
            }
            console_puts("Interface: ");
            console_puts(nif->name);
            console_puts("\nMAC:       ");
            shell_print_mac(&nif->mac);
            console_puts("\nIP:        ");
            shell_print_ip(nif->ip);
            console_puts("\nMask:      ");
            shell_print_ip(nif->netmask);
            console_puts("\nGateway:   ");
            shell_print_ip(nif->gateway);
            console_puts("\nDNS:       ");
            shell_print_ip(nif->dns[0]);
            console_puts("\nRX:  ");
            print_u32(nif->rx_packets);
            console_puts(" packets\nTX:  ");
            print_u32(nif->tx_packets);
            console_puts(" packets\nDROP: ");
            print_u32(nif->drop_packets);
            console_puts("\n");
        }
        return;
    }

    next_arg(&cursor, name, sizeof(name));
    nif = netif_find(name);
    if (nif == NULL) {
        nif = netif_get_primary();
    }
    if (nif == NULL) {
        console_puts("ifconfig: no interface\n");
        return;
    }

    if (command_is(cursor, "up")) {
        netif_add_flags(nif, NETIF_FLAG_UP | NETIF_FLAG_RUNNING);
        console_puts("up\n");
        return;
    }
    if (command_is(cursor, "down")) {
        netif_clear_flags(nif, NETIF_FLAG_UP | NETIF_FLAG_RUNNING);
        console_puts("down\n");
        return;
    }

    {
        ipv4_addr_t ip = 0;
        ipv4_addr_t mask = nif->netmask;
        ipv4_addr_t gw = nif->gateway;
        if (!shell_parse_ipv4_prefix(&cursor, &ip)) {
            console_puts("usage: ifconfig [iface] [<ip> [mask] [gw]]\n");
            return;
        }
        cursor = skip_spaces(cursor);
        if (*cursor != '\0') {
            (void)shell_parse_ipv4_prefix(&cursor, &mask);
        }
        cursor = skip_spaces(cursor);
        if (*cursor != '\0') {
            (void)shell_parse_ipv4_prefix(&cursor, &gw);
        }
        netif_set_addr(nif, ip, mask != 0 ? mask : IPV4_ADDR(255, 255, 255, 0), gw);
        arp_set_local_ip(ip);
        route_set_defaults_from_netif(nif);
        console_puts("configured\n");
    }
}

static void shell_route(const char *args)
{
    const char *cursor = skip_spaces(args);
    uint32_t i;

    if (*cursor == '\0') {
        for (i = 0; i < route_count(); i++) {
            const route_entry_t *r = route_get_index(i);
            if (r == NULL) {
                continue;
            }
            console_puts("dst ");
            shell_print_ip(r->network);
            console_puts(" mask ");
            shell_print_ip(r->netmask);
            console_puts(" gw ");
            shell_print_ip(r->gateway);
            if (r->nif != NULL) {
                console_puts(" dev ");
                console_puts(r->nif->name);
            }
            console_puts("\n");
        }
        return;
    }

    if (command_is(cursor, "add") || command_is(cursor, "default")) {
        ipv4_addr_t gw = 0;
        netif_t *nif = netif_get_primary();
        if (command_is(cursor, "add")) {
            cursor += 3;
        } else {
            cursor += 7;
        }
        cursor = skip_spaces(cursor);
        if (command_is(cursor, "default")) {
            cursor += 7;
            cursor = skip_spaces(cursor);
        }
        if (command_is(cursor, "gw") || command_is(cursor, "via")) {
            while (*cursor && *cursor != ' ') cursor++;
            cursor = skip_spaces(cursor);
        }
        if (!shell_parse_ipv4(cursor, &gw)) {
            console_puts("usage: route add default gw <ip>\n");
            return;
        }
        if (nif != NULL) {
            nif->gateway = gw;
            route_set_default_gateway(gw, nif);
        }
        console_puts("route set\n");
        return;
    }
    console_puts("usage: route | route add default gw <ip>\n");
}

static void shell_arp(const char *args)
{
    const char *cursor = skip_spaces(args);
    uint32_t i;
    arp_cache_entry_t entry;

    if (*cursor == '\0') {
        for (i = 0; arp_cache_get(i, &entry); i++) {
            shell_print_ip(entry.ip);
            console_puts(" at ");
            shell_print_mac(&entry.mac);
            console_puts("\n");
        }
        if (i == 0) {
            console_puts("(empty)\n");
        }
        return;
    }
    if (command_is(cursor, "del") || command_is(cursor, "delete")) {
        ipv4_addr_t ip;
        while (*cursor && *cursor != ' ') cursor++;
        cursor = skip_spaces(cursor);
        if (!shell_parse_ipv4(cursor, &ip)) {
            console_puts("usage: arp del <ip>\n");
            return;
        }
        if (arp_delete(ip)) {
            console_puts("deleted\n");
        } else {
            console_puts("not found\n");
        }
        return;
    }
    console_puts("usage: arp | arp del <ip>\n");
}

static void shell_dhcp(const char *args)
{
    netif_t *nif = netif_get_primary();
    (void)args;
    if (nif == NULL) {
        console_puts("dhcp: no interface\n");
        return;
    }
    console_puts("dhcp: renewing...\n");
    if (dhcp_renew(nif)) {
        console_puts("dhcp: bound ");
        shell_print_ip(nif->ip);
        console_puts("\n");
    } else {
        netif_apply_static_defaults(nif);
        arp_set_local_ip(nif->ip);
        route_set_defaults_from_netif(nif);
        console_puts("dhcp: failed; static fallback\n");
    }
}

static void shell_dns(const char *args)
{
    ipv4_addr_t ip;
    char host[96];
    const char *cursor = skip_spaces(args);

    next_arg(&cursor, host, sizeof(host));
    if (host[0] == '\0') {
        console_puts("usage: dns <hostname>\n");
        return;
    }
    if (!dns_resolve(host, &ip)) {
        console_puts("dns: lookup failed\n");
        return;
    }
    console_puts(host);
    console_puts(" -> ");
    shell_print_ip(ip);
    console_puts("\n");
}

static void shell_netstat(const char *args)
{
    netif_t *nif = netif_get_primary();
    uint32_t i;
    socket_info_t sinfo;
    tcp_conn_info_t tinfo;
    (void)args;

    if (nif != NULL) {
        console_puts("Interface: ");
        console_puts(nif->name);
        console_puts("\nMAC:       ");
        shell_print_mac(&nif->mac);
        console_puts("\nIP:        ");
        shell_print_ip(nif->ip);
        console_puts("\nMask:      ");
        shell_print_ip(nif->netmask);
        console_puts("\nGateway:   ");
        shell_print_ip(nif->gateway);
        console_puts("\nDNS:       ");
        shell_print_ip(nif->dns[0]);
        console_puts("\nRX:  ");
        print_u32(nif->rx_packets);
        console_puts(" packets\nTX:  ");
        print_u32(nif->tx_packets);
        console_puts(" packets\nDROP: ");
        print_u32(nif->drop_packets);
        console_puts("\n\n");
    }

    console_puts("TCP connections:\n");
    for (i = 0; tcp_conn_get(i, &tinfo); i++) {
        console_puts("  ");
        console_puts(tcp_state_name(tinfo.state));
        console_puts(" local:");
        print_u32(tinfo.local_port);
        console_puts(" remote ");
        shell_print_ip(tinfo.remote_ip);
        console_puts(":");
        print_u32(tinfo.remote_port);
        console_puts("\n");
    }
    if (i == 0) {
        console_puts("  (none)\n");
    }

    console_puts("Sockets:\n");
    for (i = 0; socket_get_info(i, &sinfo); i++) {
        console_puts("  h=");
        print_u32((uint32_t)sinfo.handle);
        console_puts(" st=");
        print_u32((uint32_t)sinfo.state);
        console_puts(" port=");
        print_u32(sinfo.port);
        console_puts("\n");
    }
    if (i == 0) {
        console_puts("  (none)\n");
    }
}

static void shell_wget(const char *args)
{
    char url[128];
    char outpath[FS_MAX_PATH];
    char host[64];
    char path[96];
    ipv4_addr_t ip;
    uint16_t port = 80;
    int sock;
    uint16_t local_port;
    static uint16_t wget_ephemeral = 40000U;
    char req[192];
    uint32_t ri = 0;
    uint8_t buf[1500];
    uint8_t body[1400];
    uint32_t body_len = 0;
    uint32_t start;
    int header_done = 0;
    uint32_t header_end = 0;
    int n;
    const char *cursor = skip_spaces(args);
    uint32_t i;
    uint32_t hi = 0;
    uint32_t pi = 0;
    int in_path = 0;

    next_arg(&cursor, url, sizeof(url));
    next_arg(&cursor, outpath, sizeof(outpath));
    if (url[0] == '\0' || outpath[0] == '\0') {
        console_puts("usage: wget <host[/path]|http://host/path> <file>\n");
        return;
    }

    /* Strip optional http:// */
    i = 0;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
        url[4] == ':' && url[5] == '/' && url[6] == '/') {
        i = 7;
    }
    host[0] = '\0';
    path[0] = '/';
    path[1] = '\0';
    while (url[i] != '\0' && url[i] != '/' && url[i] != ':' && hi + 1U < sizeof(host)) {
        host[hi++] = url[i++];
    }
    host[hi] = '\0';
    if (url[i] == ':') {
        i++;
        port = 0;
        while (url[i] >= '0' && url[i] <= '9') {
            port = (uint16_t)(port * 10U + (uint16_t)(url[i] - '0'));
            i++;
        }
        if (port == 0) {
            port = 80;
        }
    }
    if (url[i] == '/') {
        pi = 0;
        while (url[i] != '\0' && pi + 1U < sizeof(path)) {
            path[pi++] = url[i++];
        }
        path[pi] = '\0';
        in_path = 1;
    }
    (void)in_path;

    if (!dns_resolve(host, &ip)) {
        console_puts("wget: resolve failed\n");
        return;
    }

    sock = socket_create();
    if (sock < 0) {
        console_puts("wget: socket failed\n");
        return;
    }
    local_port = wget_ephemeral++;
    if (wget_ephemeral < 40000U) {
        wget_ephemeral = 40000U;
    }
    if (socket_connect(sock, ip, port, local_port) < 0) {
        console_puts("wget: connect failed\n");
        socket_close(sock);
        return;
    }

    /* Build request */
    {
        static const char p1[] = "GET ";
        static const char p2[] = " HTTP/1.0\r\nHost: ";
        static const char p3[] = "\r\nConnection: close\r\n\r\n";
        for (i = 0; p1[i]; i++) req[ri++] = p1[i];
        for (i = 0; path[i] && ri + 1U < sizeof(req); i++) req[ri++] = path[i];
        for (i = 0; p2[i] && ri + 1U < sizeof(req); i++) req[ri++] = p2[i];
        for (i = 0; host[i] && ri + 1U < sizeof(req); i++) req[ri++] = host[i];
        for (i = 0; p3[i] && ri + 1U < sizeof(req); i++) req[ri++] = p3[i];
        req[ri] = '\0';
    }

    start = timer_get_ticks();
    n = SOCKET_WOULD_BLOCK;
    while ((timer_get_ticks() - start) < (8U * TIMER_FREQUENCY)) {
        (void)ethernet_poll();
        http_poll();
        n = socket_send(sock, req, (uint16_t)ri);
        if (n > 0) {
            break;
        }
        if (n == SOCKET_ERROR) {
            console_puts("wget: send failed\n");
            socket_close(sock);
            return;
        }
    }
    if (n <= 0) {
        console_puts("wget: send timeout\n");
        socket_close(sock);
        return;
    }

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < (8U * TIMER_FREQUENCY)) {
        (void)ethernet_poll();
        http_poll();
        n = socket_recv(sock, buf, sizeof(buf));
        if (n > 0) {
            uint32_t j;
            for (j = 0; j < (uint32_t)n; j++) {
                if (!header_done) {
                    /* accumulate into body temporarily for header scan */
                    if (body_len < sizeof(body)) {
                        body[body_len++] = buf[j];
                    }
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
        } else if (n == SOCKET_EOF) {
            break;
        } else if (n == SOCKET_ERROR) {
            break;
        }
    }
    (void)socket_close(sock);

    if (!header_done) {
        console_puts("wget: no response\n");
        return;
    }

    {
        uint32_t content_len = body_len - header_end;
        char resolved[FS_MAX_PATH];
        if (resolve(outpath, resolved) != 0) {
            console_puts("wget: bad path\n");
            return;
        }
        if (!tfs_write(resolved, &body[header_end], content_len, 0)) {
            console_puts("wget: write failed\n");
            return;
        }
        console_puts("wget: saved ");
        print_u32(content_len);
        console_puts(" bytes to ");
        console_puts(resolved);
        console_puts("\n");
    }
}

static void shell_netrx(void)
{
    int frames;
    char number[12];

    frames = ethernet_poll();
    if (frames == 0) {
        console_puts("netrx: no frames\n");
        return;
    }

    console_puts("netrx: frames=");
    u32_to_dec((uint32_t)frames, number);
    console_puts(number);
    console_puts("\n");
}

static int shell_parse_ipv4(const char *text, ipv4_addr_t *out)
{
    uint32_t octets[4];
    uint32_t i;
    uint32_t value;

    if (text == NULL || out == NULL) {
        return 0;
    }

    text = skip_spaces(text);

    for (i = 0; i < 4U; i++) {
        if (*text < '0' || *text > '9') {
            return 0;
        }

        value = 0;
        while (*text >= '0' && *text <= '9') {
            value = (value * 10U) + (uint32_t)(*text - '0');
            if (value > 255U) {
                return 0;
            }
            text++;
        }

        octets[i] = value;

        if (i < 3U) {
            if (*text != '.') {
                return 0;
            }
            text++;
        }
    }

    text = skip_spaces(text);
    if (*text != '\0') {
        return 0;
    }

    *out = IPV4_ADDR(octets[0], octets[1], octets[2], octets[3]);
    return 1;
}

static void shell_ping(const char *args)
{
    ipv4_addr_t dst;
    char ip_str[16];
    char host[96];
    uint64_t start;
    uint64_t sent_tick;
    static uint16_t ping_seq = 1U;
    const uint16_t ping_id = 0x544FU;
    uint16_t seq;
    uint32_t count = 4;
    uint32_t ok = 0;
    uint32_t i;
    const char *cursor = skip_spaces(args);

    if (*cursor == '\0') {
        console_puts("usage: ping <ip|host> [count]\n");
        return;
    }

    next_arg(&cursor, host, sizeof(host));
    if (*cursor >= '0' && *cursor <= '9') {
        uint16_t c16 = 0;
        if (shell_parse_u16(&cursor, &c16) && c16 > 0 && c16 <= 20) {
            count = c16;
        }
    }

    if (!shell_parse_ipv4(host, &dst)) {
        if (!dns_resolve(host, &dst)) {
            console_puts("ping: resolve failed\n");
            return;
        }
    }

    ipv4_addr_format(dst, ip_str);
    console_puts("PING ");
    console_puts(ip_str);
    console_puts("\n");

    for (i = 0; i < count; i++) {
        seq = ping_seq++;
        if (ping_seq == 0U) {
            ping_seq = 1U;
        }

        icmp_arm_echo_wait(dst, ping_id, seq);
        sent_tick = timer_get_ticks();
        if (!icmp_send_echo_request(dst, ping_id, seq)) {
            /* ARP may still be resolving */
        }

        start = timer_get_ticks();
        while ((timer_get_ticks() - start) < (2U * (uint64_t)TIMER_FREQUENCY)) {
            (void)ethernet_poll();
            if (icmp_echo_wait_done()) {
                uint32_t rtt = (uint32_t)(timer_get_ticks() - sent_tick);
                console_puts("Reply from ");
                console_puts(ip_str);
                console_puts(" time=");
                print_u32(rtt);
                console_puts(" ticks\n");
                ok++;
                break;
            }
        }
        if (!icmp_echo_wait_done()) {
            console_puts("Request timed out\n");
        }
    }
    console_puts("ping: ");
    print_u32(ok);
    console_puts("/");
    print_u32(count);
    console_puts(" replies\n");
}

static int shell_parse_u16(const char **text_inout, uint16_t *out)
{
    const char *text;
    uint32_t value;

    if (text_inout == NULL || *text_inout == NULL || out == NULL) {
        return 0;
    }

    text = skip_spaces(*text_inout);
    if (*text < '0' || *text > '9') {
        return 0;
    }

    value = 0;
    while (*text >= '0' && *text <= '9') {
        value = (value * 10U) + (uint32_t)(*text - '0');
        if (value > 65535U) {
            return 0;
        }
        text++;
    }

    if (value == 0U) {
        return 0;
    }

    *out = (uint16_t)value;
    *text_inout = text;
    return 1;
}

static int shell_parse_ipv4_prefix(const char **text_inout, ipv4_addr_t *out)
{
    uint32_t octets[4];
    uint32_t i;
    uint32_t value;
    const char *text;

    if (text_inout == NULL || *text_inout == NULL || out == NULL) {
        return 0;
    }

    text = skip_spaces(*text_inout);

    for (i = 0; i < 4U; i++) {
        if (*text < '0' || *text > '9') {
            return 0;
        }

        value = 0;
        while (*text >= '0' && *text <= '9') {
            value = (value * 10U) + (uint32_t)(*text - '0');
            if (value > 255U) {
                return 0;
            }
            text++;
        }

        octets[i] = value;

        if (i < 3U) {
            if (*text != '.') {
                return 0;
            }
            text++;
        }
    }

    *out = IPV4_ADDR(octets[0], octets[1], octets[2], octets[3]);
    *text_inout = text;
    return 1;
}

static uint32_t shell_strlen(const char *s)
{
    uint32_t n = 0;
    if (s == NULL) {
        return 0;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void shell_udp(const char *args)
{
    ipv4_addr_t dst;
    uint16_t port;
    const char *message;
    uint32_t message_len;
    const uint16_t src_port = 50000U;

    args = skip_spaces(args);
    if (*args == '\0') {
        console_puts("usage: udp <ip> <port> <message>\n");
        return;
    }

    if (!shell_parse_ipv4_prefix(&args, &dst)) {
        console_puts("udp: invalid IPv4 address\n");
        return;
    }

    if (!shell_parse_u16(&args, &port)) {
        console_puts("udp: invalid port\n");
        return;
    }

    message = skip_spaces(args);
    if (*message == '\0') {
        console_puts("usage: udp <ip> <port> <message>\n");
        return;
    }

    message_len = shell_strlen(message);
    if (message_len > 512U) {
        message_len = 512U;
    }

    if (udp_send(dst, port, src_port, message, (uint16_t)message_len)) {
        console_puts("udp: sent\n");
    } else {
        console_puts("udp: failed\n");
    }
}

static int shell_is_exec_path(const char *line)
{
    return line[0] == '.' && line[1] == '/';
}

static void run_line(char *line)
{
    while (*line == ' ') line++;
    if (*line == 0) return;

    if (command_is(line, "help")) { shell_help(); return; }
    if (command_is(line, "version")) { shell_version(); return; }
    if (command_is(line, "uptime")) { shell_uptime(); return; }
    if (command_is(line, "clear")) { fb_console_clear(); return; }
    if (command_is(line, "echo")) { shell_echo(line + 4); return; }
    if (command_is(line, "mem")) { shell_mem(); return; }
    if (command_is(line, "heap")) { shell_heap(); return; }
    if (command_is(line, "pci")) { shell_pci(); return; }
    if (command_is(line, "ls")) { shell_ls(line + 2); return; }
    if (command_is(line, "cd")) { shell_cd(line + 2); return; }
    if (command_is(line, "pwd")) { shell_pwd(); return; }
    if (command_is(line, "mkdir")) { shell_mkdir(line + 5); return; }
    if (command_is(line, "rm")) { shell_rm(line + 2); return; }
    if (command_is(line, "touch")) { shell_touch(line + 5); return; }
    if (command_is(line, "cp")) { shell_cp(line + 2); return; }
    if (command_is(line, "mv")) { shell_mv(line + 2); return; }
    if (command_is(line, "write")) { shell_write(line + 5); return; }
    if (command_is(line, "cat")) { shell_cat(line + 3); return; }
    if (command_is(line, "ps")) { shell_ps(); return; }
    if (command_is(line, "kill")) { shell_kill(line + 4); return; }
    if (command_is(line, "netrx")) { shell_netrx(); return; }
    if (command_is(line, "ifconfig")) { shell_ifconfig(line + 8); return; }
    if (command_is(line, "ip")) { shell_ifconfig(line + 2); return; }
    if (command_is(line, "route")) { shell_route(line + 5); return; }
    if (command_is(line, "arp")) { shell_arp(line + 3); return; }
    if (command_is(line, "dhcp")) { shell_dhcp(line + 4); return; }
    if (command_is(line, "dns")) { shell_dns(line + 3); return; }
    if (command_is(line, "nslookup")) { shell_dns(line + 8); return; }
    if (command_is(line, "netstat")) { shell_netstat(line + 7); return; }
    if (command_is(line, "wget")) { shell_wget(line + 4); return; }
    if (command_is(line, "ping")) { shell_ping(line + 4); return; }
    if (command_is(line, "udp")) { shell_udp(line + 3); return; }
    if (command_is(line, "fsck")) { shell_fsck(); return; }
    if (command_is(line, "sleep")) { shell_sleep(line + 5); return; }
    if (command_is(line, "calc")) { shell_calc(line + 4); return; }
    if (command_is(line, "exec")) { shell_exec(line); return; }
    if (command_is(line, "panic")) { panic("initiated by shell"); }
    if (command_is(line, "reboot")) { system_reboot(); }
    if (command_is(line, "halt")) {
        console_puts("halting\n");
        irq_disable();
        for (;;) __asm__ volatile("hlt");
    }
    if (shell_is_exec_path(line)) { shell_exec(line); return; }

    console_puts("Unknown command. Type 'help' for a list of commands.\n");
}

static void print_prompt(void)
{
    console_puts(cwd);
    console_puts("# ");
}

void shell_run(void)
{
    char line[LINE_MAX];
    uint32_t len = 0;

    print_prompt();
    for (;;) {
        char c = console_getchar();
        if (c == '\n') {
            console_putc('\n');
            line[len] = 0;
            run_line(line);
            len = 0;
            print_prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
        } else if (len + 1 < LINE_MAX) {
            line[len++] = c;
            console_putc(c);
        }
    }
}
