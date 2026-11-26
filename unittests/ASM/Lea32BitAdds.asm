%ifdef CONFIG
{
  "RegData": {
    "RAX": "0x0000000000000010",
    "RDX": "0x00000000800000EF",
    "RSI": "0x00000000FFFFFFD0",
    "RDI": "0x0000000000000080",
    "R8":  "0x0000000000000010",
    "R9":  "0x0000000000000000",
    "R10": "0x0000000000000000",
    "R11": "0x00000000FFFFFFFF",
    "R12": "0x0000000000000010",
    "R13": "0x0000000000000008",
    "R14": "0x0000000080000030",
    "R15": "0x00000000FFFFFFF4"
  },
  "Env": { "FEX_LEA32BITADDS": "1" }
}
%endif

; jdrly/gw2-on-mac#164: with FEX_LEA32BITADDS, LEA with a 32-bit destination computes the address
; with 32-bit adds. Each address here goes past 4 GB or below 0, and the destination keeps its low
; half, zero-extended. The destinations start at -1.

mov rbx, 0x00000001FFFFFFF0
mov rcx, 0x0000000100000020
mov rax, -1
mov rdx, -1
mov rsi, -1
mov rdi, -1
mov r8, -1
mov r11, -1
mov r12, -1
mov r13, 0xFFFFFFFF00000001
mov r14, -1
mov r15, -1

; base and index: 0x3_00000010
lea eax, [rbx + rcx]
; base, scaled index and displacement: 0xA_800000EF
lea edx, [rbx + rcx*8 + 0x7FFFFFFF]
; a negative displacement: 0x1_FFFFFFD0
lea esi, [rbx - 0x20]
; a scaled index alone: 0x4_00000080
lea edi, [rcx*4]
; 32-bit addressing wraps at 4 GB itself: 0xFFFFFFF0 + 0x20
lea r8d, [ebx + ecx]
; RIP-relative, the same low half as the 64-bit LEA
lea r9d, [rel rip_here]
rip_here:
lea r10, [rel rip_here]
mov r10d, r10d
sub r9, r10
xor r10d, r10d
; a displacement alone, -1
lea r11d, [-1]
lea r12d, [rbx + rcx]
; the same register as base, index and destination: 3 * 0xFFFFFFFF00000001 + 5
lea r13d, [r13 + r13*2 + 5]
; a negative displacement that wraps below the index: 0x4_00000030 - 0x80000000
lea r14d, [rbx + rcx*2 - 0x80000000]
; LEA ignores a segment prefix
lea r15d, [fs:rbx + 4]

hlt
