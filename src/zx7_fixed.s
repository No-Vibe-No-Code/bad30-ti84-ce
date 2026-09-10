	.assume	adl=1

	.section	.text
	.global	_bad30_zx7_decompress
	.type	_bad30_zx7_decompress, @function

; void bad30_zx7_decompress(void *destination, const void *source)
; Standard ZX7 decoder by Einar Saukas, Antonio Villena & Metalbrain,
; adapted for the CE eZ80 ABI.  This version keeps the four-bit extended
; offset marker used by convbin's ZX7 compressor.
_bad30_zx7_decompress:
	pop	bc
	pop	de
	ex	(sp), hl
	push	de
	push	bc

	ld	a, $80
bad30_zx7_copy_byte_loop:
	ldi
bad30_zx7_main_loop:
	call	bad30_zx7_next_bit
	jr	nc, bad30_zx7_copy_byte_loop

	push	de
	ld	bc, 0
	inc.s	de
	ld	d, b
bad30_zx7_len_size_loop:
	inc	d
	call	bad30_zx7_next_bit
	jr	nc, bad30_zx7_len_size_loop

bad30_zx7_len_value_loop:
	call	nc, bad30_zx7_next_bit
	rl	c
	rl	b
	jr	c, bad30_zx7_exit
	dec	d
	jr	nz, bad30_zx7_len_value_loop
	inc	bc

	ld	e, (hl)
	inc	hl
	scf
	rl	e
	jr	nc, bad30_zx7_offset_end
	ld	d, $10
bad30_zx7_offset_bits:
	call	bad30_zx7_next_bit
	rl	d
	jr	nc, bad30_zx7_offset_bits
	inc	d
	srl	d
bad30_zx7_offset_end:
	rr	e

	ex	(sp), hl
	push	hl
	sbc	hl, de
	pop	de
	ldir
bad30_zx7_exit:
	pop	hl
	jr	nc, bad30_zx7_main_loop
	ret

bad30_zx7_next_bit:
	add	a, a
	ret	nz
	ld	a, (hl)
	inc	hl
	rla
	ret
