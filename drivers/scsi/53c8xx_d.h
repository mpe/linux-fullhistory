unsigned long SCRIPT[] = {
/*
; NCR 53c810 driver, main script
; Sponsored by 
;	iX Multiuser Multitasking Magazine
;	hm@ix.de
;
; Copyright 1993, Drew Eckhardt
;      Visionary Computing 
;      (Unix and Linux consulting and custom programming)
;      drew@Colorado.EDU
;      +1 (303) 786-7975
;
; TolerANT and SCSI SCRIPTS are registered trademarks of NCR Corporation.
;
; PRE-ALPHA
;
; For more information, please consult 
;
; NCR 53C810
; PCI-SCSI I/O Processor
; Data Manual
;
; NCR 53C710 
; SCSI I/O Processor
; Programmers Guide
;
; NCR Microelectronics
; 1635 Aeroplaza Drive
; Colorado Springs, CO 80916
; 1+ (719) 578-3400
;
; Toll free literature number
; +1 (800) 334-5454
;
; IMPORTANT : This code is self modifying due to the limitations of 
;	the NCR53c7,8xx series chips.  Persons debugging this code with
;	the remote debugger should take this into account, and NOT set
;	breakpoints in modified instructions.
;
;
; Design:
; The NCR53c7x0 family of SCSI chips are busmasters with an onboard 
; microcontroller using a simple instruction set.   
;
; So, to minimize the effects of interrupt latency, and to maximize 
; throughput, this driver offloads the practical maximum amount 
; of processing to the SCSI chip while still maintaining a common
; structure.
;
; Where tradeoffs were needed between efficiency on the older
; chips and the newer NCR53c800 series, the NCR53c800 series 
; was chosen.
;
; While the NCR53c700 and NCR53c700-66 lacked the facilities to fully
; automate SCSI transfers without host processor intervention, this 
; isn't the case with the NCR53c710 and newer chips which allow 
;
; - reads and writes to the internal registers from within the SCSI
; 	scripts, allowing the SCSI SCRIPTS(tm) code to save processor
; 	state so that multiple threads of execution are possible, and also
; 	provide an ALU for loop control, etc.
; 
; - table indirect addressing for some instructions. This allows 
;	pointers to be located relative to the DSA ((Data Structure
;	Address) register.
;
; These features make it possible to implement a mailbox style interface,
; where the same piece of code is run to handle I/O for multiple threads
; at once minimizing our need to relocate code.  Since the NCR53c700/
; NCR53c800 series have a unique combination of features, making a 
; a standard ingoing/outgoing mailbox system, costly, I've modified it.
;
; - Commands are stored in a linked list, rather than placed in 
; 	arbitrary mailboxes.  This simplifies the amount of processing
;	that must be done by the NCR53c810.
;
; - Mailboxes are a mixture of code and data.  This lets us greatly
; 	simplify the NCR53c810 code and do things that would otherwise
;	not be possible.

;
; Note : the DSA structures must be aligned on 32 bit boundaries,
; since the source and destination of MOVE MEMORY instructions 
; must share the same alignment and this is the alignment of the
; NCR registers.
;

ABSOLUTE dsa_temp_jump_resume = 0	; Patch to dsa_jump_resume
    	    	    	    	    	; 	in current dsa
ABSOLUTE dsa_temp_lun = 0		; Patch to lun for current dsa
ABSOLUTE dsa_temp_dsa_next = 0		; Patch to dsa next for current dsa
ABSOLUTE dsa_temp_sync = 0		; Patch to address of per-target
					;	sync routine
ABSOLUTE dsa_temp_target = 0		; Patch to id for current dsa



ENTRY dsa_code_template
dsa_code_template:

; Define DSA structure used for mailboxes

; wrong_dsa loads the DSA register with the value of the dsa_next
; field.
;
wrong_dsa:
;		Patch the MOVE MEMORY INSTRUCTION such that 
;		the destination address is that of the OLD next
;		pointer.
	MOVE MEMORY 4, dsa_temp_dsa_next, reselected_ok + 8

at 0x00000000 : */	0xc0000004,0x00000000,0x00000660,
/*

	MOVE dmode_memory_to_ncr TO DMODE	

at 0x00000003 : */	0x78380000,0x00000000,
/*
	MOVE MEMORY 4, dsa_temp_dsa_next, addr_scratch

at 0x00000005 : */	0xc0000004,0x00000000,0x00000000,
/*
	MOVE dmode_memory_to_memory TO DMODE

at 0x00000008 : */	0x78380000,0x00000000,
/*
	CALL scratch_to_dsa

at 0x0000000a : */	0x88080000,0x00000800,
/*
	JUMP reselected_check_next

at 0x0000000c : */	0x80080000,0x000005ac,
/*

ABSOLUTE dsa_check_reselect = 0
; dsa_check_reselect determines weather or not the current target and
; lun match the current DSA
ENTRY dsa_code_check_reselect
dsa_code_check_reselect:
	MOVE SSID TO SFBR		; SSID contains 3 bit target ID

at 0x0000000e : */	0x720a0000,0x00000000,
/*
	JUMP REL (wrong_dsa), IF NOT dsa_temp_target, AND MASK 7

at 0x00000010 : */	0x80840700,0x00ffffb8,
/*
	MOVE dmode_memory_to_ncr TO DMODE

at 0x00000012 : */	0x78380000,0x00000000,
/*
	MOVE MEMORY 1, reselected_identify, addr_sfbr

at 0x00000014 : */	0xc0000001,0x00000000,0x00000000,
/*
	JUMP REL (wrong_dsa), IF NOT dsa_temp_lun, AND MASK 7

at 0x00000017 : */	0x80840700,0x00ffff9c,
/*
	MOVE dmode_memory_to_memory TO DMODE

at 0x00000019 : */	0x78380000,0x00000000,
/*
;		Patch the MOVE MEMORY INSTRUCTION such that
;		the source address is that of this dsas
;		next pointer.
	MOVE MEMORY 4, dsa_temp_dsa_next, reselected_ok + 4

at 0x0000001b : */	0xc0000004,0x00000000,0x0000065c,
/*
	CALL reselected_ok

at 0x0000001e : */	0x88080000,0x00000658,
/*
	CALL dsa_temp_sync	

at 0x00000020 : */	0x88080000,0x00000000,
/*
ENTRY dsa_jump_resume
dsa_jump_resume:
	JUMP 0				; Jump to resume address

at 0x00000022 : */	0x80080000,0x00000000,
/*
ENTRY dsa_zero
dsa_zero:
	MOVE dmode_ncr_to_memory TO DMODE			; 8

at 0x00000024 : */	0x78380000,0x00000000,
/*
	MOVE MEMORY 4, addr_temp, dsa_temp_jump_resume		; 16	

at 0x00000026 : */	0xc0000004,0x00000000,0x00000000,
/*
	MOVE dmode_memory_to_memory TO DMODE			; 28

at 0x00000029 : */	0x78380000,0x00000000,
/*
	JUMP dsa_schedule					; 36

at 0x0000002b : */	0x80080000,0x000000b4,
/*
ENTRY dsa_code_template_end
dsa_code_template_end:

; Perform sanity check for dsa_fields_start == dsa_code_template_end - 
; dsa_zero, puke.

ABSOLUTE dsa_fields_start =  36	; Sanity marker
				; 	pad 12
ABSOLUTE dsa_next = 48		; len 4 Next DSA
 				; del 4 Previous DSA address
ABSOLUTE dsa_cmnd = 56		; len 4 Scsi_Cmnd * for this thread.
ABSOLUTE dsa_select = 60	; len 4 Device ID, Period, Offset for 
			 	;	table indirect select
ABSOLUTE dsa_msgout = 64	; len 8 table indirect move parameter for 
				;       select message
ABSOLUTE dsa_cmdout = 72	; len 8 table indirect move parameter for 
				;	command
ABSOLUTE dsa_dataout = 80	; len 4 code pointer for dataout
ABSOLUTE dsa_datain = 84	; len 4 code pointer for datain
ABSOLUTE dsa_msgin = 88		; len 8 table indirect move for msgin
ABSOLUTE dsa_status = 96 	; len 8 table indirect move for status byte
ABSOLUTE dsa_msgout_other = 104	; len 8 table indirect for normal message out
				; (Synchronous transfer negotiation, etc).
ABSOLUTE dsa_end = 112

; Linked lists of DSA structures
ABSOLUTE issue_dsa_head = 0	; Linked list of DSAs to issue
ABSOLUTE reconnect_dsa_head = 0	; Link list of DSAs which can reconnect

; These select the source and destination of a MOVE MEMORY instruction
ABSOLUTE dmode_memory_to_memory = 0x0
ABSOLUTE dmode_memory_to_ncr = 0x0
ABSOLUTE dmode_ncr_to_memory = 0x0
ABSOLUTE dmode_ncr_to_ncr = 0x0

ABSOLUTE addr_scratch = 0x0
ABSOLUTE addr_sfbr = 0x0
ABSOLUTE addr_temp = 0x0


; Interrupts - 
; MSB indicates type
; 0	handle error condition
; 1 	handle message 
; 2 	handle normal condition
; 3	debugging interrupt
; 4 	testing interrupt 
; Next byte indicates specific error

; XXX not yet implemented, I'm not sure if I want to - 
; Next byte indicates the routine the error occurred in
; The LSB indicates the specific place the error occurred
 
ABSOLUTE int_err_unexpected_phase = 0x00000000	; Unexpected phase encountered
ABSOLUTE int_err_selected = 0x00010000		; SELECTED (nee RESELECTED)
ABSOLUTE int_err_unexpected_reselect = 0x00020000 
ABSOLUTE int_err_check_condition = 0x00030000	
ABSOLUTE int_err_no_phase = 0x00040000
ABSOLUTE int_msg_wdtr = 0x01000000		; WDTR message received
ABSOLUTE int_msg_sdtr = 0x01010000		; SDTR received
ABSOLUTE int_msg_1 = 0x01020000			; single byte special message
						; received

ABSOLUTE int_norm_select_complete = 0x02000000	; Select complete, reprogram
						; registers.
ABSOLUTE int_norm_reselect_complete = 0x02010000	; Nexus established
ABSOLUTE int_norm_command_complete = 0x02020000 ; Command complete
ABSOLUTE int_norm_disconnected = 0x02030000	; Disconnected 
ABSOLUTE int_norm_aborted =0x02040000		; Aborted *dsa
ABSOLUTE int_norm_reset = 0x02050000		; Generated BUS reset.
ABSOLUTE int_debug_break = 0x03000000		; Break point
ABSOLUTE int_debug_scheduled = 0x03010000	; new I/O scheduled 
ABSOLUTE int_debug_idle = 0x03020000		; scheduler is idle
ABSOLUTE int_debug_dsa_loaded = 0x03030000	; dsa reloaded
ABSOLUTE int_debug_reselected = 0x03040000	; NCR reselected
ABSOLUTE int_debug_head = 0x03050000		; issue head overwritten

ABSOLUTE int_test_1 = 0x04000000		; Test 1 complete
ABSOLUTE int_test_2 = 0x04010000		; Test 2 complete
ABSOLUTE int_test_3 = 0x04020000		; Test 3 complete
						
EXTERNAL NCR53c7xx_msg_abort 		; Pointer to abort message
EXTERNAL NCR53c7xx_msg_reject 		; Pointer to reject message
EXTERNAL NCR53c7xx_zero			; long with zero in it, use for source
EXTERNAL NCR53c7xx_sink			; long to dump worthless data in

; Pointer to final bytes of multi-byte messages
ABSOLUTE msg_buf = 0

; Pointer to holding area for reselection information
ABSOLUTE reselected_identify = 0
ABSOLUTE reselected_tag = 0

; Request sense command pointer, its a 6 byte command, should
; be constant for all commands since we allays want 16 bytes of 
; sense and we don't need to change any fields as we did under 
; SCSI-I when we actually cared about the LUN field.
;EXTERNAL NCR53c7xx_sense		; Request sense command


; dsa_schedule  
; PURPOSE : after a DISCONNECT message has been received, and pointers
;	saved, insert the current DSA structure at the head of the 
; 	disconnected queue and fall through to the scheduler.
;
; CALLS : OK
;
; INPUTS : dsa - current DSA structure, reconnect_dsa_head - list
;	of disconnected commands
;
; MODIFIES : SCRATCH, reconnect_dsa_head
; 
; EXITS : allays passes control to schedule

ENTRY dsa_schedule
dsa_schedule:

;
; Calculate the address of the next pointer within the DSA 
; structure of the command that is currently disconnecting
;
    CALL dsa_to_scratch

at 0x0000002d : */	0x88080000,0x000007b8,
/*
; XXX - we need to deal with the NCR53c710, which lacks an add with
;	carry instruction, by moving around the DSA alignment to avoid
; 	carry in situations like this.
    MOVE SCRATCH0 + dsa_next TO SCRATCH0

at 0x0000002f : */	0x7e343000,0x00000000,
/*
    MOVE SCRATCH1 + 0 TO SCRATCH1 WITH CARRY

at 0x00000031 : */	0x7f350000,0x00000000,
/*
    MOVE SCRATCH2 + 0 TO SCRATCH2 WITH CARRY

at 0x00000033 : */	0x7f360000,0x00000000,
/*
    MOVE SCRATCH3 + 0 TO SCRATCH3 WITH CARRY

at 0x00000035 : */	0x7f370000,0x00000000,
/*

; Point the next field of this DSA structure at the current disconnected 
; list
    MOVE dmode_ncr_to_memory TO DMODE

at 0x00000037 : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, dsa_schedule_insert + 8

at 0x00000039 : */	0xc0000004,0x00000000,0x00000100,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x0000003c : */	0x78380000,0x00000000,
/*
dsa_schedule_insert:
    MOVE MEMORY 4, reconnect_dsa_head, 0 

at 0x0000003e : */	0xc0000004,0x00000000,0x00000000,
/*

; And update the head pointer.
    CALL dsa_to_scratch

at 0x00000041 : */	0x88080000,0x000007b8,
/*
    MOVE dmode_ncr_to_memory TO DMODE	

at 0x00000043 : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, reconnect_dsa_head

at 0x00000045 : */	0xc0000004,0x00000000,0x00000000,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x00000048 : */	0x78380000,0x00000000,
/*
    WAIT DISCONNECT

at 0x0000004a : */	0x48000000,0x00000000,
/*

; schedule
; PURPOSE : schedule a new I/O once the bus is free by putting the 
;	address of the next DSA structure in the DSA register.
;
; INPUTS : issue_dsa_head - list of new commands
;
; CALLS : OK
;
; MODIFIES : SCRATCH, DSA 
;
; EXITS : if the issue_dsa_head pointer is non-NULL, control
;	is passed to select.  Otherwise, control is passed to 
;	wait_reselect.


ENTRY schedule
schedule:
    ; Point DSA at the current head of the issue queue.
    MOVE dmode_memory_to_ncr TO DMODE

at 0x0000004c : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, issue_dsa_head, addr_scratch

at 0x0000004e : */	0xc0000004,0x00000000,0x00000000,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x00000051 : */	0x78380000,0x00000000,
/*

    CALL scratch_to_dsa

at 0x00000053 : */	0x88080000,0x00000800,
/*




    ; Check for a null pointer.
    MOVE DSA0 TO SFBR

at 0x00000055 : */	0x72100000,0x00000000,
/*
    JUMP select, IF NOT 0

at 0x00000057 : */	0x80040000,0x00000194,
/*
    MOVE DSA1 TO SFBR

at 0x00000059 : */	0x72110000,0x00000000,
/*
    JUMP select, IF NOT 0

at 0x0000005b : */	0x80040000,0x00000194,
/*
    MOVE DSA2 TO SFBR

at 0x0000005d : */	0x72120000,0x00000000,
/*
    JUMP select, IF NOT 0

at 0x0000005f : */	0x80040000,0x00000194,
/*
    MOVE DSA3 TO SFBR

at 0x00000061 : */	0x72130000,0x00000000,
/*
    JUMP wait_reselect, IF 0

at 0x00000063 : */	0x800c0000,0x00000560,
/*

    



;
; select
;
; PURPOSE : establish a nexus for the SCSI command referenced by DSA.
;	On success, the current DSA structure is removed from the issue 
;	queue.  Usually, this is entered as a fall-through from schedule,
;	although the contingent allegiance handling code will write
;	the select entry address to the DSP to restart a command as a 
;	REQUEST SENSE.  A message is sent (usually IDENTIFY, although
;	additional SDTR or WDTR messages may be sent).  COMMAND OUT
;	is handled.
;
; INPUTS : DSA - SCSI command, issue_dsa_head
;
; CALLS : OK
;
; MODIFIES : SCRATCH, issue_dsa_head
;
; EXITS : on reselection or selection, go to select_failed
;	otherwise, fall through to data_transfer.  If a MSG_IN
;	phase occurs before 
;

ENTRY select
select:




    CLEAR TARGET

at 0x00000065 : */	0x60000200,0x00000000,
/*

; XXX
;
; In effect, SELECTION operations are backgrounded, with execution
; continuing until code which waits for REQ or a fatal interrupt is 
; encountered.
;
; So, for more performance, we could overlap the code which removes 
; the command from the NCRs issue queue with the selection, but 
; at this point I don't want to deal with the error recovery.
;


    SELECT ATN FROM dsa_select, select_failed

at 0x00000067 : */	0x4300003c,0x00000694,
/*
    JUMP select_msgout, WHEN MSG_OUT

at 0x00000069 : */	0x860b0000,0x000001ac,
/*
ENTRY select_msgout
select_msgout:
    MOVE FROM dsa_msgout, WHEN MSG_OUT

at 0x0000006b : */	0x1e000000,0x00000040,
/*









    ; Calculate address of dsa_next field

    CALL dsa_to_scratch

at 0x0000006d : */	0x88080000,0x000007b8,
/*

    MOVE SCRATCH0 + dsa_next TO SCRATCH0

at 0x0000006f : */	0x7e343000,0x00000000,
/*
    MOVE SCRATCH1 + 0 TO SCRATCH1 WITH CARRY

at 0x00000071 : */	0x7f350000,0x00000000,
/*
    MOVE SCRATCH2 + 0 TO SCRATCH2 WITH CARRY

at 0x00000073 : */	0x7f360000,0x00000000,
/*
    MOVE SCRATCH3 + 0 TO SCRATCH3 WITH CARRY

at 0x00000075 : */	0x7f370000,0x00000000,
/*

    ; Patch memory to memory move
    move dmode_ncr_to_memory TO DMODE

at 0x00000077 : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, issue_remove + 4

at 0x00000079 : */	0xc0000004,0x00000000,0x000001fc,
/*


    ; And rewrite the issue_dsa_head pointer.
    MOVE dmode_memory_to_memory TO DMODE

at 0x0000007c : */	0x78380000,0x00000000,
/*
issue_remove:
;	The actual UPDATE of the issue_dsa_head variable is 
; 	atomic, with all of the setup code being irrelevant to
;	weather the updated value being the old or new contents of 
;	dsa_next field.
;
; 	To insure synchronization, the host system merely needs to 
;	do a XCHG instruction with interrupts disabled on the 
;	issue_dsa_head memory address.
;
;	The net effect will be that the XCHG instruction will return
;	either a non-NULL value, indicating that the NCR chip will not
;	go into the idle loop when this command DISCONNECTS, or a NULL
;	value indicating that the NCR wrote first and that the Linux
;	code must rewrite the issue_dsa_head pointer and set SIG_P.
;	


    MOVE MEMORY 4, 0, issue_dsa_head

at 0x0000007e : */	0xc0000004,0x00000000,0x00000000,
/*





; After a successful selection, we should get either a CMD phase or 
; some transfer request negotiation message.

    JUMP cmdout, WHEN CMD

at 0x00000081 : */	0x820b0000,0x00000224,
/*
    INT int_err_unexpected_phase, WHEN NOT MSG_IN 

at 0x00000083 : */	0x9f030000,0x00000000,
/*

select_msg_in:
    CALL msg_in, WHEN MSG_IN

at 0x00000085 : */	0x8f0b0000,0x00000354,
/*
    JUMP select_msg_in, WHEN MSG_IN

at 0x00000087 : */	0x870b0000,0x00000214,
/*

cmdout:
    INT int_err_unexpected_phase, WHEN NOT CMD

at 0x00000089 : */	0x9a030000,0x00000000,
/*



ENTRY cmdout_cmdout
cmdout_cmdout:

    MOVE FROM dsa_cmdout, WHEN CMD

at 0x0000008b : */	0x1a000000,0x00000048,
/*




;
; data_transfer  
; other_transfer
;
; PURPOSE : handle the main data transfer for a SCSI command in 
;	two parts.  In the first part, data_transfer, DATA_IN
;	and DATA_OUT phases are allowed, with the user provided
;	code (usually dynamically generated based on the scatter/gather
;	list associated with a SCSI command) called to handle these 
;	phases.
;
;	On completion, the user code passes control to other_transfer
;	which causes DATA_IN and DATA_OUT to result in unexpected_phase
;	interrupts so that data overruns may be trapped.
;
; INPUTS : DSA - SCSI command
;
; CALLS : OK
;
; MODIFIES : SCRATCH
;
; EXITS : if STATUS IN is detected, signifying command completion,
;	the NCR jumps to command_complete.  If MSG IN occurs, a 
;	CALL is made to msg_in.  Otherwise, other_transfer runs in 
;	an infinite loop.
;	

data_transfer:
    INT int_err_unexpected_phase, WHEN CMD

at 0x0000008d : */	0x9a0b0000,0x00000000,
/*
    CALL msg_in, WHEN MSG_IN

at 0x0000008f : */	0x8f0b0000,0x00000354,
/*
    INT int_err_unexpected_phase, WHEN MSG_OUT

at 0x00000091 : */	0x9e0b0000,0x00000000,
/*
    JUMP do_dataout, WHEN DATA_OUT

at 0x00000093 : */	0x800b0000,0x0000026c,
/*
    JUMP do_datain, WHEN DATA_IN

at 0x00000095 : */	0x810b0000,0x000002c4,
/*
    JUMP command_complete, WHEN STATUS

at 0x00000097 : */	0x830b0000,0x00000508,
/*
    JUMP data_transfer

at 0x00000099 : */	0x80080000,0x00000234,
/*

;
; On NCR53c700 and NCR53c700-66 chips, do_dataout/do_datain are fixed up 
; whenever the nexus changes so it can point to the correct routine for 
; that command.
;


; Nasty jump to dsa->dataout
do_dataout:
    CALL dsa_to_scratch

at 0x0000009b : */	0x88080000,0x000007b8,
/*
    MOVE SCRATCH0 + dsa_dataout TO SCRATCH0	

at 0x0000009d : */	0x7e345000,0x00000000,
/*
    MOVE SCRATCH1 + 0 TO SCRATCH1 WITH CARRY 

at 0x0000009f : */	0x7f350000,0x00000000,
/*
    MOVE SCRATCH2 + 0 TO SCRATCH2 WITH CARRY 

at 0x000000a1 : */	0x7f360000,0x00000000,
/*
    MOVE SCRATCH3 + 0 TO SCRATCH3 WITH CARRY 

at 0x000000a3 : */	0x7f370000,0x00000000,
/*
    MOVE dmode_ncr_to_memory TO DMODE

at 0x000000a5 : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, dataout_to_jump + 4

at 0x000000a7 : */	0xc0000004,0x00000000,0x000002b4,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x000000aa : */	0x78380000,0x00000000,
/*
dataout_to_jump:
    MOVE MEMORY 4, 0, dataout_jump + 4 

at 0x000000ac : */	0xc0000004,0x00000000,0x000002c0,
/*
dataout_jump:
    JUMP 0

at 0x000000af : */	0x80080000,0x00000000,
/*

; Nasty jump to dsa->dsain
do_datain:
    CALL dsa_to_scratch

at 0x000000b1 : */	0x88080000,0x000007b8,
/*
    MOVE SCRATCH0 + dsa_datain TO SCRATCH0	

at 0x000000b3 : */	0x7e345400,0x00000000,
/*
    MOVE SCRATCH1 + 0 TO SCRATCH1 WITH CARRY 

at 0x000000b5 : */	0x7f350000,0x00000000,
/*
    MOVE SCRATCH2 + 0 TO SCRATCH2 WITH CARRY 

at 0x000000b7 : */	0x7f360000,0x00000000,
/*
    MOVE SCRATCH3 + 0 TO SCRATCH3 WITH CARRY 

at 0x000000b9 : */	0x7f370000,0x00000000,
/*
    MOVE dmode_ncr_to_memory TO DMODE

at 0x000000bb : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, datain_to_jump + 4

at 0x000000bd : */	0xc0000004,0x00000000,0x0000030c,
/*
    MOVE dmode_memory_to_memory TO DMODE		

at 0x000000c0 : */	0x78380000,0x00000000,
/*
datain_to_jump:
    MOVE MEMORY 4, 0, datain_jump + 4

at 0x000000c2 : */	0xc0000004,0x00000000,0x00000318,
/*
datain_jump:
    JUMP 0

at 0x000000c5 : */	0x80080000,0x00000000,
/*


;
; other_transfer is exported because it is referenced by dynamically 
; generated code.
;
ENTRY other_transfer
other_transfer:



    INT int_err_unexpected_phase, WHEN CMD

at 0x000000c7 : */	0x9a0b0000,0x00000000,
/*
    CALL msg_in, WHEN MSG_IN 

at 0x000000c9 : */	0x8f0b0000,0x00000354,
/*
    INT int_err_unexpected_phase, WHEN MSG_OUT

at 0x000000cb : */	0x9e0b0000,0x00000000,
/*
    INT int_err_unexpected_phase, WHEN DATA_OUT

at 0x000000cd : */	0x980b0000,0x00000000,
/*
    INT int_err_unexpected_phase, WHEN DATA_IN

at 0x000000cf : */	0x990b0000,0x00000000,
/*
    JUMP command_complete, WHEN STATUS

at 0x000000d1 : */	0x830b0000,0x00000508,
/*
    JUMP other_transfer

at 0x000000d3 : */	0x80080000,0x0000031c,
/*

;
; msg_in
; munge_msg
;
; PURPOSE : process messages from a target.  msg_in is called when the 
;	caller hasn't read the first byte of the message.  munge_message
;	is called when the caller has read the first byte of the message,
;	and left it in SFBR.
;
;	Various int_* interrupts are generated when the host system
;	needs to intervene, as is the case with SDTR, WDTR, and
;	INITIATE RECOVERY messages.
;
;	When the host system handles one of these interrupts,
;	it can respond by reentering at reject_message, 
;	which rejects the message and returns control to
;	the caller of msg_in or munge_msg, accept_message
;	which clears ACK and returns control, or reply_message
;	which sends the message pointed to by the DSA 
;	msgout_other table indirect field.
;
;	DISCONNECT messages are handled by moving the command
;	to the reconnect_dsa_queue.
;
;	SAVE DATA POINTER and RESTORE DATA POINTERS are currently 
;	treated as NOPS. 
;
; INPUTS : DSA - SCSI COMMAND, SFBR - first byte of message (munge_msg
;	only)
;
; CALLS : NO.  The TEMP register isn't backed up to allow nested calls.
;
; MODIFIES : SCRATCH, DSA on DISCONNECT
;
; EXITS : On receipt of SAVE DATA POINTER, RESTORE POINTERS,
;	and normal return from message handlers running under
;	Linux, control is returned to the caller.  Receipt
;	of DISCONNECT messages pass control to dsa_schedule.
;
ENTRY msg_in
msg_in:
    MOVE 1, msg_buf, WHEN MSG_IN

at 0x000000d5 : */	0x0f000001,0x00000000,
/*

munge_msg:
    JUMP munge_extended, IF 0x01		; EXTENDED MESSAGE

at 0x000000d7 : */	0x800c0001,0x00000428,
/*
    JUMP munge_2, IF 0x20, AND MASK 0xdf	; two byte message

at 0x000000d9 : */	0x800cdf20,0x0000039c,
/*
;
; I've seen a handful of broken SCSI devices which fail to issue
; a SAVE POINTERS message before disconnecting in the middle of 
; a transfer, assuming that the DATA POINTER will be implicitly 
; restored.  So, we treat the SAVE DATA POINTER message as a NOP.
;
; I've also seen SCSI devices which don't issue a RESTORE DATA
; POINTER message and assume that thats implicit.
;
    JUMP accept_message, IF 0x02		; SAVE DATA POINTER

at 0x000000db : */	0x800c0002,0x000004d8,
/*
    JUMP accept_message, IF 0x03		; RESTORE POINTERS 

at 0x000000dd : */	0x800c0003,0x000004d8,
/*
    JUMP munge_disconnect, IF 0x04		; DISCONNECT

at 0x000000df : */	0x800c0004,0x000003b4,
/*
    INT int_msg_1, IF 0x07			; MESSAGE REJECT

at 0x000000e1 : */	0x980c0007,0x01020000,
/*
    INT int_msg_1, IF 0x0f			; INITIATE RECOVERY

at 0x000000e3 : */	0x980c000f,0x01020000,
/*
    JUMP reject_message

at 0x000000e5 : */	0x80080000,0x000004b8,
/*

munge_2:
    JUMP reject_message

at 0x000000e7 : */	0x80080000,0x000004b8,
/*

munge_save_data_pointer:
    CLEAR ACK

at 0x000000e9 : */	0x60000040,0x00000000,
/*
    RETURN

at 0x000000eb : */	0x90080000,0x00000000,
/*

munge_disconnect:
    MOVE SCNTL2 & 0x7f TO SCNTL2

at 0x000000ed : */	0x7c027f00,0x00000000,
/*
    CLEAR ACK

at 0x000000ef : */	0x60000040,0x00000000,
/*


; Pass control to the DSA routine.  Note that we can not call
; dsa_to_scratch here because that would clobber temp, which 
; we must preserve.
    MOVE DSA0 TO SFBR

at 0x000000f1 : */	0x72100000,0x00000000,
/*
    MOVE SFBR TO SCRATCH0

at 0x000000f3 : */	0x6a340000,0x00000000,
/*
    MOVE DSA1 TO SFBR

at 0x000000f5 : */	0x72110000,0x00000000,
/*
    MOVE SFBR TO SCRATCH1

at 0x000000f7 : */	0x6a350000,0x00000000,
/*
    MOVE DSA2 TO SFBR

at 0x000000f9 : */	0x72120000,0x00000000,
/*
    MOVE SFBR TO SCRATCH2

at 0x000000fb : */	0x6a360000,0x00000000,
/*
    MOVE DSA3 TO SFBR

at 0x000000fd : */	0x72130000,0x00000000,
/*
    MOVE SFBR TO SCRATCH3

at 0x000000ff : */	0x6a370000,0x00000000,
/*

    MOVE dmode_ncr_to_memory TO DMODE

at 0x00000101 : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, jump_to_dsa + 4

at 0x00000103 : */	0xc0000004,0x00000000,0x00000424,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x00000106 : */	0x78380000,0x00000000,
/*
jump_to_dsa:
    JUMP 0

at 0x00000108 : */	0x80080000,0x00000000,
/*





munge_extended:
    CLEAR ACK

at 0x0000010a : */	0x60000040,0x00000000,
/*
    INT int_err_unexpected_phase, WHEN NOT MSG_IN

at 0x0000010c : */	0x9f030000,0x00000000,
/*
    MOVE 1, msg_buf + 1, WHEN MSG_IN

at 0x0000010e : */	0x0f000001,0x00000001,
/*
    JUMP munge_extended_2, IF 0x02

at 0x00000110 : */	0x800c0002,0x00000458,
/*
    JUMP munge_extended_3, IF 0x03 

at 0x00000112 : */	0x800c0003,0x00000488,
/*
    JUMP reject_message

at 0x00000114 : */	0x80080000,0x000004b8,
/*

munge_extended_2:
    CLEAR ACK

at 0x00000116 : */	0x60000040,0x00000000,
/*
    MOVE 1, msg_buf + 2, WHEN MSG_IN

at 0x00000118 : */	0x0f000001,0x00000002,
/*
    JUMP reject_message, IF NOT 0x02	; Must be WDTR

at 0x0000011a : */	0x80040002,0x000004b8,
/*
    CLEAR ACK

at 0x0000011c : */	0x60000040,0x00000000,
/*
    MOVE 1, msg_buf + 3, WHEN MSG_IN

at 0x0000011e : */	0x0f000001,0x00000003,
/*
    INT int_msg_wdtr

at 0x00000120 : */	0x98080000,0x01000000,
/*

munge_extended_3:
    CLEAR ACK

at 0x00000122 : */	0x60000040,0x00000000,
/*
    MOVE 1, msg_buf + 2, WHEN MSG_IN

at 0x00000124 : */	0x0f000001,0x00000002,
/*
    JUMP reject_message, IF NOT 0x01	; Must be SDTR

at 0x00000126 : */	0x80040001,0x000004b8,
/*
    CLEAR ACK

at 0x00000128 : */	0x60000040,0x00000000,
/*
    MOVE 2, msg_buf + 3, WHEN MSG_IN

at 0x0000012a : */	0x0f000002,0x00000003,
/*
    INT int_msg_sdtr

at 0x0000012c : */	0x98080000,0x01010000,
/*

ENTRY reject_message
reject_message:
    SET ATN

at 0x0000012e : */	0x58000008,0x00000000,
/*
    CLEAR ACK

at 0x00000130 : */	0x60000040,0x00000000,
/*
    MOVE 1, NCR53c7xx_msg_reject, WHEN MSG_OUT

at 0x00000132 : */	0x0e000001,((unsigned long)&NCR53c7xx_msg_reject),
/*
    RETURN

at 0x00000134 : */	0x90080000,0x00000000,
/*

ENTRY accept_message
accept_message:
    CLEAR ACK

at 0x00000136 : */	0x60000040,0x00000000,
/*
    RETURN

at 0x00000138 : */	0x90080000,0x00000000,
/*

ENTRY respond_message
msg_respond:
    SET ATN

at 0x0000013a : */	0x58000008,0x00000000,
/*
    CLEAR ACK

at 0x0000013c : */	0x60000040,0x00000000,
/*
    MOVE FROM dsa_msgout_other, WHEN MSG_OUT

at 0x0000013e : */	0x1e000000,0x00000068,
/*
    RETURN

at 0x00000140 : */	0x90080000,0x00000000,
/*

;
; command_complete
;
; PURPOSE : handle command termination when STATUS IN is detected by reading
;	a status byte followed by a command termination message. 
;
;	Normal termination results in an INTFLY instruction, and 
;	the host system can pick out which command terminated by 
;	examining the MESSAGE and STATUS buffers of all currently 
;	executing commands;
;
;	Abnormal (CHECK_CONDITION) termination results in an
;	int_err_check_condition interrupt so that a REQUEST SENSE
;	command can be issued out-of-order so that no other command
;	clears the contingent allegiance condition.
;	
;
; INPUTS : DSA - command	
;
; CALLS : OK
;
; EXITS : On successful termination, control is passed to schedule.
;	On abnormal termination, the user will usually modify the 
;	DSA fields and corresponding buffers and return control
;	to select.
;

ENTRY command_complete
command_complete:
    MOVE FROM dsa_status, WHEN STATUS

at 0x00000142 : */	0x1b000000,0x00000060,
/*

    MOVE SFBR TO SCRATCH0		; Save status

at 0x00000144 : */	0x6a340000,0x00000000,
/*

ENTRY command_complete_msgin
command_complete_msgin:
    MOVE FROM dsa_msgin, WHEN MSG_IN

at 0x00000146 : */	0x1f000000,0x00000058,
/*
; Indicate that we should be expecting a disconnect
    MOVE SCNTL2 & 0x7f TO SCNTL2

at 0x00000148 : */	0x7c027f00,0x00000000,
/*
    CLEAR ACK

at 0x0000014a : */	0x60000040,0x00000000,
/*

    MOVE SCRATCH0 TO SFBR			

at 0x0000014c : */	0x72340000,0x00000000,
/*

;
; The SCSI specification states that when a UNIT ATTENTION condition
; is pending, as indicated by a CHECK CONDITION status message,
; the target shall revert to asynchronous transfers.  Since
; synchronous transfers parameters are maintained on a per INITIATOR/TARGET 
; basis, and returning control to our scheduler could work on a command
; running on another lun on that target using the old parameters, we must
; interrupt the host processor to get them changed, or change them ourselves.
;
; Once SCSI-II tagged queueing is implemented, things will be even more
; hairy, since contingent allegiance conditions exist on a per-target/lun
; basis, and issuing a new command with a different tag would clear it.
; In these cases, we must interrupt the host processor to get a request 
; added to the HEAD of the queue with the request sense command, or we
; must automatically issue the request sense command.




    INTFLY

at 0x0000014e : */	0x98180000,0x00000000,
/*

    WAIT DISCONNECT

at 0x00000150 : */	0x48000000,0x00000000,
/*

    JUMP schedule

at 0x00000152 : */	0x80080000,0x00000130,
/*
command_failed:
    WAIT DISCONNECT

at 0x00000154 : */	0x48000000,0x00000000,
/*
    INT int_err_check_condition

at 0x00000156 : */	0x98080000,0x00030000,
/*




;
; wait_reselect
;
; PURPOSE : This is essentially the idle routine, where control lands
;	when there are no new processes to schedule.  wait_reselect
;	waits for reselection, selection, and new commands.
;
;	When a successful reselection occurs, with the aid 
;	of fixed up code in each DSA, wait_reselect walks the 
;	reconnect_dsa_queue, asking each dsa if the target ID
;	and LUN match its.
;
;	If a match is found, a call is made back to reselected_ok,
;	which through the miracles of self modifying code, extracts
;	the found DSA from the reconnect_dsa_queue and then 
;	returns control to the DSAs thread of execution.
;
; INPUTS : NONE
;
; CALLS : OK
;
; MODIFIES : DSA,
;
; EXITS : On successful reselection, control is returned to the 
;	DSA which called reselected_ok.  If the WAIT RESELECT
;	was interrupted by a new commands arrival signaled by 
;	SIG_P, control is passed to schedule.  If the NCR is 
;	selected, the host system is interrupted with an 
;	int_err_selected which is usually responded to by
;	setting DSP to the target_abort address.
    
wait_reselect:



    WAIT RESELECT wait_reselect_failed

at 0x00000158 : */	0x50000000,0x0000067c,
/*

reselected:
    ; Read all data needed to reestablish the nexus - 
    MOVE 1, reselected_identify, WHEN MSG_IN

at 0x0000015a : */	0x0f000001,0x00000000,
/*

    ; Well add a jump to here after some how determining that 
    ; tagged queueing isn't in use on this device.
reselected_notag:    
    MOVE MEMORY 1, NCR53c7xx_zero, reselected_tag

at 0x0000015c : */	0xc0000001,((unsigned long)&NCR53c7xx_zero),0x00000000,
/*





    ; Point DSA at the current head of the disconnected queue.
    MOVE dmode_memory_to_ncr  TO DMODE

at 0x0000015f : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, reconnect_dsa_head, addr_scratch

at 0x00000161 : */	0xc0000004,0x00000000,0x00000000,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x00000164 : */	0x78380000,0x00000000,
/*
    CALL scratch_to_dsa

at 0x00000166 : */	0x88080000,0x00000800,
/*

    ; Fix the update-next pointer so that the reconnect_dsa_head
    ; pointer is the one that will be updated if this DSA is a hit 
    ; and we remove it from the queue.

    MOVE MEMORY 4, reconnect_dsa_head, reselected_ok + 8

at 0x00000168 : */	0xc0000004,0x00000000,0x00000660,
/*

ENTRY reselected_check_next
reselected_check_next:
    ; Check for a NULL pointer.
    MOVE DSA0 TO SFBR

at 0x0000016b : */	0x72100000,0x00000000,
/*
    JUMP reselected_not_end, IF NOT 0

at 0x0000016d : */	0x80040000,0x000005f4,
/*
    MOVE DSA1 TO SFBR

at 0x0000016f : */	0x72110000,0x00000000,
/*
    JUMP reselected_not_end, IF NOT 0

at 0x00000171 : */	0x80040000,0x000005f4,
/*
    MOVE DSA2 TO SFBR

at 0x00000173 : */	0x72120000,0x00000000,
/*
    JUMP reselected_not_end, IF NOT 0

at 0x00000175 : */	0x80040000,0x000005f4,
/*
    MOVE DSA3 TO SFBR

at 0x00000177 : */	0x72130000,0x00000000,
/*
    JUMP reselected_not_end, IF NOT 0

at 0x00000179 : */	0x80040000,0x000005f4,
/*
    INT int_err_unexpected_reselect

at 0x0000017b : */	0x98080000,0x00020000,
/*

reselected_not_end:
    MOVE DSA0 TO SFBR

at 0x0000017d : */	0x72100000,0x00000000,
/*
    ;
    ; XXX the ALU is only eight bits wide, and the assembler
    ; wont do the dirt work for us.  As long as dsa_check_reselect
    ; is negative, we need to sign extend with 1 bits to the full
    ; 32 bit width of the address.
    ;
    ; A potential work around would be to have a known alignment 
    ; of the DSA structure such that the base address plus 
    ; dsa_check_reselect doesn't require carrying from bytes 
    ; higher than the LSB.
    ;

    MOVE SFBR + dsa_check_reselect TO SCRATCH0

at 0x0000017f : */	0x6e340000,0x00000000,
/*
    MOVE DSA1 TO SFBR

at 0x00000181 : */	0x72110000,0x00000000,
/*
    MOVE SFBR + 0xff TO SCRATCH1 WITH CARRY

at 0x00000183 : */	0x6f35ff00,0x00000000,
/*
    MOVE DSA2 TO SFBR

at 0x00000185 : */	0x72120000,0x00000000,
/*
    MOVE SFBR + 0xff TO SCRATCH2 WITH CARRY

at 0x00000187 : */	0x6f36ff00,0x00000000,
/*
    MOVE DSA3 TO SFBR

at 0x00000189 : */	0x72130000,0x00000000,
/*
    MOVE SFBR + 0xff TO SCRATCH3 WITH CARRY

at 0x0000018b : */	0x6f37ff00,0x00000000,
/*

    MOVE dmode_ncr_to_memory TO DMODE

at 0x0000018d : */	0x78380000,0x00000000,
/*
    MOVE MEMORY 4, addr_scratch, reselected_check + 4

at 0x0000018f : */	0xc0000004,0x00000000,0x00000654,
/*
    MOVE dmode_memory_to_memory TO DMODE

at 0x00000192 : */	0x78380000,0x00000000,
/*
reselected_check:
    JUMP 0

at 0x00000194 : */	0x80080000,0x00000000,
/*


;
;
reselected_ok:
    MOVE MEMORY 4, 0, 0				; Patched : first word

at 0x00000196 : */	0xc0000004,0x00000000,0x00000000,
/*
						; 	is this successful 
						; 	dsa_next
						; Second word is 
						;	unsuccessful dsa_next
    CLEAR ACK					; Accept last message

at 0x00000199 : */	0x60000040,0x00000000,
/*
    RETURN					; Return control to where

at 0x0000019b : */	0x90080000,0x00000000,
/*




selected:
    INT int_err_selected;

at 0x0000019d : */	0x98080000,0x00010000,
/*

;
; A select or reselect failure can be caused by one of two conditions : 
; 1.  SIG_P was set.  This will be the case if the user has written
;	a new value to a previously NULL head of the issue queue.
;
; 2.  The NCR53c810 was selected or reselected by another device.
; 

wait_reselect_failed:
; Reading CTEST2 clears the SIG_P bit in the ISTAT register.
    MOVE CTEST2 & 0x40 TO SFBR	

at 0x0000019f : */	0x741a4000,0x00000000,
/*
    JUMP selected, IF NOT 0x40

at 0x000001a1 : */	0x80040040,0x00000674,
/*
    JUMP schedule

at 0x000001a3 : */	0x80080000,0x00000130,
/*

select_failed:
    MOVE ISTAT & 0x20 TO SFBR

at 0x000001a5 : */	0x74142000,0x00000000,
/*
    JUMP reselected, IF NOT 0x20

at 0x000001a7 : */	0x80040020,0x00000568,
/*
    MOVE ISTAT & 0xdf TO ISTAT

at 0x000001a9 : */	0x7c14df00,0x00000000,
/*
    JUMP schedule

at 0x000001ab : */	0x80080000,0x00000130,
/*

;
; test_1
; test_2
;
; PURPOSE : run some verification tests on the NCR.  test_1
;	copies test_src to test_dest and interrupts the host
;	processor, testing for cache coherency and interrupt
; 	problems in the processes.
;
;	test_2 runs a command with offsets relative to the 
;	DSA on entry, and is useful for miscellaneous experimentation.
;

; Verify that interrupts are working correctly and that we don't 
; have a cache invalidation problem.

ABSOLUTE test_src = 0, test_dest = 0
ENTRY test_1
test_1:
    MOVE MEMORY 4, test_src, test_dest

at 0x000001ad : */	0xc0000004,0x00000000,0x00000000,
/*
    INT int_test_1

at 0x000001b0 : */	0x98080000,0x04000000,
/*

;
; Run arbitrary commands, with test code establishing a DSA
;
 
ENTRY test_2
test_2:
    CLEAR TARGET

at 0x000001b2 : */	0x60000200,0x00000000,
/*
    SELECT ATN FROM 0, test_2_fail

at 0x000001b4 : */	0x43000000,0x00000720,
/*
    JUMP test_2_msgout, WHEN MSG_OUT

at 0x000001b6 : */	0x860b0000,0x000006e0,
/*
ENTRY test_2_msgout
test_2_msgout:
    MOVE FROM 8, WHEN MSG_OUT

at 0x000001b8 : */	0x1e000000,0x00000008,
/*
    MOVE FROM 16, WHEN CMD 

at 0x000001ba : */	0x1a000000,0x00000010,
/*
    MOVE FROM 24, WHEN DATA_IN

at 0x000001bc : */	0x19000000,0x00000018,
/*
    MOVE FROM 32, WHEN STATUS

at 0x000001be : */	0x1b000000,0x00000020,
/*
    MOVE FROM 40, WHEN MSG_IN

at 0x000001c0 : */	0x1f000000,0x00000028,
/*
    MOVE SCNTL2 & 0x7f TO SCNTL2

at 0x000001c2 : */	0x7c027f00,0x00000000,
/*
    CLEAR ACK

at 0x000001c4 : */	0x60000040,0x00000000,
/*
    WAIT DISCONNECT

at 0x000001c6 : */	0x48000000,0x00000000,
/*
test_2_fail:
    INT int_test_2

at 0x000001c8 : */	0x98080000,0x04010000,
/*

ENTRY debug_break
debug_break:
    INT int_debug_break

at 0x000001ca : */	0x98080000,0x03000000,
/*

;
; initiator_abort
; target_abort
;
; PURPOSE : Abort the currently established nexus from with initiator
;	or target mode.
;
;  

ENTRY target_abort
target_abort:
    SET TARGET

at 0x000001cc : */	0x58000200,0x00000000,
/*
    DISCONNECT

at 0x000001ce : */	0x48000000,0x00000000,
/*
    CLEAR TARGET

at 0x000001d0 : */	0x60000200,0x00000000,
/*
    JUMP schedule

at 0x000001d2 : */	0x80080000,0x00000130,
/*
    
ENTRY initiator_abort
initiator_abort:
    SET ATN

at 0x000001d4 : */	0x58000008,0x00000000,
/*
; In order to abort the currently established nexus, we 
; need to source/sink up to one byte of data in any SCSI phase, 
; since the phase cannot change until REQ transitions 
; false->true
    JUMP no_eat_cmd, WHEN NOT CMD

at 0x000001d6 : */	0x82030000,0x00000768,
/*
    MOVE 1, NCR53c7xx_zero, WHEN CMD

at 0x000001d8 : */	0x0a000001,((unsigned long)&NCR53c7xx_zero),
/*
no_eat_cmd:
    JUMP no_eat_msg, WHEN NOT MSG_IN

at 0x000001da : */	0x87030000,0x00000778,
/*
    MOVE 1, NCR53c7xx_sink, WHEN MSG_IN

at 0x000001dc : */	0x0f000001,((unsigned long)&NCR53c7xx_sink),
/*
no_eat_msg:
    JUMP no_eat_data, WHEN NOT DATA_IN

at 0x000001de : */	0x81030000,0x00000788,
/*
    MOVE 1, NCR53c7xx_sink, WHEN DATA_IN

at 0x000001e0 : */	0x09000001,((unsigned long)&NCR53c7xx_sink),
/*
no_eat_data:
    JUMP no_eat_status, WHEN NOT STATUS

at 0x000001e2 : */	0x83030000,0x00000798,
/*
    MOVE 1, NCR53c7xx_sink, WHEN STATUS

at 0x000001e4 : */	0x0b000001,((unsigned long)&NCR53c7xx_sink),
/*
no_eat_status:
    JUMP no_source_data, WHEN NOT DATA_OUT

at 0x000001e6 : */	0x80030000,0x000007a8,
/*
    MOVE 1, NCR53c7xx_zero, WHEN DATA_OUT

at 0x000001e8 : */	0x08000001,((unsigned long)&NCR53c7xx_zero),
/*
no_source_data:
;
; If DSP points here, and a phase mismatch is encountered, we need to 
; do a bus reset.
;
    MOVE 1, NCR53c7xx_msg_abort, WHEN MSG_OUT

at 0x000001ea : */	0x0e000001,((unsigned long)&NCR53c7xx_msg_abort),
/*
    INT int_norm_aborted

at 0x000001ec : */	0x98080000,0x02040000,
/*

;
; dsa_to_scratch
; scratch_to_dsa
;
; PURPOSE :
; 	The NCR chips cannot do a move memory instruction with the DSA register 
; 	as the source or destination.  So, we provide a couple of subroutines
; 	that let us switch between the DSA register and scratch register.
;
; 	Memory moves to/from the DSPS  register also don't work, but we 
; 	don't use them.
;
;

 
dsa_to_scratch:
    MOVE DSA0 TO SFBR

at 0x000001ee : */	0x72100000,0x00000000,
/*
    MOVE SFBR TO SCRATCH0

at 0x000001f0 : */	0x6a340000,0x00000000,
/*
    MOVE DSA1 TO SFBR

at 0x000001f2 : */	0x72110000,0x00000000,
/*
    MOVE SFBR TO SCRATCH1

at 0x000001f4 : */	0x6a350000,0x00000000,
/*
    MOVE DSA2 TO SFBR

at 0x000001f6 : */	0x72120000,0x00000000,
/*
    MOVE SFBR TO SCRATCH2

at 0x000001f8 : */	0x6a360000,0x00000000,
/*
    MOVE DSA3 TO SFBR

at 0x000001fa : */	0x72130000,0x00000000,
/*
    MOVE SFBR TO SCRATCH3

at 0x000001fc : */	0x6a370000,0x00000000,
/*
    RETURN

at 0x000001fe : */	0x90080000,0x00000000,
/*

scratch_to_dsa:
    MOVE SCRATCH0 TO SFBR

at 0x00000200 : */	0x72340000,0x00000000,
/*
    MOVE SFBR TO DSA0

at 0x00000202 : */	0x6a100000,0x00000000,
/*
    MOVE SCRATCH1 TO SFBR

at 0x00000204 : */	0x72350000,0x00000000,
/*
    MOVE SFBR TO DSA1

at 0x00000206 : */	0x6a110000,0x00000000,
/*
    MOVE SCRATCH2 TO SFBR

at 0x00000208 : */	0x72360000,0x00000000,
/*
    MOVE SFBR TO DSA2

at 0x0000020a : */	0x6a120000,0x00000000,
/*
    MOVE SCRATCH3 TO SFBR

at 0x0000020c : */	0x72370000,0x00000000,
/*
    MOVE SFBR TO DSA3

at 0x0000020e : */	0x6a130000,0x00000000,
/*
    RETURN

at 0x00000210 : */	0x90080000,0x00000000,
};

