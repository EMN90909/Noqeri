.intel_syntax noprefix
.text
.globl efi_entry
efi_entry:
    push rbp
    mov rbp, rsp
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    call noqeri_uefi_main
    pop rsi
    pop rdi
    pop rbp
    ret
.globl fw_probe
fw_probe:
    xor eax, eax
    ret
.data
.globl efi_base_reloc
efi_base_reloc:
    .quad efi_entry
