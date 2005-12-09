
mod_fail=-1
mod_DOC=0
mod_MST=2
mod_STIV=3
mod_UST=4

mod_PTK_comp=32
mod_PTK=33
mod_PTK_vblank=34
mod_NTK_2=35
mod_NTK_1=36
mod_NTK_AMP=37
mod_STK=38
mod_FLT4=39
mod_FLT8=40
mod_ADSC4=41
mod_ADSC8=42
mod_FTK=43



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
			bne	.mod32			;yup
			move.l	#600,d0
			bsr	mcheck_calc_modlen
			cmp.w	#-1,d0			;mod15 ?
			beq	.mcheck_fail		;nope

.mod15:			bsr	mcheck_mod15
			bra	.mcheck_open_on_uade

.mod32:			bsr mcheck_mod32

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

;---- Mod32 Checks ----
mod32_Magic:
			dc.l	"M.K."
					bra	mcheck_which_mk
			dc.l	"M!K!"
					bra	mcheck_is_ptk
			dc.l	".M.K"
					bra	mcheck_is_ptk
			dc.l	"M&K!"
					bra	mcheck_is_ntk_amp
			dc.l	"FLT4"
					bra	mcheck_which_FLT4
			dc.l	"EXO4"
					bra	mcheck_which_FLT4
			dc.l	-1,-1,-1


mcheck_mod32:
			lea.l	mod32_Magic(pc),a1
.magic_loop:		move.l	(a1)+,d0
			cmp.l	#-1,d0
			beq	mcheck_which_mk		; no M.K. found
			cmp.l	$438(a0),d0
			beq.s	.tag_found
			add.l	#4,a1
			bra	.magic_loop
.tag_found:		jmp	(a1)

mcheck_mod32_fail:
			rts

;-----------------------------------------------------------------------------
; M.K. - file: TODO: Distinguish STK,NTK1,NTK2,PTK and FTK :)
;
mcheck_which_mk:
			bsr	ParseInstruments32	; returns -1 for failed check.
			cmp.b	#-1,d0
			beq	mcheck_mod32_fail

			tst.b	finetune_used
			beq	mcheck_is_ptk
			
			tst.b	repeat_in_bytes_used
			bne	.no_finetune
			move.l #mod_STK,modtag		; Soundtracker 2.5
			rts
.no_finetune:
			
mcheck_is_ptk:
			; Check for vblank by playtime
			; in Protracker modules 
			moveq.l	#0,d0
			move.l	#25000/50,d1		; 50Hz
			moveq.l	#1,d2
			move.l	song,a0
			lea.l	Timer,a1
			bsr	PTCalcTime
			move.l #mod_PTK_vblank,modtag
			lea.l	Timer,a1
			move.l	(a1),d0
			cmp.l	#0,d0			; playtime in hours?
			bgt	.mcheck_end
			move.l	4(a1),d0
			cmp.l	#28,d0			; more than 28 minutes?
			bgt	.mcheck_end
			move.l #mod_PTK,modtag	
.mcheck_end		rts

			
;----------------------------------------------------------------------------
; M&K!- Noisetracker file
;
mcheck_is_ntk_amp:
			move.l #mod_NTK_AMP,modtag	
			rts

;----------------------------------------------------------------------------
; M&K!- Noisetracker file
;
mcheck_which_flt4:	;bra	mcheck_is_flt4
mcheck_is_flt4:
			rts




;******  Mod15 Checks *******************************************************
mcheck_mod15:
			move.l #"M15.",modtag
			rts

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
			move.w	d5,maxpattern
			
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


;------------------------------------------------------------------------------
;
;	$VER: PTCalcTime v1.1 - by Håvard "Howard" Pedersen
;	© 1994-96 Mental Diseases
;
;	A program for calculating the playtime of ProTracker modules.
;
;	I cannot be held responsible for any damage caused directly or in-
;	directly by this code. Still, every released version is thouroughly
;	tested with Mungwall and Enforcer, official Commodore debugging tools.
;	These programs traps writes to unallocated ram and reads/writes to/from
;	non-ram memory areas, which should cover most bugs.
;
;	HISTORY:
;
;v1.0	Simple thingy with bugs and quirks. Did calculating using IRQ/second.
;
;v1.1	Bugfix version. Finally usable.
;	* Pattern loop wasn't sensed properly. By some reason I used effect
;	  command E5 for set loop! (?)
;	* Entire pattern loop code was broken. Recoded, does now work correctly
;	  with "mod.couldbe".
;	* Uses 1/25000th second between each interrupt for timing. Much more
;	  accurate in CIA mode.
;	* Small speedups here and there.
;
;------------------------------------------------------------------------------
;
;	PUBLIC FUNCTIONS:
;
;Function:	PTCalcTime(SongPos,delay,CIA(BOOL),Module,TimerStruct)
;		(D0,D1,D2,A0,A1)
;Purpose:	Calculates exact playtime of a ProTracker module.
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
;			PTCALCTIME
;------------------------------------------------------------------------------
; D0 - Local use
; D1 - 25000th of a second delay between each IRQ
; D2 - Local use
; D3 - Pattern delay count
; D4 - Local use
; D5 - 
; D6 - 
; D7 - Voiceloop count
; A0 - Module
; A1 - TimerStruct
; A2 - PatternPosition
; A3 - Local use
; A4 - 
; A5 - 
; A6 - 

