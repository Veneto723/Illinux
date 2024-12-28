set confirm off
set architecture riscv:rv64
target remote 127.0.0.1:26376
set disassemble-next-line auto
set riscv use-compressed-breakpoints yes
add-symbol-file ../user/bin/init6 0x00000000c0000000
