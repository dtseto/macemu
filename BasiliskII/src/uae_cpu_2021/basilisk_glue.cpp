/*
 *  basilisk_glue.cpp - Glue UAE CPU to Basilisk II CPU engine interface
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "emul_op.h"
#include "rom_patches.h"
#include "timer.h"
#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "compiler/compemu.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


// RAM and ROM pointers
uint32 RAMBaseMac = 0;		// RAM base (Mac address space) gb-- initializer is important
uint8 *RAMBaseHost;			// RAM base (host address space)
uint32 RAMSize;				// Size of RAM
uint32 ROMBaseMac;			// ROM base (Mac address space)
uint8 *ROMBaseHost;			// ROM base (host address space)
uint32 ROMSize;				// Size of ROM

#if !REAL_ADDRESSING
// Mac frame buffer
uint8 *MacFrameBaseHost;	// Frame buffer base (host address space)
uint32 MacFrameSize;		// Size of frame buffer
int MacFrameLayout;			// Frame buffer layout
#endif

#if DIRECT_ADDRESSING
uintptr MEMBaseDiff;		// Global offset between a Mac address and its Host equivalent
#endif

#if USE_JIT
bool UseJIT = false;
#endif

// #if defined(ENABLE_EXCLUSIVE_SPCFLAGS) && !defined(HAVE_HARDWARE_LOCKS)
B2_mutex *spcflags_lock = NULL;
// #endif

// From newcpu.cpp
extern int quit_program;


/*
 *  Initialize 680x0 emulation, CheckROM() must have been called first
 */

bool Init680x0(void)
{
	spcflags_lock = B2_create_mutex();
#if REAL_ADDRESSING
	// Mac address space = host address space
	RAMBaseMac = (uintptr)RAMBaseHost;
	ROMBaseMac = (uintptr)ROMBaseHost;
#elif DIRECT_ADDRESSING
	// Mac address space = host address space minus constant offset (MEMBaseDiff)
	// NOTE: MEMBaseDiff is set up in main_unix.cpp/main()
	RAMBaseMac = 0;
	ROMBaseMac = Host2MacAddr(ROMBaseHost);
#else
	// Initialize UAE memory banks
	RAMBaseMac = 0;
	switch (ROMVersion) {
		case ROM_VERSION_64K:
		case ROM_VERSION_PLUS:
		case ROM_VERSION_CLASSIC:
			ROMBaseMac = 0x00400000;
			break;
		case ROM_VERSION_II:
			ROMBaseMac = 0x00a00000;
			break;
		case ROM_VERSION_32:
			ROMBaseMac = 0x40800000;
			break;
		default:
			return false;
	}
	memory_init();
#endif

	init_m68k();
#if USE_JIT
	UseJIT = compiler_use_jit();
	if (UseJIT)
	    compiler_init();
#endif
	return true;
}


/*
 *  Deinitialize 680x0 emulation
 */

void Exit680x0(void)
{
#if USE_JIT
    if (UseJIT)
	compiler_exit();
#endif
	exit_m68k();
}


/*
 *  Initialize memory mapping of frame buffer (called upon video mode change)
 */

void InitFrameBufferMapping(void)
{
#if !REAL_ADDRESSING && !DIRECT_ADDRESSING
	memory_init();
#endif
}

/*
 *  Reset and start 680x0 emulation (doesn't return)
 */

