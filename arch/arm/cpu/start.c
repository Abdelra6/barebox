/*
 * start-arm.c
 *
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <common.h>
#include <init.h>
#include <asm/barebox-arm.h>
#include <asm/system.h>
#include <asm-generic/memory_layout.h>

void __naked __section(.text_entry) exception_vectors(void)
{
	__asm__ __volatile__ (
		"b reset\n"				/* reset */
		"ldr pc, =undefined_instruction\n"	/* undefined instruction */
		"ldr pc, =software_interrupt\n"		/* software interrupt (SWI) */
		"ldr pc, =prefetch_abort\n"		/* prefetch abort */
		"ldr pc, =data_abort\n"			/* data abort */
		"ldr pc, =not_used\n"			/* (reserved) */
		"ldr pc, =irq\n"			/* irq (interrupt) */
		"ldr pc, =fiq\n"			/* fiq (fast interrupt) */
	);
}

extern char __bss_start, _end;

/*
 * The actual reset vector. This code is position independent and usually
 * does not run at the address it's linked at.
 */
void __naked __bare_init reset(void)
{
	uint32_t r;

	/* set the cpu to SVC32 mode */
	__asm__ __volatile__("mrs %0, cpsr":"=r"(r));
	r &= ~0x1f;
	r |= 0xd3;
	__asm__ __volatile__("msr cpsr, %0" : : "r"(r));

	__asm__ __volatile__ (
		"bl __mmu_cache_flush;"
		:
		:
		: "r0", "r1", "r2", "r3", "r6", "r10", "r12", "cc", "memory"
	);

	/* disable MMU stuff and caches */
	r = get_cr();
	r &= ~(CR_M | CR_C | CR_B | CR_S | CR_R | CR_V);
	r |= CR_A | CR_I;
	set_cr(r);

#ifdef CONFIG_MACH_DO_LOWLEVEL_INIT
	board_init_lowlevel();
#endif
	board_init_lowlevel_return();
}

/*
 * Board code can jump here by either returning from board_init_lowlevel
 * or by calling this funtion directly.
 */
void __naked __bare_init board_init_lowlevel_return(void)
{
	uint32_t r;

	/* Setup the stack */
	r = STACK_BASE + STACK_SIZE - 16;
	__asm__ __volatile__("mov sp, %0" : : "r"(r));

	/* Get runtime address of this function */
	__asm__ __volatile__("adr %0, 0":"=r"(r));

	/* Get start of binary image */
	r -= (uint32_t)&board_init_lowlevel_return - TEXT_BASE;

	/* relocate to link address if necessary */
	if (r != TEXT_BASE)
		memcpy((void *)TEXT_BASE, (void *)r,
				(unsigned int)&__bss_start - TEXT_BASE);

	/* clear bss */
	memset(&__bss_start, 0, &_end - &__bss_start);

	/* call start_barebox with its absolute address */
	r = (unsigned int)&start_barebox;
	__asm__ __volatile__("mov pc, %0" : : "r"(r));
}

