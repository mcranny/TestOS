#include "limine.h"

/* Delimit requests and retain them in the loaded ELF image. */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = {
    0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL,
    0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = {
    0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL
};

struct display {
    volatile uint8_t *address;
    uint64_t width, height, pitch;
    uint8_t bytes_per_pixel;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};

static struct display screen;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_putc(char c)
{
    uint16_t port = 0x3f8;
    uint32_t spin = 1000000;
    while ((inb(port + 5) & 0x20) == 0 && spin-- != 0) { }
    outb(port, (uint8_t)c);
}

static void serial_init(void)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3); outb(0x3f9, 0);
    outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}

/* Keep hardware interrupts masked while the diagnostic layer owns the CPU. */
static void interrupt_timer_initialize(void)
{
    const uint16_t divisor = 11932; /* 100 Hz PIT channel 0. */
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xff));
    outb(0x40, (uint8_t)(divisor >> 8));
}

static void serial_puts(const char *text)
{
    while (*text != '\0') serial_putc(*text++);
}

static void serial_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) serial_putc(digits[(value >> shift) & 0xfU]);
}

static uint32_t component(uint8_t value, uint8_t size, uint8_t shift)
{
    if (size == 0 || size > 8 || shift + size > 32) return 0;
    return (((uint32_t)value * ((1U << size) - 1U)) / 255U) << shift;
}

static void pixel(uint64_t x, uint64_t y, uint32_t rgb)
{
    uint32_t value;
    volatile uint8_t *dst;
    uint8_t byte;
    if (x >= screen.width || y >= screen.height) return;
    value = component((uint8_t)(rgb >> 16), screen.red_size, screen.red_shift) |
            component((uint8_t)(rgb >> 8), screen.green_size, screen.green_shift) |
            component((uint8_t)rgb, screen.blue_size, screen.blue_shift);
    dst = screen.address + y * screen.pitch + x * screen.bytes_per_pixel;
    for (byte = 0; byte < screen.bytes_per_pixel; byte++) dst[byte] = (uint8_t)(value >> (byte * 8));
}