static bool run_opcode_test_mode()
{
    const char *hex = getenv("B2_TEST_HEX");
    if (!(hex && *hex))
        return false;

    uint16 words[1024];
    size_t word_count = 0;
    const char *cursor = hex;
    while (*cursor) {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',' || *cursor == ';' || *cursor == ':'))
            cursor++;
        if (!*cursor)
            break;
        if (word_count >= lengthof(words)) {
            fprintf(stderr, "B2_TEST_HEX has too many words\\n");
            quit_program = 1;
            return true;
        }
        char *end = NULL;
        unsigned long value = strtoul(cursor, &end, 16);
        if (end == cursor || value > 0xffff) {
            fprintf(stderr, "B2_TEST_HEX parse failed\\n");
            quit_program = 1;
            return true;
        }
        words[word_count++] = (uint16)value;
        cursor = end;
    }
    if (word_count == 0) {
        fprintf(stderr, "B2_TEST_HEX is empty\\n");
        quit_program = 1;
        return true;
    }

    const uaecptr test_addr = RAMBaseMac + 0x1000;
    const uaecptr stack_addr = RAMBaseMac + RAMSize - 0x2000;
    for (size_t i = 0; i < word_count; i++)
        put_word(test_addr + (uaecptr)(i * 2), words[i]);
    put_word(test_addr + (uaecptr)(word_count * 2), M68K_EXEC_RETURN);

    for (int i = 0; i < 8; i++) {
        m68k_dreg(regs, i) = 0;
        m68k_areg(regs, i) = 0;
    }
    m68k_areg(regs, 7) = stack_addr;
    regs.usp = regs.isp = regs.msp = stack_addr;
    regs.sr = 0x2700;

    const char *init = getenv("B2_TEST_INIT");
    if (init && *init) {
        uint32 init_values[17];
        size_t init_count = 0;
        cursor = init;
        while (*cursor) {
            while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',' || *cursor == ';' || *cursor == ':'))
                cursor++;
            if (!*cursor)
                break;
            if (init_count >= lengthof(init_values))
                break;
            char *end = NULL;
            unsigned long value = strtoul(cursor, &end, 16);
            if (end == cursor || value > 0xffffffffUL) {
                fprintf(stderr, "B2_TEST_INIT parse failed\\n");
                quit_program = 1;
                return true;
            }
            init_values[init_count++] = (uint32)value;
            cursor = end;
        }
        if (init_count != 16 && init_count != 17) {
            fprintf(stderr, "B2_TEST_INIT needs 16 or 17 values\\n");
            quit_program = 1;
            return true;
        }
        for (int i = 0; i < 8; i++)
            m68k_dreg(regs, i) = init_values[i];
        for (int i = 0; i < 8; i++)
            m68k_areg(regs, i) = init_values[8 + i];
        if (init_count == 17)
            regs.sr = (uint16)(init_values[16] & 0xffff);
        regs.usp = regs.isp = regs.msp = m68k_areg(regs, 7);
    }

    MakeFromSR();
    regs.stopped = 0;
    SPCFLAGS_CLEAR(SPCFLAG_STOP | SPCFLAG_BRK | SPCFLAG_DOTRACE | SPCFLAG_TRACE);
    m68k_setpc(test_addr);
    fill_prefetch_0();
    quit_program = 0;
#if USE_JIT
    if (UseJIT)
        m68k_compile_execute();
    else
#endif
        m68k_execute();

    MakeSR();
    fprintf(stderr,
        "REGDUMP: D0=%08x D1=%08x D2=%08x D3=%08x D4=%08x D5=%08x D6=%08x D7=%08x "
        "A0=%08x A1=%08x A2=%08x A3=%08x A4=%08x A5=%08x A6=%08x A7=%08x SR=%04x\\n",
        (unsigned)m68k_dreg(regs, 0), (unsigned)m68k_dreg(regs, 1),
        (unsigned)m68k_dreg(regs, 2), (unsigned)m68k_dreg(regs, 3),
        (unsigned)m68k_dreg(regs, 4), (unsigned)m68k_dreg(regs, 5),
        (unsigned)m68k_dreg(regs, 6), (unsigned)m68k_dreg(regs, 7),
        (unsigned)m68k_areg(regs, 0), (unsigned)m68k_areg(regs, 1),
        (unsigned)m68k_areg(regs, 2), (unsigned)m68k_areg(regs, 3),
        (unsigned)m68k_areg(regs, 4), (unsigned)m68k_areg(regs, 5),
        (unsigned)m68k_areg(regs, 6), (unsigned)m68k_areg(regs, 7),
        (unsigned)regs.sr);
    quit_program = 1;
    return true;
}

