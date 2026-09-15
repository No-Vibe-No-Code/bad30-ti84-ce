	.assume	adl=1

	.section	.text
	.global	_rickroll_zx7_decompress
	.type	_rickroll_zx7_decompress, @function

; void rickroll_zx7_decompress(void *destination, const void *source)
; Standard ZX7 decoder by Einar Saukas, Antonio Villena & Metalbrain,
; adapted for the CE eZ80 ABI.  This version keeps the four-bit extended
; offset marker used by convbin's ZX7 compressor.
_rickroll_zx7_decompress:
	pop	bc
	pop	de
	ex	(sp), hl
	push	de
	push	bc

	ld	a, $80
rickroll_zx7_copy_byte_loop:
	ldi
rickroll_zx7_main_loop:
	call	rickroll_zx7_next_bit
	jr	nc, rickroll_zx7_copy_byte_loop

	push	de
	ld	bc, 0
	inc.s	de
	ld	d, b
rickroll_zx7_len_size_loop:
	inc	d
	call	rickroll_zx7_next_bit
	jr	nc, rickroll_zx7_len_size_loop

rickroll_zx7_len_value_loop:
	call	nc, rickroll_zx7_next_bit
	rl	c
	rl	b
	jr	c, rickroll_zx7_exit
	dec	d
	jr	nz, rickroll_zx7_len_value_loop
	inc	bc

	ld	e, (hl)
	inc	hl
	scf
	rl	e
	jr	nc, rickroll_zx7_offset_end
	ld	d, $10
rickroll_zx7_offset_bits:
	call	rickroll_zx7_next_bit
	rl	d
	jr	nc, rickroll_zx7_offset_bits
	inc	d
	srl	d
rickroll_zx7_offset_end:
	rr	e

	ex	(sp), hl
	push	hl
	sbc	hl, de
	pop	de
	ldir
rickroll_zx7_exit:
	pop	hl
	jr	nc, rickroll_zx7_main_loop
	ret

rickroll_zx7_next_bit:
	add	a, a
	ret	nz
	ld	a, (hl)
	inc	hl
	rla
	ret