#define A_addr_scratch	0x00000000
unsigned long A_addr_scratch_used[] = {
	0x00000007,
	0x0000003a,
	0x00000046,
	0x00000050,
	0x0000007a,
	0x000000a8,
	0x000000be,
	0x00000104,
	0x00000163,
	0x00000190,
};

#define A_addr_sfbr	0x00000000
unsigned long A_addr_sfbr_used[] = {
	0x00000016,
};

#define A_addr_temp	0x00000000
unsigned long A_addr_temp_used[] = {
	0x00000027,
};

#define A_dmode_memory_to_memory	0x00000000
unsigned long A_dmode_memory_to_memory_used[] = {
	0x00000008,
	0x00000019,
	0x00000029,
	0x0000003c,
	0x00000048,
	0x00000051,
	0x0000007c,
	0x000000aa,
	0x000000c0,
	0x00000106,
	0x00000164,
	0x00000192,
};

#define A_dmode_memory_to_ncr	0x00000000
unsigned long A_dmode_memory_to_ncr_used[] = {
	0x00000003,
	0x00000012,
	0x0000004c,
	0x0000015f,
};

#define A_dmode_ncr_to_memory	0x00000000
unsigned long A_dmode_ncr_to_memory_used[] = {
	0x00000024,
	0x00000037,
	0x00000043,
	0x00000077,
	0x000000a5,
	0x000000bb,
	0x00000101,
	0x0000018d,
};

