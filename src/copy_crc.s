	.assume adl=1
	.section .text
	.global _bad30_copy_crc
	.type _bad30_copy_crc, @function
	.extern _crc16_table
; unsigned int bad30_copy_crc(dst, src, count, crc)
; Forward copy with CRC16, including overlapping ZX7 matches.
; count is 1..255. IX is callee-saved; all other work registers are volatile.
_bad30_copy_crc:
	push ix
	ld ix, 0
	add ix, sp
	ld hl, (ix+9)
	ld de, (ix+6)
	ld bc, (ix+15)
	ld ix, (ix+12)
.Lcopy:
	ld a, (hl)
	ld (de), a
	inc hl
	inc de
	push hl
	push de
	xor a, b
	ld hl, 0
	ld l, a
	add hl, hl
	ld de, _crc16_table
	add hl, de
	ld e, (hl)
	inc hl
	ld a, (hl)
	xor a, c
	ld b, a
	ld c, e
	pop de
	pop hl
	dec ix
	ld a, ixl
	or a, a
	jr nz, .Lcopy
	ld hl, 0
	ld l, c
	ld h, b
	pop ix
	ret
