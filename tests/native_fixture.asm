OPTION CASEMAP:NONE
PUBLIC ExerciseBefore
PUBLIC FixtureHookSite
PUBLIC ExerciseAfter
PUBLIC FixtureAfterSite
PUBLIC TrashVolatiles
.const
Canary BYTE 16 DUP (05ah)
.code
ExerciseBefore PROC
    push rbx
    push rsi
    push rdi
    push r13
    push r14
    sub rsp, 0a0h
    mov r14, [rsp+0f0h]
    mov rsi, rcx
    mov rdi, rdx
    mov eax, r8d
    mov r13d, r9d
    mov r11d, eax
    mov r10, 0123456789abcdefh
    movdqu xmm0, XMMWORD PTR [Canary]
    movdqu xmm1, XMMWORD PTR [Canary]
    movdqu xmm2, XMMWORD PTR [Canary]
    movdqu xmm3, XMMWORD PTR [Canary]
    movdqu xmm4, XMMWORD PTR [Canary]
    movdqu xmm5, XMMWORD PTR [Canary]
FixtureHookSite LABEL BYTE
    ; Exact live-game instructions at RVA 3ed3ed.
    db 046h, 08dh, 004h, 028h, 045h, 033h, 0edh
    mov [r14+0], r8
    mov [r14+8], rax
    mov [r14+16], rcx
    mov [r14+24], rdx
    mov [r14+32], r9
    mov [r14+40], r10
    mov [r14+48], r11
    mov [r14+56], r13
    pushfq
    pop rbx
    mov [r14+64], rbx
    movdqu XMMWORD PTR [r14+72], xmm0
    movdqu XMMWORD PTR [r14+88], xmm1
    movdqu XMMWORD PTR [r14+104], xmm2
    movdqu XMMWORD PTR [r14+120], xmm3
    movdqu XMMWORD PTR [r14+136], xmm4
    movdqu XMMWORD PTR [r14+152], xmm5
    add rsp, 0a0h
    pop r14
    pop r13
    pop rdi
    pop rsi
    pop rbx
    ret
ExerciseBefore ENDP

ExerciseAfter PROC
    push rsi
    push r14
    push rbx
    sub rsp, 20h
    mov rsi, rcx
    mov r14, rdx
    mov rbx, r8
FixtureAfterSite LABEL BYTE
    db 0f3h,00fh,010h,046h,044h
    movss DWORD PTR [rbx], xmm0
    add rsp, 20h
    pop rbx
    pop r14
    pop rsi
    ret
ExerciseAfter ENDP

TrashVolatiles PROC
    mov rcx, -1
    mov rdx, -1
    mov r8, -1
    mov r9, -1
    mov r10, -1
    mov r11, -1
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    stc
    ret
TrashVolatiles ENDP
END