#define A_dmode_ncr_to_ncr	0x00000000
unsigned long A_dmode_ncr_to_ncr_used[] = {
};

#define A_dsa_check_reselect	0x00000000
unsigned long A_dsa_check_reselect_used[] = {
	0x0000017f,
};

#define A_dsa_cmdout	0x00000048
unsigned long A_dsa_cmdout_used[] = {
	0x0000008c,
};

#define A_dsa_cmnd	0x00000038
unsigned long A_dsa_cmnd_used[] = {
};

#define A_dsa_datain	0x00000054
unsigned long A_dsa_datain_used[] = {
	0x000000b3,
};

#define A_dsa_dataout	0x00000050
unsigned long A_dsa_dataout_used[] = {
	0x0000009d,
};

#define A_dsa_end	0x00000070
unsigned long A_dsa_end_used[] = {
};

#define A_dsa_fields_start	0x00000024
unsigned long A_dsa_fields_start_used[] = {
};

#define A_dsa_msgin	0x00000058
unsigned long A_dsa_msgin_used[] = {
	0x00000147,
};

#define A_dsa_msgout	0x00000040
unsigned long A_dsa_msgout_used[] = {
	0x0000006c,
};

#define A_dsa_msgout_other	0x00000068
unsigned long A_dsa_msgout_other_used[] = {
	0x0000013f,
};