static uint8_t glyph(char c, uint8_t row)
{
    static const uint8_t font[][7] = {
        {0,0,0,0,0,0,0},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
        {31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
        {31,4,4,4,4,4,31},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},
        {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
        {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
        {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const uint8_t digits[][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,2,12}
    };
    if (c == ' ' || row >= 7) return 0;
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    return (c >= 'A' && c <= 'Z') ? font[c - 'A' + 1][row] : 0;
}

static void draw_text(const char *text, uint64_t x, uint64_t y, uint32_t color)
{
    uint64_t letter;
    for (letter = 0; text[letter]; letter++) for (uint8_t row = 0; row < 7; row++) {
        uint8_t bits = glyph(text[letter], row);
        for (uint8_t col = 0; col < 5; col++) if (bits & (1U << (4 - col)))
            for (uint8_t dy = 0; dy < 8; dy++) for (uint8_t dx = 0; dx < 8; dx++)
                pixel(x + letter * 48 + col * 8 + dx, y + row * 8 + dy, color);
    }
}

static void stage(const char *name)
{
    uint64_t x, y;
    for (y = 0; y < screen.height; y++) for (x = 0; x < screen.width; x++) pixel(x, y, 0x0b1d34);
    draw_text("TESTOS", 32, 32, 0xe0f4ff);
    draw_text(name, 32, 112, 0xffffff);
}

static void console_fatal(const char *code, const char *reason)
{
    serial_puts("FATAL "); serial_puts(code); serial_puts(": "); serial_puts(reason); serial_puts("\r\n");
    if (screen.address != 0) {
        stage("FATAL");
        draw_text(code, 32, 192, 0xffd0d0);
        draw_text(reason, 32, 272, 0xffd0d0);
    }
}

static int valid_framebuffer(struct limine_framebuffer *fb)
{
    uint64_t minimum_pitch, total;
    if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->width == 0 || fb->height == 0 || fb->bpp != 32 || fb->bpp % 8 != 0) return 0;
    if (fb->width > UINT64_MAX / 4 || fb->pitch < fb->width * 4) return 0;
    if (fb->height > UINT64_MAX / fb->pitch) return 0;
    minimum_pitch = fb->width * 4; total = fb->pitch * fb->height;
    if (minimum_pitch == 0 || total == 0 || fb->red_mask_size == 0 || fb->green_mask_size == 0 || fb->blue_mask_size == 0 ||
        fb->red_mask_size + fb->red_mask_shift > 32 || fb->green_mask_size + fb->green_mask_shift > 32 || fb->blue_mask_size + fb->blue_mask_shift > 32) return 0;
    return 1;
}

static int framebuffer_is_reserved(struct limine_framebuffer *fb)
{
    uint64_t physical, size, end, index;
    struct limine_memmap_response *map = memmap_request.response;

    if (map->entry_count == 0 || map->entry_count > 512 || !map->entries ||
        (uint64_t)(uintptr_t)fb->address < hhdm_request.response->offset ||
        fb->height > UINT64_MAX / fb->pitch) return 0;
    physical = (uint64_t)(uintptr_t)fb->address - hhdm_request.response->offset;
    size = fb->pitch * fb->height;
    if (physical > UINT64_MAX - size) return 0;
    end = physical + size;
    for (index = 0; index < map->entry_count; index++) {
        struct limine_memmap_entry *entry = map->entries[index];
        uint64_t entry_end;
        if (!entry || entry->length == 0 || entry->base > UINT64_MAX - entry->length) return 0;
        entry_end = entry->base + entry->length;
        if (entry->type == LIMINE_MEMMAP_FRAMEBUFFER && physical >= entry->base && end <= entry_end) return 1;
    }
    return 0;
}

static uint64_t framebuffer_physical_address(struct limine_framebuffer *fb)
{
    return (uint64_t)(uintptr_t)fb->address - hhdm_request.response->offset;
}

void uefi_main(void)
{
    struct limine_framebuffer *fb;
    serial_init(); serial_puts("BOOT: Kernel entry (x86-64 Limine)\r\n");
    if (!hhdm_request.response) { console_fatal("E001A", "HHDM MISSING"); goto halt; }
    if (!memmap_request.response) { console_fatal("E001B", "MEMORY MAP MISSING"); goto halt; }
    if (!framebuffer_request.response) { console_fatal("E001C", "FRAMEBUFFER MISSING"); goto halt; }
    if (hhdm_request.response->revision > 1 || memmap_request.response->revision > 1 || framebuffer_request.response->revision > 2) { console_fatal("E001D", "RESPONSE REVISION"); goto halt; }
    if (framebuffer_request.response->framebuffer_count == 0 || framebuffer_request.response->framebuffer_count > 16 || !framebuffer_request.response->framebuffers) { console_fatal("E001E", "FRAMEBUFFER RESPONSE"); goto halt; }
    fb = framebuffer_request.response->framebuffers[0];
    if (!valid_framebuffer(fb)) { console_fatal("E002", "FRAMEBUFFER INVALID"); goto halt; }
    screen.address = fb->address; screen.width = fb->width; screen.height = fb->height; screen.pitch = fb->pitch;
    screen.bytes_per_pixel = 4; screen.red_size = fb->red_mask_size; screen.red_shift = fb->red_mask_shift;
    screen.green_size = fb->green_mask_size; screen.green_shift = fb->green_mask_shift;
    screen.blue_size = fb->blue_mask_size; screen.blue_shift = fb->blue_mask_shift;
    if (!framebuffer_is_reserved(fb)) { console_fatal("E003", "MEMORY MAP INVALID"); goto halt; }
    stage("KERNEL ENTRY"); serial_puts("BOOT: Kernel entry stage visible\r\n");
    stage("BOOT INFO VALIDATED"); serial_puts("BOOT: Boot information validated; framebuffer physical="); serial_hex64(framebuffer_physical_address(fb)); serial_puts("\r\n");
    stage("DISPLAY MAPPED"); serial_puts("BOOT: Display mapped and initialized through Limine active HHDM paging\r\n");
    stage("MEMORY MAP ACCEPTED"); serial_puts("BOOT: Memory map accepted; framebuffer range is reserved\r\n");
    stage("DESCRIPTORS PAGING READY"); serial_puts("BOOT: Limine descriptor tables and active paging are ready\r\n");
    interrupt_timer_initialize();
    stage("INTERRUPTS TIMER READY"); serial_puts("BOOT: PIC masked and PIT timer initialized; interrupts remain disabled\r\n");
    stage("TESTOS READY"); serial_puts("BOOT: TestOS ready\r\n");
halt:
    for (;;) __asm__ volatile ("cli; hlt");
}
