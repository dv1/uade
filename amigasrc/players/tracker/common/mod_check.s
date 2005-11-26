******************************************************************************
** general check mod routine (mldoering@gmx.net)
**
** mcheck_moduledata
** input:
**         a0 (long) - pointer to module
**	   d1 (long) - filesize
**
** returns:
**	   d0 (word) - result (-1 for check failed, other type of mod)
**
** -----
** TODO: Real checks like modlen, patterndata, magic ids :) 
**       Currently we accept anything if we are running under
**       UADE, and refuse anything when we run under a real amiga
******************************************************************************

mcheck_moduledata:	; Current implemation is just a hack for uade only.
			movem.l	d1-d7/a0-a6,-(a7)

			move.l	#1084,d0
			bsr	mcheck_calc_modlen
			cmp.w	#-1,d0			;mod32 ?
			bne	.mcheck_mod32	;yup
			move.l	#600,d0
			bsr	mcheck_calc_modlen
			cmp.w	#-1,d0			;mod15 ?
			beq	.mcheck_fail		;nope

.mcheck_mod15:		move.l #"M15.",modtag
			bra	.mcheck_open_on_uade
.mcheck_mod32:		move.l #"MOD.",modtag

.mcheck_open_on_uade:	move.l	4.w,a6
			lea	uadename(pc),a1
			moveq	#0,d0
    			jsr	-552(a6)
			move.l	d0,uadebase
			tst.l	d0
			beq.b	.mcheck_fail

			move.l	modtag,d0
.mcheck_end:		movem.l	(a7)+,d1-d7/a0-a6
			rts

.mcheck_fail:		moveq	#-1,d0
			bra.s	.mcheck_end

;--------------------------------------------------------------------------
; Calculate Modlen
; Arguments:
;       a0.l = pointer to module data
;       d0.l = 1084(mod32) or 600 (mod16)
;	d1.l = file length
;
; returns:
;	d0 = status (-1 bad len, 0 = len ok)
;

mcheck_calc_modlen:
			cmp.l	d0,d1
			bhi.b	.is_higher
			moveq	#-1,d0
			rts

.is_higher		movem.l	d1-d7/a0-a6,-(a7)

        		move.l	d0,header
			cmp.l	#1084,d0		;header size
			beq	.mcheck_32instr

			move.l	#472,d2			;d2 = plist
			moveq	#14,d3			;d3 no of Instruments 	
			bra	.mcheck_calc_start

.mcheck_32instr:	move.l	#952,d2
			moveq	#30,d3

			;--- Get Maxpattern ---
.mcheck_calc_start:
			lea.l	(a0,d2),a5		; plist
			moveq	#127,d4
			moveq	#0,d5
.mcheck_loop:		
			cmp.b	(a5)+,d5
			bge.s	.mcheck_loop2
			move.b	-1(a5),d5
.mcheck_loop2:
			dbra	d4,.mcheck_loop
			addq.b	#1,d5			; maxpattern

			;--- Calc Instruments ---

			move.l	a0,a5
			asl.l	#8,d5
			asl.l	#2,d5
			add.l	header,d5
.mcheck_loop3:		moveq	#0,d6
			move.w	42(a5),d6
			asl.l	#1,d6
			add.l	d6,d5
			add.l	#30,a5
			dbra	d3,.mcheck_loop3

			;--- Check file len ---
			cmp.l	d1,d5
			bne	.mcheck_bad_length

.mcheck_good_length:	moveq	#0,d0
			bra	.mcheck_end			

.mcheck_bad_length:	moveq	#-1,d0
.mcheck_end:		movem.l	(a7)+,d1-d7/a0-a6
	    		rts



;--------------------------------------------------------------------------
; Datas:

;fx:			blk.b	31,0	;fx used
;fxarg:			blk.b	31,0	;highest fx arg

header:			dc.l	0
modtag:			dc.l	0

			even
uadebase:		dc.l	0
uadename:		dc.b "uade.library",0
			even