#define A_dsa_next	0x00000030
unsigned long A_dsa_next_used[] = {
	0x0000002f,
	0x0000006f,
};

#define A_dsa_select	0x0000003c
unsigned long A_dsa_select_used[] = {
	0x00000067,
};

#define A_dsa_status	0x00000060
unsigned long A_dsa_status_used[] = {
	0x00000143,
};

#define A_dsa_temp_dsa_next	0x00000000
unsigned long A_dsa_temp_dsa_next_used[] = {
	0x00000001,
	0x00000006,
	0x0000001c,
};

#define A_dsa_temp_jump_resume	0x00000000
unsigned long A_dsa_temp_jump_resume_used[] = {
	0x00000028,
};

#define A_dsa_temp_lun	0x00000000
unsigned long A_dsa_temp_lun_used[] = {
	0x00000017,
};

#define A_dsa_temp_sync	0x00000000
unsigned long A_dsa_temp_sync_used[] = {
	0x00000021,
};

#define A_dsa_temp_target	0x00000000
unsigned long A_dsa_temp_target_used[] = {
	0x00000010,
};

#define A_int_debug_break	0x03000000
unsigned long A_int_debug_break_used[] = {
	0x000001cb,
};

#define A_int_debug_dsa_loaded	0x03030000
unsigned long A_int_debug_dsa_loaded_used[] = {
};

