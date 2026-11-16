; Reconstructed arithmetic, not copied machine code. Preserve x87 intermediate precision
; and the original per-bin double caches before truncating to pixel coordinates.
.const
c1024 REAL8 1024.0
c1025 REAL8 1025.0
.code
csp_mapping_coordinates PROC FRAME
    sub rsp, 248
    .allocstack 248
    .endprolog
    mov r8,rsp
    mov r10,rcx
    mov r11,rdx
    fnstcw word ptr [r8+232]
    mov word ptr [r8+234],027fh
    fldcw word ptr [r8+234]
    fldln2
    fld qword ptr c1025
    fyl2x
    fst qword ptr [r8+224]
    fstp tbyte ptr [r8+0] ; ln(1025), extended
    mov eax,[r10+8]
    dec eax
    mov [r8+240],eax
    fild dword ptr [r8+240]
    fstp tbyte ptr [r8+16] ; D
    mov eax,[r10]
    dec eax
    call compute_q
    cmp dword ptr [r10+16],0
    je keep_previous
    fstp qword ptr [r8+208]
    fld qword ptr [r8+208]
keep_previous:
    fstp tbyte ptr [r8+32]
    mov eax,[r10]
    call compute_q
    fstp qword ptr [r8+208]
    fld qword ptr [r8+208]
    fstp tbyte ptr [r8+48]
    ; Anchor uses the reloaded denominator on channels after the first.
    cmp dword ptr [r10+16],0
    je anchor_q
    fld qword ptr [r8+224]
    fstp tbyte ptr [r8+0]
anchor_q:
    mov eax,[r10+4]
    call compute_q
    fstp tbyte ptr [r8+64]
    ; T = max(m, extent*m/D)
    fild dword ptr [r10+4]
    fild dword ptr [r10+12]
    fmulp st(1),st(0)
    fld tbyte ptr [r8+16]
    fdivp st(1),st(0)
    fild dword ptr [r10+4]
    fcomi st(0),st(1)
    jae keep_anchor
    fstp st(0)
    jmp anchor_t
keep_anchor:
    fstp st(1)
anchor_t:
    fstp tbyte ptr [r8+80]
    ; Denominator 1024-Q(m), scale extent-T
    fld qword ptr c1024
    fld tbyte ptr [r8+64]
    fsubp st(1),st(0)
    fstp tbyte ptr [r8+96]
    fild dword ptr [r10+12]
    fld tbyte ptr [r8+80]
    fsubp st(1),st(0)
    fstp tbyte ptr [r8+112]
    fld tbyte ptr [r8+32]
    call interpolate
    fstp tbyte ptr [r8+128]
    fld tbyte ptr [r8+48]
    call interpolate
    fstp tbyte ptr [r8+144]
    fld tbyte ptr [r8+144]
    fld tbyte ptr [r8+128]
    fsubp st(1),st(0)
    fabs
    fld1
    fxch st(1)
    fcomip st(0),st(1)
    seta al
    movzx eax,al
    mov [r11+8],eax
    fstp st(0)
    fld tbyte ptr [r8+128]
    fisttp dword ptr [r11]
    fld tbyte ptr [r8+144]
    fisttp dword ptr [r11+4]
    fldcw word ptr [r8+232]
    add rsp,248
    ret
compute_q:
    shl eax,10
    mov [r8+240],eax
    fild dword ptr [r8+240]
    fld tbyte ptr [r8+16]
    fdivp st(1),st(0)
    fld1
    faddp st(1),st(0)
    fldln2
    fxch st(1)
    fyl2x
    fmul qword ptr c1024
    fld tbyte ptr [r8+0]
    fdivp st(1),st(0)
    ret
interpolate:
    fld tbyte ptr [r8+64]
    fsubp st(1),st(0)
    fld tbyte ptr [r8+112]
    fmulp st(1),st(0)
    fld tbyte ptr [r8+96]
    fdivp st(1),st(0)
    fld tbyte ptr [r8+80]
    faddp st(1),st(0)
    ret
csp_mapping_coordinates ENDP
END