void Start680x0(void)
{
	m68k_reset();
	if (run_opcode_test_mode())
		return;
#if USE_JIT
    if (UseJIT)
	m68k_compile_execute();
    else
#endif
	m68k_execute();
}


/*
 *  Trigger interrupt
 */

void TriggerInterrupt(void)
{
	idle_resume();
	SPCFLAGS_SET( SPCFLAG_INT );
}

void TriggerNMI(void)
{
	//!! not implemented yet
	// SPCFLAGS_SET( SPCFLAG_BRK ); // use _BRK for NMI
}


/*
 *  Get 68k interrupt level
 */

int intlev(void)
{
	return InterruptFlags ? 1 : 0;
}


/*
 *  Execute MacOS 68k trap
 *  r->a[7] and r->sr are unused!
 */

void Execute68kTrap(uint16 trap, struct M68kRegisters *r)
{
	int i;

	// Save old PC
	uaecptr oldpc = m68k_getpc();

	// Set registers
	for (i=0; i<8; i++)
		m68k_dreg(regs, i) = r->d[i];
	for (i=0; i<7; i++)
		m68k_areg(regs, i) = r->a[i];

	// Push trap and EXEC_RETURN on stack
	m68k_areg(regs, 7) -= 2;
	put_word(m68k_areg(regs, 7), M68K_EXEC_RETURN);
	m68k_areg(regs, 7) -= 2;
	put_word(m68k_areg(regs, 7), trap);

	// Execute trap
	m68k_setpc(m68k_areg(regs, 7));
	fill_prefetch_0();
	quit_program = 0;
	m68k_execute();

	// Clean up stack
	m68k_areg(regs, 7) += 4;

	// Restore old PC
	m68k_setpc(oldpc);
	fill_prefetch_0();

	// Get registers
	for (i=0; i<8; i++)
		r->d[i] = m68k_dreg(regs, i);
	for (i=0; i<7; i++)
		r->a[i] = m68k_areg(regs, i);
	quit_program = 0;
}


/*
 *  Execute 68k subroutine
 *  The executed routine must reside in UAE memory!
 *  r->a[7] and r->sr are unused!
 */

void Execute68k(uint32 addr, struct M68kRegisters *r)
{
	int i;

	// Save old PC
	uaecptr oldpc = m68k_getpc();

	// Set registers
	for (i=0; i<8; i++)
		m68k_dreg(regs, i) = r->d[i];
	for (i=0; i<7; i++)
		m68k_areg(regs, i) = r->a[i];

	// Push EXEC_RETURN and faked return address (points to EXEC_RETURN) on stack
	m68k_areg(regs, 7) -= 2;
	put_word(m68k_areg(regs, 7), M68K_EXEC_RETURN);
	m68k_areg(regs, 7) -= 4;
	put_long(m68k_areg(regs, 7), m68k_areg(regs, 7) + 4);

	// Execute routine
	m68k_setpc(addr);
	fill_prefetch_0();
	quit_program = 0;
	m68k_execute();

	// Clean up stack
	m68k_areg(regs, 7) += 2;

	// Restore old PC
	m68k_setpc(oldpc);
	fill_prefetch_0();

	// Get registers
	for (i=0; i<8; i++)
		r->d[i] = m68k_dreg(regs, i);
	for (i=0; i<7; i++)
		r->a[i] = m68k_areg(regs, i);
	quit_program = 0;
}

void report_double_bus_error()
{
#if 0
	panicbug("CPU: Double bus fault detected !");
	/* would be cool to open SDL dialog here: */
	/* [Double bus fault detected. The emulated system crashed badly.
	    Do you want to reset ARAnyM or quit ?] [Reset] [Quit]"
	*/
	panicbug(CPU_MSG);
	CPU_ACTION;
#endif
}