* BUGGY !!! Does not work right with mod.ode2ptk *sigh*

PTCalcTime	move.l	#6,.Speed
		move.l	#0,.PattPos
		move.l	d0,.SongPos
		move.l	#0,.PattLoopPos
		move.l	#0,.PosJumpPos
		move.b	d2,.CIAFlag

.MainLoop	lea.l	952(a0),a2		; Get position
		add.l	.SongPos,a2
		moveq.l	#0,d0
		move.b	(a2),d0			; Get pattern at current pos
		lsl.l	#8,d0			; *1024
		lsl.l	#2,d0
		lea.l	1084(a0),a2
		add.l	d0,a2			; Address for pattern

.StepLoop	lea.l	952(a0),a2		; Get position
		add.l	.SongPos,a2
		moveq.l	#0,d0
		move.b	(a2),d0			; Get pattern at current pos
		lsl.l	#8,d0			; *1024
		lsl.l	#2,d0
		lea.l	1084(a0),a2
		add.l	d0,a2			; Address for pattern

		move.l	.PattPos,d0
		lsl.l	#4,d0
		add.l	d0,a2

		moveq.l	#4-1,d7			; Loop
.VoiceLoop	lea.l	.CmdsTab,a3
		move.l	(a2),d0			; Get stuff
		and.l	#$00000ff0,d0		; Get command

		move.l	d0,d2
		and.l	#$00000f00,d2
		cmp.l	#$00000e00,d2		; Misc cmds?
		beq.s	.TabLoop

		and.l	#$00000f00,d0

.TabLoop	cmp.l	#-1,(a3)
		beq.s	.NoneFound

		cmp.l	(a3),d0
		bne.s	.NoMatch

		move.l	4(a3),a3
		jsr	(a3)
		bra.s	.NoneFound

.NoMatch	addq.l	#8,a3
		bra.s	.TabLoop

.NoneFound	addq.l	#4,a2
		dbf	d7,.VoiceLoop

		bsr.w	.AddSpeed

		addq.l	#1,.PattPos

		tst.b	.BreakFlag
		bne.s	.NewPos

		cmp.l	#64,.PattPos
		bne.w	.StepLoop

.NewPos		move.b	#0,.BreakFlag

		move.l	.PattBreakPos,.PattPos
		move.l	#0,.PattBreakPos	; Default pattern break pos

		tst.l	.PosJumpPos
		beq.s	.NoPosJump

		move.l	.PosJumpPos,.SongPos
		move.l	#0,.PosJumpPos
		bra.s	.EndIt

.NoPosJump	add.l	#1,.SongPos
		move.l	.SongPos,d0
		lea.l	950(a0),a3
		cmp.b	(a3),d0
		blo.w	.MainLoop

.EndIt		move.l	12(a1),d0
		divu.w	#250,d0
		and.l	#$ffff,d0
		move.l	d0,12(a1)		; Convert to 100/s.

		rts

.AddSpeed	move.l	.Speed,d0
		subq.l	#1,d0
.SpeedLoop	add.l	d1,12(a1)
		dbf	d0,.SpeedLoop
.AddSpeedLoop	cmp.l	#25000,12(a1)
		blo.s	.OkIRQs
		sub.l	#25000,12(a1)
		add.l	#1,8(a1)
.OkIRQs		cmp.l	#60,8(a1)
		blo.s	.OkSecs
		sub.l	#60,8(a1)
		add.l	#1,4(a1)
.OkSecs		cmp.l	#60,4(a1)
		blo.s	.OkMins
		sub.l	#60,4(a1)
		add.l	#1,(a1)
.OkMins		rts

.CmdsTab	dc.l	$00000b00,._PosJump
		dc.l	$00000d00,._PattBreak
		dc.l	$00000f00,._SetSpeed
		dc.l	$00000e60,._PatLoop
		dc.l	$00000ee0,._PatDelay
		dc.l	-1,-1

