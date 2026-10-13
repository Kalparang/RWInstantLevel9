; Both sites are MID-FUNCTION jumps, with RSP % 16 == 0, not call entries.
; Save flags + 7 volatile GPRs (64 bytes), then shadow space + XMM0..5 (128).
; R13 is the only deliberately changed nonvolatile register at the before site.
OPTION CASEMAP:NONE
EXTERN AdjustMasteryGain:PROC
EXTERN ObserveMasteryCommit:PROC
EXTERN g_mastery_trampoline:QWORD
EXTERN g_mastery_after_trampoline:QWORD
PUBLIC MasteryDetour
PUBLIC MasteryAfterDetour

SAVE_STATE MACRO
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 80h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    movdqu [rsp+60h], xmm4
    movdqu [rsp+70h], xmm5
ENDM
RESTORE_STATE MACRO
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    movdqu xmm4, [rsp+60h]
    movdqu xmm5, [rsp+70h]
    add rsp, 80h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
ENDM
.code
MasteryDetour PROC
    SAVE_STATE
    mov rcx, rsi
    mov rdx, rdi
    mov r8d, eax
    mov r9d, r13d
    call AdjustMasteryGain
    mov r13d, eax
    RESTORE_STATE
    ; Original LEA computes new cumulative XP; XOR resets R13 and flags.
    jmp QWORD PTR [g_mastery_trampoline]
MasteryDetour ENDP

MasteryAfterDetour PROC
    SAVE_STATE
    mov rcx, rsi
    mov rdx, r14
    call ObserveMasteryCommit
    RESTORE_STATE
    jmp QWORD PTR [g_mastery_after_trampoline]
MasteryAfterDetour ENDP
END
