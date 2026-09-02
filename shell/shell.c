#include "shell.h"
#include "keyboard.h"
#include "exec.h"
#include "heap.h"
#include "memory.h"
#include "memory_map.h"
#include "mouse.h"
#include "e1000.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "udp.h"
#include "paging.h"
#include "pmm.h"
#include "port_io.h"
#include "fs.h"
#include "process.h"
#include "terminal.h"
#include "timer.h"
#include "version.h"
#include "log.h"

#define LINE_BUFFER_SIZE 256
#define KERNEL_LOAD_ADDRESS 0x00100000

#define SHELL_VGA_WIDTH      80
#define SHELL_MODE_WIDTH     5
#define SHELL_PROMPT_MAX     48
#define SHELL_INPUT_MAX      (SHELL_VGA_WIDTH - 2 - SHELL_MODE_WIDTH)

static char shell_prompt[SHELL_PROMPT_MAX];

extern char _kernel_end;
extern char stack_top;

static int command_is(const char *line, const char *command)
{
    while (*command != '\0')
    {
        if (*line != *command)
        {
            return 0;
        }

        line++;
        command++;
    }

    return *line == '\0' || *line == ' ';
}

static const char *skip_spaces(const char *string)
{
    while (*string == ' ')
    {
        string++;
    }

    return string;
}

static uint32_t string_length(const char *string)
{
    uint32_t length = 0;

    while (string[length] != '\0')
    {
        length++;
    }

    return length;
}

static int shell_input_capacity(void)
{
    int prompt_length = (int)string_length(shell_prompt);
    int capacity = SHELL_VGA_WIDTH - prompt_length - SHELL_MODE_WIDTH;

    if (capacity < 1)
    {
        capacity = 1;
    }

    if (capacity > SHELL_INPUT_MAX)
    {
        capacity = SHELL_INPUT_MAX;
    }

    return capacity;
}