._PosJump	move.l	(a2),d0			; Get stuff
		and.l	#$ff,d0
		move.l	d0,.PosJumpPos
		move.b	#-1,.BreakFlag
		rts

._PattBreak	move.l	(a2),d0			; Get stuff
		and.l	#$ff,d0
		move.l	d0,.PattBreakPos
		move.b	#-1,.BreakFlag
		rts

._SetSpeed	move.l	(a2),d0			; Get stuff
		and.l	#$ff,d0
		beq.s	.Halt

		tst.b	.CIAFlag
		beq.s	.VBL

		cmp.b	#$20,d0
		blo.s	.VBL

		; Do some CIA->Hz converting!
		move.l	#62500,d1
		divu.w	d0,d1
		and.l	#$ffff,d1

		rts

.VBL		move.l	d0,.Speed
		rts

.Halt		move.l	#-1,.PosJumpPos		; Halt module
		move.b	#-1,.BreakFlag
		rts

._PatLoop	rts				; temporarily disable pt_loop
						; because of ode2ptk...

		move.l	(a2),d0			; Get stuff
		and.l	#$f,d0
		tst.l	d0
		beq.s	.SetLoop

		tst.l	.PattLoopCnt
		beq.s	.SetLoopCnt


		subq.l	#1,.PattLoopCnt
		bne.s	.DoLoop

		rts

.SetLoop
		move.l	.PattPos,.PattLoopPos
		rts

.SetLoopCnt
		move.l	d0,.PattLoopCnt

.DoLoop		move.l	.PattLoopPos,.PattBreakPos; Force loop
		sub.l	#1,.SongPos
		move.b	#-1,.BreakFlag

		rts

.PattLoopIt	subq.l	#1,.PattLoopCnt
		tst.l	.PattLoopCnt
		beq.s	.Return

		move.l	.PattLoopPos,.PattPos
.Return		rts

._PatDelay	move.l	(a2),d0			; Get stuff
		and.l	#$f,d0
		tst.l	d0
		beq.s	.PatDelNo
		subq.l	#1,d0
		move.l	d0,d3
.PatDelLoop	bsr.w	.AddSpeed
		dbf	d3,.PatDelLoop
.PatDelNo	rts

.BreakFlag	dc.b	0
.CIAFlag	dc.b	0
	EVEN
.Speed		dc.l	0
.PattPos	dc.l	0
.SongPos	dc.l	0
.PattLoopPos	dc.l	0
.PattLoopCnt	dc.l	0
.PosJumpPos	dc.l	0
.PattBreakPos	dc.l	0

;--------------------------------------------------------------------------
; ParseInstruments
; Arguments:
;       a0.l = pointer to module data
;
; returns:
;	d0 = status (-1 bad file, 0 = len ok)
;

ParseInstruments32:
		movem.l	d1-d7/a0-a6,-(a7)
		moveq	#0,d1
		moveq	#30,d0
.parseloop:
		cmp.b	#64,45(a0)		; volume > 64
		bgt	.parse_fail

		move.b	44(a0),d1
		cmp.w	#15,d1			; fine_tune > 15
		bgt	.parse_fail
		cmp.w	#0,d1
		beq	.parse_no_finetune
		st	finetune_used		; 0 <finetune <16

.parse_no_finetune:
		cmp.w	#0,42(a0)		; sample len
		beq	.parse_empty
		bra	.parse_other

.parse_empty
		;cmp.l	#0,20(a0)		; empty instrument name
		;cmp.w	#0,46(a0)		; repl len
		;cmp.w	#0,48(a0)		; loop size

		bra	.parse_next
.parse_other:
		move.w	46(a0),d1
		add.w	48(a0),d1
		cmp.w	42(a0),d1		; srep+sreplen>slen ?
		bls	.parse_next
		st	repeat_in_bytes_used

.parse_next:	add.l	#30,a1
		dbra	d0,.parseloop

.parse_Ok	moveq	#0,d0
		movem.l	(a7)+,d1-d7/a0-a6
		rts

.parse_fail	moveq	#-1,d0
		movem.l	(a7)+,d1-d7/a0-a6
		rts

;--------------------------------------------------------------------------
; Datas:
;Instrument flags

repeat_in_bytes_used	dc.b	0	
finetune_used:		dc.b	0
			even
maxpattern:		dc.w	0
Timer:			dc.l	0,0,0,0			; Hours, Minutes, secs
header:			dc.l	0
modtag:			dc.l	0
uadebase:		dc.l	0
uadename:		dc.b	"uade.library",0
			even
