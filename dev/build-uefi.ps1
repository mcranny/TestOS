# Builds the UEFI (x86-64/Limine) TestOS kernel using clang + ld.lld.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$clang = "C:\Program Files\LLVM\bin\clang.exe"
$ld = "C:\Program Files\LLVM\bin\ld.lld.exe"

$cflags = @(
    "--target=x86_64-unknown-none-elf", "-m64", "-ffreestanding", "-fno-pie",
    "-fno-pic", "-fno-stack-protector", "-mno-red-zone", "-mcmodel=kernel",
    "-nostdlib", "-nostdinc", "-Wall", "-Wextra", "-Iuefi", "-Iuefi/net"
)

Write-Host "Building userland programs..."
New-Item -ItemType Directory -Force -Path build/uefi/user | Out-Null
$ucflags = @(
    "--target=x86_64-unknown-none-elf","-m64","-ffreestanding","-fno-pie","-fno-pic",
    "-fno-stack-protector","-nostdlib","-nostdinc","-Wall","-Iuefi","-Iuefi/user"
)
& $clang @ucflags -c uefi/user/crt0.S -o build/uefi/user/crt0.o
& $clang @ucflags -c uefi/user/ulib.c -o build/uefi/user/ulib.o
function Build-UserElf($name, $src) {
    Write-Host "Building uefi-$name.elf..."
    & $clang @ucflags -c $src -o "build/uefi/user/$name.o"
    if ($LASTEXITCODE -ne 0) { throw "$name compile failed" }
    & $ld -m elf_x86_64 -nostdlib -T uefi/user/user.ld -o "build/uefi-$name.elf" `
        build/uefi/user/crt0.o build/uefi/user/ulib.o "build/uefi/user/$name.o"
    if ($LASTEXITCODE -ne 0) { throw "$name link failed" }
}
Build-UserElf "calc" "uefi/user/calc.c"
Build-UserElf "dns" "uefi/user/dns_main.c"
Build-UserElf "wget" "uefi/user/wget_main.c"
Build-UserElf "tcp" "uefi/user/tcp_main.c"
Build-UserElf "udp" "uefi/user/udp_main.c"

$cSources = @(
    "uefi/kernel.c",
    "uefi/boot/limine_boot.c",
    "uefi/drivers/serial.c",
    "uefi/drivers/log.c",
    "uefi/drivers/fb.c",
    "uefi/drivers/pic.c",
    "uefi/drivers/pit.c",
    "uefi/arch/tss.c",
    "uefi/arch/gdt.c",
    "uefi/arch/idt.c",
    "uefi/arch/interrupts.c",
    "uefi/arch/smap.c",
    "uefi/mm/pmm.c",
    "uefi/mm/paging.c",
    "uefi/mm/heap.c",
    "uefi/mm/dma.c",
    "uefi/cpu/cpu_local.c",
    "uefi/lib/string.c",
    "uefi/drivers/kbd.c",
    "uefi/drivers/console.c",
    "uefi/drivers/device.c",
    "uefi/drivers/pci.c",
    "uefi/drivers/ata.c",
    "uefi/drivers/ahci.c",
    "uefi/drivers/nvme.c",
    "uefi/block/block.c",
    "uefi/input/input.c",
    "uefi/usb/usb.c",
    "uefi/usb/xhci.c",
    "uefi/usb/hid.c",
    "uefi/fs/tfs.c",
    "uefi/fs/path.c",
    "uefi/fs/ramfs.c",
    "uefi/task/process.c",
    "uefi/user/syscall.c",
    "uefi/user/elf64.c",
    "uefi/user/exec.c",
    "uefi/net/e1000.c",
    "uefi/net/checksum.c",
    "uefi/net/mac.c",
    "uefi/net/netif.c",
    "uefi/net/route.c",
    "uefi/net/ethernet.c",
    "uefi/net/arp.c",
    "uefi/net/ipv4.c",
    "uefi/net/icmp.c",
    "uefi/net/udp.c",
    "uefi/net/tcp.c",
    "uefi/net/socket.c",
    "uefi/net/dhcp.c",
    "uefi/net/dns.c",
    "uefi/net/http.c",
    "uefi/shell/shell.c"
)

$sSources = @(
    "uefi/entry.S",
    "uefi/arch/isr.S",
    "uefi/arch/gdt_load.S",
    "uefi/task/switch.S",
    "uefi/user/syscall_entry.S",
    "uefi/user/calc_blob.S",
    "uefi/user/dns_blob.S",
    "uefi/user/wget_blob.S",
    "uefi/user/tcp_blob.S",
    "uefi/user/udp_blob.S"
)

$objects = @()

foreach ($src in ($cSources + $sSources)) {
    if (-not (Test-Path $src)) { throw "missing $src" }
    $rel = $src.Substring(5)
    $obj = "build/uefi/" + ($rel -replace '\.[cS]$', '.o')
    $dir = Split-Path -Parent $obj
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Write-Host "CC/AS $src"
    & $clang @cflags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
    $objects += $obj
}

Write-Host "LD build/testos-uefi.elf"
& $ld -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T uefi/linker.ld -o build/testos-uefi.elf @objects
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Host "OK: build/testos-uefi.elf"