static void uint_to_string(uint32_t value, char *buffer)
{
    char temp[11];
    int index = 0;
    int output = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    while (value > 0)
    {
        temp[index++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (index > 0)
    {
        buffer[output++] = temp[--index];
    }

    buffer[output] = '\0';
}

static void uint_to_hex(uint32_t value, char *buffer)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    int index;

    buffer[0] = '0';
    buffer[1] = 'x';

    for (index = 0; index < 8; index++)
    {
        buffer[2 + index] =
            hex_digits[(value >> ((7 - index) * 4)) & 0x0F];
    }

    buffer[10] = '\0';
}

static void system_reboot(void)
{
    uint8_t status;

    do
    {
        status = inb(0x64);
    } while (status & 0x02);

    outb(0x64, 0xFE);

    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}

static void shell_echo(const char *args);
static void shell_ls(const char *args);
static void shell_cat(const char *args);
static void shell_cd(const char *args);
static void shell_pwd(void);
static void shell_mkdir(const char *args);
static void shell_rm(const char *args);
static void shell_touch(const char *args);
static void shell_write(const char *args);
static void shell_kill(const char *args);
static void shell_sleep(const char *args);
static void shell_exec(const char *line);
static void shell_ps(void);

static void shell_update_prompt(void)
{
    const char *cwd = fs_get_cwd();
    uint32_t out = 0;
    uint32_t cwd_index = 0;

    shell_prompt[out++] = ' ';

    while (cwd[cwd_index] != '\0' && out + 2 < SHELL_PROMPT_MAX)
    {
        shell_prompt[out++] = cwd[cwd_index++];
    }

    shell_prompt[out++] = '>';
    shell_prompt[out++] = ' ';
    shell_prompt[out] = '\0';
}

static uint32_t shell_parse_uint(const char *string)
{
    uint32_t value = 0;

    string = skip_spaces(string);

    while (*string >= '0' && *string <= '9')
    {
        value = (value * 10U) + (uint32_t)(*string - '0');
        string++;
    }

    return value;
}

static int shell_is_exec_path(const char *line)
{
    return line[0] == '/' || (line[0] == '.' && line[1] == '/');
}

static void shell_help(void)
{
    terminal_write("Available commands:\n");
    terminal_write("  help     - show this message\n");
    terminal_write("  version  - show TestOS version\n");
    terminal_write("  uptime   - show system uptime\n");
    terminal_write("  clear    - clear the screen\n");
    terminal_write("  echo     - print text\n");
    terminal_write("  mem      - show memory information\n");
    terminal_write("  ls       - list directory\n");
    terminal_write("  cd       - change directory\n");
    terminal_write("  pwd      - print working directory\n");
    terminal_write("  cat      - print a file\n");
    terminal_write("  write    - write text to a file\n");
    terminal_write("  mkdir    - create a directory\n");
    terminal_write("  rm       - remove a file or empty directory\n");
    terminal_write("  touch    - create an empty file\n");
    terminal_write("  cp       - copy a file\n");
    terminal_write("  mv       - move/rename a file\n");
    terminal_write("  ./file   - execute a program\n");
    terminal_write("  sleep    - block for N timer ticks\n");
    terminal_write("  ps       - list processes\n");
    terminal_write("  kill     - terminate a process\n");
    terminal_write("  mouse    - show mouse position and buttons\n");
    terminal_write("  netrx    - poll E1000 for received Ethernet frames\n");
    terminal_write("  ping     - ICMP echo request to an IPv4 address\n");
    terminal_write("  udp      - send a UDP datagram\n");
    terminal_write("  panic    - halt the kernel\n");
    terminal_write("  reboot   - reboot the system\n");
    terminal_write("  fsck     - check filesystem consistency\n");
    terminal_write("  fstest   - run filesystem stress tests\n");
}

static void shell_version(void)
{
    terminal_write(TESTOS_NAME " " TESTOS_VERSION "\n");
}

static void shell_uptime(void)
{
    char number[11];
    uint32_t seconds = timer_get_uptime_seconds();
    uint32_t ticks = timer_get_ticks();

    terminal_write("Uptime: ");
    uint_to_string(seconds, number);
    terminal_write(number);
    terminal_write(" seconds (");
    uint_to_string(ticks, number);
    terminal_write(number);
    terminal_write(" ticks)\n");
}

static void shell_print_padded_u32(uint32_t value, int width)
{
    char number[11];
    int length;
    int pad;

    uint_to_string(value, number);
    length = (int)string_length(number);

    for (pad = width - length; pad > 0; pad--)
    {
        terminal_putchar(' ');
    }

    terminal_write(number);
}

static void shell_mem_summary(void)
{
    uint32_t free_frames = pmm_get_free_frames();
    uint32_t total_frames = pmm_get_total_frames();
    uint32_t used_frames = total_frames - free_frames;
    uint32_t total_mb = (total_frames * PAGE_SIZE) / (1024U * 1024U);
    uint32_t used_mb = (used_frames * PAGE_SIZE) / (1024U * 1024U);
    uint32_t free_mb = (free_frames * PAGE_SIZE) / (1024U * 1024U);
    uint32_t heap_total_kb = heap_get_total_bytes() / 1024U;
    uint32_t heap_used_kb = heap_get_used_bytes() / 1024U;
    uint32_t heap_free_kb = heap_get_free_bytes() / 1024U;

    terminal_write("MEMORY SUMMARY\n\n");

    terminal_write("Physical:\n");
    terminal_write("  Total:  ");
    shell_print_padded_u32(total_mb, 6);
    terminal_write(" MB\n");
    terminal_write("  Used:   ");
    shell_print_padded_u32(used_mb, 6);
    terminal_write(" MB\n");
    terminal_write("  Free:   ");
    shell_print_padded_u32(free_mb, 6);
    terminal_write(" MB\n\n");

    terminal_write("Frames:\n");
    terminal_write("  Total:  ");
    shell_print_padded_u32(total_frames, 6);
    terminal_write("\n");
    terminal_write("  Used:   ");
    shell_print_padded_u32(used_frames, 6);
    terminal_write("\n");
    terminal_write("  Free:   ");
    shell_print_padded_u32(free_frames, 6);
    terminal_write("\n\n");

    terminal_write("Heap:\n");
    terminal_write("  Total:  ");
    shell_print_padded_u32(heap_total_kb, 6);
    terminal_write(" KB\n");
    terminal_write("  Used:   ");
    shell_print_padded_u32(heap_used_kb, 6);
    terminal_write(" KB\n");
    terminal_write("  Free:   ");
    shell_print_padded_u32(heap_free_kb, 6);
    terminal_write(" KB\n");
}

static void shell_mem(void)
{
    char hex[11];
    char number[11];
    uint32_t index;
    uint32_t kernel_end = (uint32_t)&_kernel_end;
    uint32_t kernel_size = kernel_end - KERNEL_LOAD_ADDRESS;
    uint32_t stack_top_address = (uint32_t)&stack_top;
    uint32_t free_frames = pmm_get_free_frames();
    uint32_t total_frames = pmm_get_total_frames();
    uint32_t frame_bytes = free_frames * PAGE_SIZE;
    uint32_t total_bytes = total_frames * PAGE_SIZE;

    shell_mem_summary();

    terminal_write("\nMemory details:\n");
    terminal_write("  Kernel load:   ");
    uint_to_hex(KERNEL_LOAD_ADDRESS, hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("  Kernel end:    ");
    uint_to_hex(kernel_end, hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("  Kernel size:   ");
    uint_to_string(kernel_size, number);
    terminal_write(number);
    terminal_write(" bytes\n");

    terminal_write("  Stack top:     ");
    uint_to_hex(stack_top_address, hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("\nMemory map (");
    uint_to_string(memory_map_get_region_count(), number);
    terminal_write(number);
    terminal_write(" regions):\n");

    for (index = 0; index < memory_map_get_region_count(); index++)
    {
        const memory_region_t *region = memory_map_get_region(index);

        terminal_write("  ");
        uint_to_hex((uint32_t)region->base, hex);
        terminal_write(hex);
        terminal_write("  size ");
        uint_to_string((uint32_t)region->length, number);
        terminal_write(number);
        terminal_write("  type ");
        uint_to_string(region->type, number);
        terminal_write(number);
        terminal_write("\n");
    }

    terminal_write("\nPhysical memory:\n");
    terminal_write("  Total frames:  ");
    uint_to_string(total_frames, number);
    terminal_write(number);
    terminal_write(" (");
    uint_to_string(total_bytes / (1024U * 1024U), number);
    terminal_write(number);
    terminal_write(" MiB)\n");

    terminal_write("  Free frames:   ");
    uint_to_string(free_frames, number);
    terminal_write(number);
    terminal_write(" (");
    uint_to_string(frame_bytes / 1024U, number);
    terminal_write(number);
    terminal_write(" KiB)\n");

    terminal_write("  Bitmap:        ");
    uint_to_hex(pmm_get_bitmap_address(), hex);
    terminal_write(hex);
    terminal_write(" (");
    uint_to_string(pmm_get_bitmap_size(), number);
    terminal_write(number);
    terminal_write(" bytes)\n");

    terminal_write("\nPaging:\n");
    terminal_write("  Enabled:       ");
    terminal_write(paging_is_enabled() ? "yes" : "no");
    terminal_write("\n");

    terminal_write("  Directory:     ");
    uint_to_hex(paging_get_directory_address(), hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("  Identity map:  ");
    uint_to_string(paging_get_mapped_bytes() / (1024U * 1024U), number);
    terminal_write(number);
    terminal_write(" MiB\n");

    terminal_write("\nKernel heap:\n");
    terminal_write("  Start:         ");
    uint_to_hex(heap_get_start(), hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("  End:           ");
    uint_to_hex(heap_get_end(), hex);
    terminal_write(hex);
    terminal_write("\n");

    terminal_write("  Total:         ");
    uint_to_string(heap_get_total_bytes(), number);
    terminal_write(number);
    terminal_write(" bytes\n");

    terminal_write("  Used:          ");
    uint_to_string(heap_get_used_bytes(), number);
    terminal_write(number);
    terminal_write(" bytes\n");

    terminal_write("  Free:          ");
    uint_to_string(heap_get_free_bytes(), number);
    terminal_write(number);
    terminal_write(" bytes\n");
}

static void shell_echo(const char *args)
{
    args = skip_spaces(args);

    if (*args == '\0')
    {
        return;
    }

    terminal_write(args);
    terminal_putchar('\n');
}

static void shell_ls(const char *args)
{
    args = skip_spaces(args);

    if (*args == '\0')
    {
        fs_list_directory(fs_get_cwd());
        return;
    }

    fs_list_directory(args);
}

static void shell_cd(const char *args)
{
    char resolved[FS_MAX_PATH];

    args = skip_spaces(args);

    if (*args == '\0')
    {
        fs_set_cwd("/");
        shell_update_prompt();
        return;
    }

    if (!fs_resolve_path(args, resolved))
    {
        terminal_write("cd: invalid path\n");
        return;
    }

    if (!fs_is_directory(resolved))
    {
        terminal_write("cd: not a directory\n");
        return;
    }

    fs_set_cwd(resolved);
    shell_update_prompt();
}

static void shell_pwd(void)
{
    terminal_write(fs_get_cwd());
    terminal_putchar('\n');
}

static void shell_mkdir(const char *args)
{
    args = skip_spaces(args);

    if (*args == '\0')
    {
        terminal_write("Usage: mkdir <directory>\n");
        return;
    }

    if (!fs_mkdir(args))
    {
        terminal_write("mkdir: failed\n");
    }
}

static void shell_rm(const char *args)
{
    args = skip_spaces(args);

    if (*args == '\0')
    {
        terminal_write("Usage: rm <path>\n");
        return;
    }

    if (!fs_remove(args))
    {
        terminal_write("rm: failed\n");
    }
}

static void shell_touch(const char *args)
{
    args = skip_spaces(args);

    if (*args == '\0')
    {
        terminal_write("Usage: touch <file>\n");
        return;
    }

    if (!fs_touch(args))
    {
        terminal_write("touch: failed\n");
    }
}

static int shell_split_two_paths(
    const char *args,
    char *first,
    uint32_t first_size,
    const char **second_out
)
{
    uint32_t length = 0;

    args = skip_spaces(args);

    if (*args == '\0')
    {
        return 0;
    }

    while (args[length] != '\0' && args[length] != ' ')
    {
        if (length + 1U >= first_size)
        {
            return 0;
        }

        first[length] = args[length];
        length++;
    }

    first[length] = '\0';
    *second_out = skip_spaces(args + length);

    return **second_out != '\0';
}

static void shell_cp(const char *args)
{
    char src[FS_MAX_PATH];
    const char *dst;

    if (!shell_split_two_paths(args, src, sizeof(src), &dst))
    {
        terminal_write("Usage: cp <src> <dst>\n");
        return;
    }

    if (!fs_copy(src, dst))
    {
        terminal_write("cp: failed\n");
    }
}

static void shell_mv(const char *args)
{
    char src[FS_MAX_PATH];
    const char *dst;

    if (!shell_split_two_paths(args, src, sizeof(src), &dst))
    {
        terminal_write("Usage: mv <src> <dst>\n");
        return;
    }

    if (!fs_move(src, dst))
    {
        terminal_write("mv: failed\n");
    }
}

static void shell_write(const char *args)
{
    char path[FS_MAX_PATH];
    const char *text;
    uint32_t path_length = 0;
    uint32_t text_length;

    args = skip_spaces(args);

    if (*args == '\0')
    {
        terminal_write("Usage: write <file> <text>\n");
        return;
    }

    while (args[path_length] != '\0' &&
           args[path_length] != ' ' &&
           path_length + 1 < FS_MAX_PATH)
    {
        path[path_length] = args[path_length];
        path_length++;
    }

    path[path_length] = '\0';
    text = skip_spaces(args + path_length);

    if (*text == '\0')
    {
        terminal_write("Usage: write <file> <text>\n");
        return;
    }

    text_length = string_length(text);

    if (text_length > FS_MAX_FILE_SIZE)
    {
        terminal_write("write: text too long\n");
        return;
    }

    if (!fs_write(path, text, text_length))
    {
        terminal_write("write: failed\n");
    }
}

static void shell_kill(const char *args)
{
    const char *text = skip_spaces(args);
    uint32_t pid;

    if (*text == '\0')
    {
        terminal_write("Usage: kill <pid>\n");
        return;
    }

    pid = shell_parse_uint(text);

    if (pid == 0)
    {
        terminal_write("kill: cannot kill system process\n");
        return;
    }

    if (process_terminate(pid) != 0)
    {
        terminal_write("kill: failed (missing or system process)\n");
    }
}

static int shell_tokenize(char *line, char *argv[], int max_argv)
{
    int argc = 0;
    char *current = line;

    while (*current != '\0' && argc < max_argv)
    {
        while (*current == ' ')
        {
            current++;
        }

        if (*current == '\0')
        {
            break;
        }

        argv[argc++] = current;

        while (*current != '\0' && *current != ' ')
        {
            current++;
        }

        if (*current == '\0')
        {
            break;
        }

        *current = '\0';
        current++;
    }

    return argc;
}

static void shell_sleep(const char *args)
{
    uint32_t ticks = shell_parse_uint(args);

    if (ticks == 0)
    {
        terminal_write("Usage: sleep <ticks>\n");
        return;
    }

    process_sleep(ticks);
}

static void shell_exec(const char *line)
{
    char mutable_line[LINE_BUFFER_SIZE];
    char *argv[8];
    char resolved[FS_MAX_PATH];
    int argc;
    int index;
    int pid;
    process_t *child;

    for (index = 0; line[index] != '\0' && index < LINE_BUFFER_SIZE - 1; index++)
    {
        mutable_line[index] = line[index];
    }

    mutable_line[index] = '\0';
    argc = shell_tokenize(mutable_line, argv, 8);

    if (argc == 0)
    {
        terminal_write("Exec failed.\n");
        return;
    }

    if (!fs_resolve_path(argv[0], resolved))
    {
        terminal_write("Exec failed.\n");
        return;
    }

    pid = process_exec(
        resolved,
        process_get_current_pid(),
        argc,
        (const char **)argv
    );

    if (pid < 0)
    {
        terminal_write("Exec failed.\n");
        return;
    }

    /* Run the program in the foreground, then restore a fresh prompt line. */
    for (;;)
    {
        child = process_find_by_pid((uint32_t)pid);

        if (child == NULL || child->state == PROCESS_TERMINATED)
        {
            break;
        }

        scheduler_yield();
    }

    process_reap_terminated();
}

static void shell_cat(const char *args)
{
    char *buffer;
    uint32_t bytes_read = 0;

    args = skip_spaces(args);

    if (*args == '\0')
    {
        terminal_write("Usage: cat <file>\n");
        return;
    }

    buffer = (char *)kmalloc(FS_MAX_FILE_SIZE);

    if (buffer == NULL)
    {
        terminal_write("Out of memory.\n");
        return;
    }

    if (!fs_read(args, buffer, FS_MAX_FILE_SIZE, &bytes_read))
    {
        kfree(buffer);
        terminal_write("File not found.\n");
        return;
    }

    terminal_write(buffer);
    terminal_putchar('\n');
    kfree(buffer);
}

static void shell_ps(void)
{
    process_list();
}

static void int_to_string(int value, char *buffer)
{
    uint32_t abs_value;
    int out = 0;

    if (value < 0)
    {
        buffer[out++] = '-';
        abs_value = (uint32_t)(-value);
    }
    else
    {
        abs_value = (uint32_t)value;
    }

    uint_to_string(abs_value, buffer + out);
}

static void shell_mouse(void)
{
    mouse_state_t state;
    char number[12];

    mouse_get_state(&state);

    terminal_write("Mouse: x=");
    int_to_string(state.x, number);
    terminal_write(number);
    terminal_write(" y=");
    int_to_string(state.y, number);
    terminal_write(number);
    terminal_write(" left=");
    terminal_write(state.left ? "1" : "0");
    terminal_write(" right=");
    terminal_write(state.right ? "1" : "0");
    terminal_write(" middle=");
    terminal_write(state.middle ? "1" : "0");
    terminal_write("\n");
}

static void shell_netrx(void)
{
    int frames;
    char number[12];

    frames = ethernet_poll();
    if (frames == 0)
    {
        terminal_write("netrx: no frames\n");
        return;
    }

    terminal_write("netrx: frames=");
    int_to_string(frames, number);
    terminal_write(number);
    terminal_write("\n");
}

static int shell_parse_ipv4(const char *text, ipv4_addr_t *out)
{
    uint32_t octets[4];
    uint32_t i;
    uint32_t value;

    if (text == NULL || out == NULL)
    {
        return 0;
    }

    text = skip_spaces(text);

    for (i = 0; i < 4U; i++)
    {
        if (*text < '0' || *text > '9')
        {
            return 0;
        }

        value = 0;
        while (*text >= '0' && *text <= '9')
        {
            value = (value * 10U) + (uint32_t)(*text - '0');
            if (value > 255U)
            {
                return 0;
            }
            text++;
        }

        octets[i] = value;

        if (i < 3U)
        {
            if (*text != '.')
            {
                return 0;
            }
            text++;
        }
    }

    text = skip_spaces(text);
    if (*text != '\0')
    {
        return 0;
    }

    *out = IPV4_ADDR(octets[0], octets[1], octets[2], octets[3]);
    return 1;
}

static void shell_ping(const char *args)
{
    ipv4_addr_t dst;
    char ip_str[16];
    uint32_t start;
    static uint16_t ping_seq = 1U;
    const uint16_t ping_id = 0x544FU;
    uint16_t seq;

    args = skip_spaces(args);
    if (*args == '\0')
    {
        terminal_write("usage: ping <ip>\n");
        return;
    }

    if (!shell_parse_ipv4(args, &dst))
    {
        terminal_write("ping: invalid IPv4 address\n");
        return;
    }

    seq = ping_seq++;
    if (ping_seq == 0U)
    {
        ping_seq = 1U;
    }

    ipv4_addr_format(dst, ip_str);
    terminal_write("PING ");
    terminal_write(ip_str);
    terminal_write("\n");

    icmp_arm_echo_wait(dst, ping_id, seq);
    if (!icmp_send_echo_request(dst, ping_id, seq))
    {
        /* ARP may still be resolving; keep waiting for a reply. */
    }

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < (3U * TIMER_FREQUENCY))
    {
        (void)ethernet_poll();
        if (icmp_echo_wait_done())
        {
            terminal_write("Reply from ");
            terminal_write(ip_str);
            terminal_write("\n");
            return;
        }
    }

    terminal_write("Request timed out\n");
}

static int shell_parse_u16(const char **text_inout, uint16_t *out)
{
    const char *text;
    uint32_t value;

    if (text_inout == NULL || *text_inout == NULL || out == NULL)
    {
        return 0;
    }

    text = skip_spaces(*text_inout);
    if (*text < '0' || *text > '9')
    {
        return 0;
    }

    value = 0;
    while (*text >= '0' && *text <= '9')
    {
        value = (value * 10U) + (uint32_t)(*text - '0');
        if (value > 65535U)
        {
            return 0;
        }
        text++;
    }

    if (value == 0U)
    {
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

    if (text_inout == NULL || *text_inout == NULL || out == NULL)
    {
        return 0;
    }

    text = skip_spaces(*text_inout);

    for (i = 0; i < 4U; i++)
    {
        if (*text < '0' || *text > '9')
        {
            return 0;
        }

        value = 0;
        while (*text >= '0' && *text <= '9')
        {
            value = (value * 10U) + (uint32_t)(*text - '0');
            if (value > 255U)
            {
                return 0;
            }
            text++;
        }

        octets[i] = value;

        if (i < 3U)
        {
            if (*text != '.')
            {
                return 0;
            }
            text++;
        }
    }

    *out = IPV4_ADDR(octets[0], octets[1], octets[2], octets[3]);
    *text_inout = text;
    return 1;
}

static void shell_udp(const char *args)
{
    ipv4_addr_t dst;
    uint16_t port;
    const char *message;
    uint32_t message_len;
    const uint16_t src_port = 50000U;

    args = skip_spaces(args);
    if (*args == '\0')
    {
        terminal_write("usage: udp <ip> <port> <message>\n");
        return;
    }

    if (!shell_parse_ipv4_prefix(&args, &dst))
    {
        terminal_write("udp: invalid IPv4 address\n");
        return;
    }

    if (!shell_parse_u16(&args, &port))
    {
        terminal_write("udp: invalid port\n");
        return;
    }

    message = skip_spaces(args);
    if (*message == '\0')
    {
        terminal_write("usage: udp <ip> <port> <message>\n");
        return;
    }

    message_len = string_length(message);
    if (message_len > 512U)
    {
        message_len = 512U;
    }

    if (udp_send(dst, port, src_port, message, (uint16_t)message_len))
    {
        terminal_write("udp: sent\n");
    }
    else
    {
        terminal_write("udp: failed\n");
    }
}

static void shell_fsck(void)
{
    if (fs_check() == 0)
    {
        terminal_write("fsck: OK\n");
    }
    else
    {
        terminal_write("fsck: FAILED\n");
    }
}

static void shell_fstest(void)
{
    if (fs_selftest() == 0)
    {
        terminal_write("fstest: PASSED\n");
    }
    else
    {
        terminal_write("fstest: FAILED\n");
    }
}

static void shell_panic(void)
{
    panic("SHELL", "initiated by shell");
}

static void shell_render_line(char *line, int length, int cursor_pos)
{
    int index;
    int prompt_length = (int)string_length(shell_prompt);
    int input_max = shell_input_capacity();
    int mode_column = SHELL_VGA_WIDTH - SHELL_MODE_WIDTH;
    int cursor_column;

    if (length > input_max)
    {
        length = input_max;
    }

    if (cursor_pos > length)
    {
        cursor_pos = length;
    }

    cursor_column = prompt_length + cursor_pos;

    if (cursor_column >= mode_column)
    {
        cursor_column = mode_column - 1;
    }

    terminal_putchar('\r');
    terminal_write(shell_prompt);

    for (index = 0; index < length; index++)
    {
        terminal_putchar(line[index]);
    }

    /* Clear the rest of the input region up to the mode marker. */
    for (index = prompt_length + length; index < mode_column; index++)
    {
        terminal_putchar(' ');
    }

    terminal_set_column((uint8_t)mode_column);

    if (keyboard_is_insert_mode())
    {
        terminal_write("[INS]");
    }
    else
    {
        terminal_write("[OVR]");
    }

    terminal_set_column((uint8_t)cursor_column);
}

static void shell_insert_char(char *line, int *length, int *cursor_pos, char c)
{
    int index;
    int input_max = shell_input_capacity();

    if (*length >= input_max)
    {
        return;
    }

    if (keyboard_is_insert_mode())
    {
        for (index = *length; index > *cursor_pos; index--)
        {
            line[index] = line[index - 1];
        }

        line[*cursor_pos] = c;
        (*length)++;
        line[*length] = '\0';
        (*cursor_pos)++;
        shell_render_line(line, *length, *cursor_pos);
        return;
    }

    if (*cursor_pos < *length)
    {
        line[*cursor_pos] = c;
    }
    else
    {
        line[*cursor_pos] = c;
        (*length)++;
        line[*length] = '\0';
    }

    (*cursor_pos)++;
    shell_render_line(line, *length, *cursor_pos);
}

static void shell_backspace_char(char *line, int *length, int *cursor_pos)
{
    int index;

    if (*cursor_pos == 0)
    {
        return;
    }

    (*cursor_pos)--;

    for (index = *cursor_pos; index < *length - 1; index++)
    {
        line[index] = line[index + 1];
    }

    (*length)--;
    line[*length] = '\0';
    shell_render_line(line, *length, *cursor_pos);
}

static void shell_delete_char(char *line, int *length, int *cursor_pos)
{
    int index;

    if (*cursor_pos >= *length)
    {
        return;
    }

    for (index = *cursor_pos; index < *length - 1; index++)
    {
        line[index] = line[index + 1];
    }

    (*length)--;
    line[*length] = '\0';
    shell_render_line(line, *length, *cursor_pos);
}

static void shell_execute(const char *line)
{
    const char *args;

    line = skip_spaces(line);

    if (*line == '\0')
    {
        return;
    }

    if (command_is(line, "help"))
    {
        shell_help();
        return;
    }

    if (command_is(line, "version"))
    {
        shell_version();
        return;
    }

    if (command_is(line, "uptime"))
    {
        shell_uptime();
        return;
    }

    if (command_is(line, "clear"))
    {
        terminal_clear();
        return;
    }

    if (command_is(line, "mem"))
    {
        shell_mem();
        return;
    }

    if (command_is(line, "ls"))
    {
        shell_ls(line + 2);
        return;
    }

    if (command_is(line, "cd"))
    {
        shell_cd(line + 2);
        return;
    }

    if (command_is(line, "pwd"))
    {
        shell_pwd();
        return;
    }

    if (command_is(line, "mkdir"))
    {
        shell_mkdir(line + 5);
        return;
    }

    if (command_is(line, "rm"))
    {
        shell_rm(line + 2);
        return;
    }

    if (command_is(line, "touch"))
    {
        shell_touch(line + 5);
        return;
    }

    if (command_is(line, "cp"))
    {
        shell_cp(line + 2);
        return;
    }

    if (command_is(line, "mv"))
    {
        shell_mv(line + 2);
        return;
    }

    if (command_is(line, "write"))
    {
        shell_write(line + 5);
        return;
    }

    if (command_is(line, "kill"))
    {
        shell_kill(line + 4);
        return;
    }

    if (command_is(line, "cat"))
    {
        args = line + 3;
        shell_cat(args);
        return;
    }

    if (command_is(line, "ps"))
    {
        shell_ps();
        return;
    }

    if (command_is(line, "mouse"))
    {
        shell_mouse();
        return;
    }

    if (command_is(line, "netrx"))
    {
        shell_netrx();
        return;
    }

    if (command_is(line, "ping"))
    {
        shell_ping(line + 4);
        return;
    }

    if (command_is(line, "udp"))
    {
        shell_udp(line + 3);
        return;
    }

    if (command_is(line, "fsck"))
    {
        shell_fsck();
        return;
    }

    if (command_is(line, "fstest"))
    {
        shell_fstest();
        return;
    }

    if (command_is(line, "sleep"))
    {
        shell_sleep(line + 5);
        return;
    }

    if (command_is(line, "panic"))
    {
        shell_panic();
    }

    if (command_is(line, "reboot"))
    {
        system_reboot();
    }

    if (command_is(line, "echo"))
    {
        args = line + 4;
        shell_echo(args);
        return;
    }

    if (shell_is_exec_path(line))
    {
        shell_exec(line);
        return;
    }

    terminal_write("Unknown command. Type 'help' for a list of commands.\n");
}

static void shell_ensure_at_bottom(char *line, int length, int cursor_pos)
{
    if (!terminal_is_at_bottom())
    {
        terminal_scroll_to_bottom();
        shell_render_line(line, length, cursor_pos);
    }
}

void shell_run(void)
{
    char line[LINE_BUFFER_SIZE];
    int length = 0;
    int cursor_pos = 0;
    keyboard_event_t event;

    line[0] = '\0';
    shell_update_prompt();
    shell_render_line(line, 0, 0);

    for (;;)
    {
        keyboard_poll();

        if (!keyboard_has_event())
        {
            (void)ethernet_poll();
            __asm__ volatile ("hlt");
            continue;
        }

        event = keyboard_read_event();

        switch (event.type)
        {
            case KEYBOARD_EVENT_INSERT:
                shell_ensure_at_bottom(line, length, cursor_pos);
                shell_render_line(line, length, cursor_pos);
                break;

            case KEYBOARD_EVENT_LEFT:
                shell_ensure_at_bottom(line, length, cursor_pos);
                if (cursor_pos > 0)
                {
                    cursor_pos--;
                    shell_render_line(line, length, cursor_pos);
                }
                break;

            case KEYBOARD_EVENT_RIGHT:
                shell_ensure_at_bottom(line, length, cursor_pos);
                if (cursor_pos < length)
                {
                    cursor_pos++;
                    shell_render_line(line, length, cursor_pos);
                }
                break;

            case KEYBOARD_EVENT_UP:
                terminal_scroll_up();
                break;

            case KEYBOARD_EVENT_DOWN:
                terminal_scroll_down();
                if (terminal_is_at_bottom())
                {
                    shell_render_line(line, length, cursor_pos);
                }
                break;

            case KEYBOARD_EVENT_DELETE:
                shell_ensure_at_bottom(line, length, cursor_pos);
                shell_delete_char(line, &length, &cursor_pos);
                break;

            case KEYBOARD_EVENT_CHAR:
                shell_ensure_at_bottom(line, length, cursor_pos);

                if (event.character == '\n')
                {
                    line[length] = '\0';
                    terminal_putchar('\n');
                    shell_execute(line);
                    length = 0;
                    cursor_pos = 0;
                    line[0] = '\0';
                    shell_render_line(line, 0, 0);
                    break;
                }

                if (event.character == '\b')
                {
                    shell_backspace_char(line, &length, &cursor_pos);
                    break;
                }

                if (event.character != 0)
                {
                    shell_insert_char(line, &length, &cursor_pos, event.character);
                }
                break;

            default:
                break;
        }
    }
}