#define A_int_debug_head	0x03050000
unsigned long A_int_debug_head_used[] = {
};

#define A_int_debug_idle	0x03020000
unsigned long A_int_debug_idle_used[] = {
};

#define A_int_debug_reselected	0x03040000
unsigned long A_int_debug_reselected_used[] = {
};

#define A_int_debug_scheduled	0x03010000
unsigned long A_int_debug_scheduled_used[] = {
};

#define A_int_err_check_condition	0x00030000
unsigned long A_int_err_check_condition_used[] = {
	0x00000157,
};

#define A_int_err_no_phase	0x00040000
unsigned long A_int_err_no_phase_used[] = {
};

#define A_int_err_selected	0x00010000
unsigned long A_int_err_selected_used[] = {
	0x0000019e,
};

#define A_int_err_unexpected_phase	0x00000000
unsigned long A_int_err_unexpected_phase_used[] = {
	0x00000084,
	0x0000008a,
	0x0000008e,
	0x00000092,
	0x000000c8,
	0x000000cc,
	0x000000ce,
	0x000000d0,
	0x0000010d,
};

#define A_int_err_unexpected_reselect	0x00020000
unsigned long A_int_err_unexpected_reselect_used[] = {
	0x0000017c,
};

#define A_int_msg_1	0x01020000
unsigned long A_int_msg_1_used[] = {
	0x000000e2,
	0x000000e4,
};

