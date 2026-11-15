%ifdef CONFIG
{
  "RegData": {
    "RAX": "0x00000000FFFF8009",
    "RBX": "0x00000000FFFFFF81",
    "RDX": "0x000000000000007F",
    "RSI": "0x00000000FFFFFF83",
    "RDI": "0xFFFFFFFFFFFFFF84",
    "RBP": "0x414243444546FF85",
    "R8":  "0x00000000FFFF8001",
    "R9":  "0x00000000FFFF8002",
    "R10": "0xFFFFFFFFFFFF8003",
    "R11": "0x00000000FFFF8004",
    "R12": "0xFFFFFFFF80000005",
    "R13": "0xFFFFFFFF80000006",
    "R14": "0xFFFFFFFF80000007",
    "R15": "0xFFFFFFFF80000008"
  },
  "MemoryRegions": {
    "0x10000000": "24576"
  },
  "MemoryData": {
    "0x10000EF8": "08 00 00 80",
    "0x10000EFF": "85",
    "0x10000F00": "84",
    "0x100010FF": "03 80",
    "0x10001101": "04 80",
    "0x10001203": "07 00 00 80",
    "0x10001FFF": "7F",
    "0x10002000": "83",
    "0x10002008": "81",
    "0x10002200": "09 80",
    "0x10002FFE": "01 80",
    "0x10003000": "02 80",
    "0x10004FFC": "05 00 00 80",
    "0x10005000": "06 00 00 80"
  },
  "Env": { "FEX_SIGNEXTENDINGLOADS": "1" }
}
%endif

; jdrly/gw2-on-mac#164: with FEX_SIGNEXTENDINGLOADS, MOVSX and MOVSXD from memory are one ldrsb,
; ldrsh or ldrsw. An offset is inlined only when it fits the access's own immediate: unsigned and
; scaled up to 4095 bytes, 8190 halfwords' bytes and 16380 words' bytes, or -256 to 255 unscaled.
; Each limit is loaded on both sides. +0x1008 fits a 32-bit load's scaled immediate but not a
; byte's; encoded as a byte's it reads other memory. The 32-bit destinations start at -1, so the
; upper half has to be cleared.

mov rcx, 0x10001000

mov rax, 0x900
mov rbx, -1
mov rdx, -1
mov rsi, -1
mov rdi, -1
mov rbp, 0x4142434445464748
mov r8, -1
mov r9, -1
mov r10, -1
mov r11, -1
mov r12, -1
mov r13, -1
mov r14, -1
mov r15, -1

; a halfword index scaled by the access size
movsx eax, word [rcx + rax*2]

; bytes
movsx ebx, byte [rcx + 0x1008]
movsx edx, byte [rcx + 4095]
movsx esi, byte [rcx + 4096]
movsx rdi, byte [rcx - 256]
movsx bp, byte [rcx - 257]

; halfwords
movsx r8d, word [rcx + 8190]
movsx r9d, word [rcx + 8192]
movsx r10, word [rcx + 255]
movsx r11d, word [rcx + 257]

; words
movsxd r12, dword [rcx + 16380]
movsxd r13, dword [rcx + 16384]
movsxd r14, dword [rcx + 0x203]
movsxd r15, dword [rcx - 264]

hlt
