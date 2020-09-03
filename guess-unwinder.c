/*
 * guess-unwinder.c - Best-guess unwinder
 * 
 * Copyright 2011 Sony Network Entertainment
 *
 * By Tim Bird <tim.bird@am.sony.com>
 *
 * How hard can it be to write an unwinder from scratch, using
 * guesswork and heuristics on the stack and code?...
 */
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include "utility.h"
#include "crash_handler.h"

/* test for branch and link */
#define is_ARM_bl(ins)	((ins & 0x0f000000) >> 24)==0x0b

#define is_ARM_b(ins)	((ins & 0x0f000000) >> 24)==0x0a

/* calculate the offset of the branch, from the instruction value
 * mask off lower 24 bits (the offset),
 * sign-extend by OR-ing with some 1 bits multiplied by the sign-bit
 * multiply by 4 (to convert to word address)
 * and add 8 (to account for instruction pipelining)
 */
#define branch_offset(ins) ((int)((unsigned int)((ins & 0x00ffffff) | \
	(0xff000000 * ((ins & 0x01000000) >> 24)))<<2)+8)

#define branch_target(p, ins)	((unsigned int)((int)p + branch_offset(ins)))

/* routine to determine if a word value represents a return address
 * This routine checks to see if the word value points to an address that
 * immediately follows a branch instruction.
 */
int is_ARM_return_address(int pid, mapinfo *milist, unsigned int value)
{
	unsigned int addr;
	unsigned int data;
	unsigned int rel_addr;
	const mapinfo *mi;

	/* check 4 bytes before the address */
	addr = value-4;

	/* FIXTHIS - check map to see if this is in text segment */
	rel_addr = addr;
	mi = pc_to_mapinfo(milist, addr, &rel_addr);
	if (mi == NULL) {
		/* address is not in the executable memory map */
		/* note that the [stack] map is not executable,
 		 * and has already been filtered from the map list
 		*/
		return 0;
	}

	DLOG("checking addr=0x%08lx for instruction\n", addr);

	data = ptrace(PTRACE_PEEKTEXT, pid, (void*)addr, NULL);

	/* detect failure to read data from process memory */
	if (data==0xffffffff) {
		return 0;
	}

	DLOG("instruction at %08lx is %08lx\n", addr, data);

	if (is_ARM_bl(data)) {	
		DLOG("this instruction is a branch and link, with offset %d and target 0x%08x\n",
			branch_offset(data), branch_target(addr, data));
		return 1;
	} else {
		return 0;
	}
}

/* guess_unwind_backtrace_with_ptrace()
 * returns number of frames found
 */

int guess_unwind_backtrace_with_ptrace(int pid, mapinfo *milist,
	unsigned int sp_list[], int *frame0_pc_sane)
{
	struct pt_regs r;

	unsigned int sp, pc, lr, fp, p, addr, data;
	unsigned int func_addr, exec_addr;
	unsigned int stack_size;
	int frame_no;
	int i;

	/* move up stack looking at addresses */

	/* set up regs for backtrace */
	if (ptrace(PTRACE_GETREGS, pid, 0, &r)) return;
	sp = r.ARM_sp;
	pc = r.ARM_pc;
	lr = r.ARM_lr;
	fp = r.ARM_fp;

	/* find out if LR points to an instruction after a BL */
	DLOG("lr=0x%08x\n", lr);

	/* check 4 bytes before the address */
	if (is_ARM_return_address(pid, milist, lr)) {
		DLOG("lr points to a branch and link instruction\n");
	} else {
		DLOG("lr doesn't point to a branch link instruction\n");
		return 0;
	}

	addr = lr-4;
	data = ptrace(PTRACE_PEEKTEXT, pid, (void*)addr, NULL);

	DLOG("addr+offset = 0x%08x\n", (int)(addr) + branch_offset(data));

	/* determine function called by branch */
	func_addr = branch_target(addr, data);
	DLOG("called function is: 0x%08lx\n", func_addr);
	exec_addr = pc;
	DLOG("execution offset in function is: %08lx + 0x%x\n", func_addr,
		exec_addr-func_addr);

	LOG("Crash occured at PC: 0x%08x\n", pc);
	exec_addr = pc;
	frame_no = 0;

	LOG("#%d:0x%08lx in function 0x%08lx at offset 0x%lx\n",
		frame_no, exec_addr, func_addr, exec_addr-func_addr);

	/* start of loop */
	/* desired output format(final):
	 * <frame number>:<execution addr> in function <function_name> at <line_no>
	 * desired output format(unprocessed):
	 * <frame number>:<execution addr> in function <function_addr> at offset 
	 */

	/* scan stack looking for return addresses */
	stack_size = stack_map.end - sp;
	for ( i=0; i<(stack_size/4); i++ ) {
		data = ptrace(PTRACE_PEEKTEXT, pid, (void*)sp, NULL);
		DLOG("checking value 0x%08lx at stack position 0x%08lx\n", data, sp);
		if (is_ARM_return_address(pid, milist, data)) {
			DLOG("at sp=%08lx: possible return address 0x%08lx on stack\n",
				sp, data);

			addr = data-4;
			data = ptrace(PTRACE_PEEKTEXT, pid, (void*)addr, NULL);

			/* determine function called by branch */
			func_addr = branch_target(addr, data);
			DLOG("called function is: 0x%08lx\n", func_addr);
			exec_addr = addr;
			DLOG("execution offset in function is: %08lx + 0x%x\n", func_addr,
				exec_addr-func_addr);

			frame_no++;

			/* FIXTHIS - use pc_to_mapinfo() to find the section name */

			LOG("#%d:0x%08lx in function 0x%08lx at offset 0x%lx\n",
				frame_no, exec_addr, func_addr, exec_addr-func_addr);
		}
		sp += 4;
	}

	return frame_no;
}
	