#define A_int_msg_sdtr	0x01010000
unsigned long A_int_msg_sdtr_used[] = {
	0x0000012d,
};

#define A_int_msg_wdtr	0x01000000
unsigned long A_int_msg_wdtr_used[] = {
	0x00000121,
};

#define A_int_norm_aborted	0x02040000
unsigned long A_int_norm_aborted_used[] = {
	0x000001ed,
};

#define A_int_norm_command_complete	0x02020000
unsigned long A_int_norm_command_complete_used[] = {
};

#define A_int_norm_disconnected	0x02030000
unsigned long A_int_norm_disconnected_used[] = {
};

#define A_int_norm_reselect_complete	0x02010000
unsigned long A_int_norm_reselect_complete_used[] = {
};

#define A_int_norm_reset	0x02050000
unsigned long A_int_norm_reset_used[] = {
};

#define A_int_norm_select_complete	0x02000000
unsigned long A_int_norm_select_complete_used[] = {
};

#define A_int_test_1	0x04000000
unsigned long A_int_test_1_used[] = {
	0x000001b1,
};

#define A_int_test_2	0x04010000
unsigned long A_int_test_2_used[] = {
	0x000001c9,
};

#define A_int_test_3	0x04020000
unsigned long A_int_test_3_used[] = {
};

