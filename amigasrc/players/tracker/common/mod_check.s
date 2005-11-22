******************************************************************************
** general check mod routine (mldoering@gmx.net)
**
** mcheck_moduledata
** input:
**         a0 (long) - pointer to module
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
			move.l	4.w,a6
			lea	uadename(pc),a1
			moveq	#0,d0
    			jsr	-552(a6)
			move.l	d0,uadebase
			tst.l	d0
			bne.b	.mcheck_on_uade
.mcheck_no_uade:	moveq	#-1,d0
			bra.s	.mcheck_end
.mcheck_on_uade:	moveq	#0,d0
.mcheck_end:		movem.l	(a7)+,d1-d7/a0-a6
			rts

;--------------------------------------------------------------------------
; Arguments:
;       a0 = pointer to module data
;       d0 = 1084(mod32) or 600 (mod16)
;
; returns:
;	d0 = status (-1 bad len, 0 = len ok)
;

;mcheck_modlen:
;.mcheck_end:		rts



;--------------------------------------------------------------------------
; Datas:

;fx:			blk.b	31,0	;fx used
;fxarg:			blk.b	31,0	;highest fx arg

uadebase:		dc.l	0
uadename:		dc.b "uade.library",0
			even
