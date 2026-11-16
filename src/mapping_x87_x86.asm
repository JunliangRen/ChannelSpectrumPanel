; x86 cdecl counterpart of mapping_x87.asm; keep arithmetic and cache points aligned.
; Reconstructed arithmetic, not copied machine code. Preserve x87 intermediate precision
; and the original per-bin double caches before truncating to pixel coordinates.
.686p
.model flat, C
option casemap:none
.const
c1024 REAL8 1024.0
c1025 REAL8 1025.0
.code
csp_mapping_coordinates PROC
    push ebx
    push esi
    push edi
    sub esp,248
    mov ebx,esp
    mov esi,[esp+264]
    mov edi,[esp+268]
    fnstcw word ptr [ebx+232]
    mov word ptr [ebx+234],027fh
    fldcw word ptr [ebx+234]
    fldln2
    fld qword ptr c1025
    fyl2x
    fst qword ptr [ebx+224]
    fstp tbyte ptr [ebx+0] ; ln(1025), extended
    mov eax,[esi+8]
    dec eax
    mov [ebx+240],eax
    fild dword ptr [ebx+240]
    fstp tbyte ptr [ebx+16] ; D
    mov eax,[esi]
    dec eax
    call compute_q
    cmp dword ptr [esi+16],0
    je keep_previous
    fstp qword ptr [ebx+208]
    fld qword ptr [ebx+208]
keep_previous:
    fstp tbyte ptr [ebx+32]
    mov eax,[esi]
    call compute_q
    fstp qword ptr [ebx+208]
    fld qword ptr [ebx+208]
    fstp tbyte ptr [ebx+48]
    ; Anchor uses the reloaded denominator on channels after the first.
    cmp dword ptr [esi+16],0
    je anchor_q
    fld qword ptr [ebx+224]
    fstp tbyte ptr [ebx+0]
anchor_q:
    mov eax,[esi+4]
    call compute_q
    fstp tbyte ptr [ebx+64]
    ; T = max(m, extent*m/D)
    fild dword ptr [esi+4]
    fild dword ptr [esi+12]
    fmulp st(1),st(0)
    fld tbyte ptr [ebx+16]
    fdivp st(1),st(0)
    fild dword ptr [esi+4]
    fcomi st(0),st(1)
    jae keep_anchor
    fstp st(0)
    jmp anchor_t
keep_anchor:
    fstp st(1)
anchor_t:
    fstp tbyte ptr [ebx+80]
    ; Denominator 1024-Q(m), scale extent-T
    fld qword ptr c1024
    fld tbyte ptr [ebx+64]
    fsubp st(1),st(0)
    fstp tbyte ptr [ebx+96]
    fild dword ptr [esi+12]
    fld tbyte ptr [ebx+80]
    fsubp st(1),st(0)
    fstp tbyte ptr [ebx+112]
    fld tbyte ptr [ebx+32]
    call interpolate
    fstp tbyte ptr [ebx+128]
    fld tbyte ptr [ebx+48]
    call interpolate
    fstp tbyte ptr [ebx+144]
    fld tbyte ptr [ebx+144]
    fld tbyte ptr [ebx+128]
    fsubp st(1),st(0)
    fabs
    fld1
    fxch st(1)
    fcomip st(0),st(1)
    seta al
    movzx eax,al
    mov [edi+8],eax
    fstp st(0)
    ; Truncate without requiring SSE3 FISTTP on x86.
    mov word ptr [ebx+234],0e7fh
    fldcw word ptr [ebx+234]
    fld tbyte ptr [ebx+128]
    fistp dword ptr [edi]
    fld tbyte ptr [ebx+144]
    fistp dword ptr [edi+4]
    fldcw word ptr [ebx+232]
    add esp,248
    pop edi
    pop esi
    pop ebx
    ret
compute_q:
    shl eax,10
    mov [ebx+240],eax
    fild dword ptr [ebx+240]
    fld tbyte ptr [ebx+16]
    fdivp st(1),st(0)
    fld1
    faddp st(1),st(0)
    fldln2
    fxch st(1)
    fyl2x
    fmul qword ptr c1024
    fld tbyte ptr [ebx+0]
    fdivp st(1),st(0)
    ret
interpolate:
    fld tbyte ptr [ebx+64]
    fsubp st(1),st(0)
    fld tbyte ptr [ebx+112]
    fmulp st(1),st(0)
    fld tbyte ptr [ebx+96]
    fdivp st(1),st(0)
    fld tbyte ptr [ebx+80]
    faddp st(1),st(0)
    ret
csp_mapping_coordinates ENDP
END