#define A_issue_dsa_head	0x00000000
unsigned long A_issue_dsa_head_used[] = {
	0x0000004f,
	0x00000080,
};

#define A_msg_buf	0x00000000
unsigned long A_msg_buf_used[] = {
	0x000000d6,
	0x0000010f,
	0x00000119,
	0x0000011f,
	0x00000125,
	0x0000012b,
};

#define A_reconnect_dsa_head	0x00000000
unsigned long A_reconnect_dsa_head_used[] = {
	0x0000003f,
	0x00000047,
	0x00000162,
	0x00000169,
};

#define A_reselected_identify	0x00000000
unsigned long A_reselected_identify_used[] = {
	0x00000015,
	0x0000015b,
};

#define A_reselected_tag	0x00000000
unsigned long A_reselected_tag_used[] = {
	0x0000015e,
};

#define A_test_dest	0x00000000
unsigned long A_test_dest_used[] = {
	0x000001af,
};

#define A_test_src	0x00000000
unsigned long A_test_src_used[] = {
	0x000001ae,
};

#define Ent_accept_message	0x000004d8
#define Ent_cmdout_cmdout	0x0000022c
#define Ent_command_complete	0x00000508
#define Ent_command_complete_msgin	0x00000518
#define Ent_debug_break	0x00000728
#define Ent_dsa_code_check_reselect	0x00000038
#define Ent_dsa_code_template	0x00000000
#define Ent_dsa_code_template_end	0x000000b4
#define Ent_dsa_jump_resume	0x00000088
#define Ent_dsa_schedule	0x000000b4
#define Ent_dsa_zero	0x00000090
#define Ent_initiator_abort	0x00000750
#define Ent_msg_in	0x00000354
#define Ent_other_transfer	0x0000031c
#define Ent_reject_message	0x000004b8
#define Ent_reselected_check_next	0x000005ac
#define Ent_respond_message	0x00000000
#define Ent_schedule	0x00000130
#define Ent_select	0x00000194
#define Ent_select_msgout	0x000001ac
#define Ent_target_abort	0x00000730
#define Ent_test_1	0x000006b4
#define Ent_test_2	0x000006c8
#define Ent_test_2_msgout	0x000006e0
unsigned long LABELPATCHES[] = {
	0x00000002,
	0x0000000b,
	0x0000000d,
	0x0000001d,
	0x0000001f,
	0x0000002c,
	0x0000002e,
	0x0000003b,
	0x00000042,
	0x00000054,
	0x00000058,
	0x0000005c,
	0x00000060,
	0x00000064,
	0x00000068,
	0x0000006a,
	0x0000006e,
	0x0000007b,
	0x00000082,
	0x00000086,
	0x00000088,
	0x00000090,
	0x00000094,
	0x00000096,
	0x00000098,
	0x0000009a,
	0x0000009c,
	0x000000a9,
	0x000000ae,
	0x000000b2,
	0x000000bf,
	0x000000c4,
	0x000000ca,
	0x000000d2,
	0x000000d4,
	0x000000d8,
	0x000000da,
	0x000000dc,
	0x000000de,
	0x000000e0,
	0x000000e6,
	0x000000e8,
	0x00000105,
	0x00000111,
	0x00000113,
	0x00000115,
	0x0000011b,
	0x00000127,
	0x00000153,
	0x00000159,
	0x00000167,
	0x0000016a,
	0x0000016e,
	0x00000172,
	0x00000176,
	0x0000017a,
	0x00000191,
	0x000001a2,
	0x000001a4,
	0x000001a8,
	0x000001ac,
	0x000001b5,
	0x000001b7,
	0x000001d3,
	0x000001d7,
	0x000001db,
	0x000001df,
	0x000001e3,
	0x000001e7,
};

unsigned long INSTRUCTIONS	= 0x000000fe;
unsigned long PATCHES	= 0x00000045;
