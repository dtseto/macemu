/*
 * UAE - The Un*x Amiga Emulator
 *
 * memory management
 *
 * Copyright 1995 Bernd Schmidt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef UAE_MEMORY_H
#define UAE_MEMORY_H

#include "registers.h"

#if DIRECT_ADDRESSING
extern uintptr MEMBaseDiff;
#endif

extern void Exception (int, uaecptr);

static __inline__ bool trace_write_window_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (getenv("B2_TRACE_WRITE_START") && *getenv("B2_TRACE_WRITE_START")) ? 1 : 0;
    return cached != 0;
}

static __inline__ uae_u32 trace_write_window_start(void)
{
    static uae_u32 value = 0;
    static bool init = false;
    if (!init) {
        const char *env = getenv("B2_TRACE_WRITE_START");
        value = env && *env ? (uae_u32)strtoul(env, NULL, 0) : 0;
        init = true;
    }
    return value;
}

static __inline__ uae_u32 trace_write_window_end(void)
{
    static uae_u32 value = 0xffffffff;
    static bool init = false;
    if (!init) {
        const char *env = getenv("B2_TRACE_WRITE_END");
        value = env && *env ? (uae_u32)strtoul(env, NULL, 0) : 0xffffffff;
        init = true;
    }
    return value;
}

static __inline__ unsigned long trace_write_limit(void)
{
    static unsigned long value = 200;
    static bool init = false;
    if (!init) {
        const char *env = getenv("B2_TRACE_WRITE_LIMIT");
        value = env && *env ? strtoul(env, NULL, 0) : 200;
        init = true;
    }
    return value;
}

static __inline__ void trace_write_log(const char *kind, uaecptr addr, uae_u32 val)
{
    static unsigned long count = 0;
    if (!trace_write_window_enabled())
        return;
    if (addr < trace_write_window_start() || addr > trace_write_window_end())
        return;
    if (count >= trace_write_limit())
        return;
    count++;
    fprintf(stderr, "TRACEWRITE %s step=%lu pc=%08x addr=%08x val=%08x\n",
        kind, count, (unsigned)regs.fault_pc, (unsigned)addr, (unsigned)val);
}

/* Auto-select longjmp-based exceptions when C++ exceptions are disabled
   (e.g. -fno-exceptions). GCC/Clang undefine __EXCEPTIONS in that case. */
#if defined(__GNUC__) && !defined(__EXCEPTIONS) && !defined(EXCEPTIONS_VIA_LONGJMP)
#define EXCEPTIONS_VIA_LONGJMP 1
#endif

#ifdef EXCEPTIONS_VIA_LONGJMP
    #include <setjmp.h>
    #ifndef JMP_BUF
    #define JMP_BUF  sigjmp_buf
    #define SETJMP(env) sigsetjmp(env, 0)
    #define LONGJMP  siglongjmp
    #endif
    extern JMP_BUF excep_env;
    #define SAVE_EXCEPTION \
        JMP_BUF excep_env_old; \
        memcpy(excep_env_old, excep_env, sizeof(JMP_BUF))
    #define RESTORE_EXCEPTION \
        memcpy(excep_env, excep_env_old, sizeof(JMP_BUF))
    #define TRY(var) int var = SETJMP(excep_env); if (!var)
    #define CATCH(var) else
    #define THROW(n) LONGJMP(excep_env, n)
    #define THROW_AGAIN(var) LONGJMP(excep_env, var)
    #define VOLATILE volatile
#else
    struct m68k_exception {
        int prb;
        m68k_exception (int exc) : prb (exc) {}
        operator int() { return prb; }
    };
    #define SAVE_EXCEPTION
    #define RESTORE_EXCEPTION
    #define TRY(var) try
    #define CATCH(var) catch(m68k_exception var)
    #define THROW(n) throw m68k_exception(n)
    #define THROW_AGAIN(var) throw
    #define VOLATILE
#endif /* EXCEPTIONS_VIA_LONGJMP */

#if DIRECT_ADDRESSING
/*
 * Fast RAM mapping used by the interpreter.  These are host pointers and must
 * remain pointer-sized on arm64; the guest address itself remains 32-bit.
 */
extern uae_u8 *fast_ram_base;
extern uae_u32 fast_ram_size;

static __inline__ bool fast_ram_contains(uaecptr addr, size_t size)
{
    return fast_ram_base != NULL && addr <= fast_ram_size &&
        size <= (size_t)(fast_ram_size - addr);
}

static __inline__ uae_u32 slow_get_long(uaecptr addr)
{
    uae_u32 raw;
    memcpy(&raw, (const uae_u8 *)MEMBaseDiff + addr, sizeof(raw));
    return __builtin_bswap32(raw);
}

static __inline__ uae_u32 slow_get_word(uaecptr addr)
{
    uae_u16 raw;
    memcpy(&raw, (const uae_u8 *)MEMBaseDiff + addr, sizeof(raw));
    return __builtin_bswap16(raw);
}

static __inline__ void slow_put_long(uaecptr addr, uae_u32 value)
{
    const uae_u32 raw = __builtin_bswap32(value);
    memcpy((uae_u8 *)MEMBaseDiff + addr, &raw, sizeof(raw));
}

static __inline__ void fast_put_word(uaecptr addr, uae_u32 value)
{
    if (__builtin_expect(fast_ram_contains(addr, sizeof(uae_u16)), 1)) {
        const uae_u16 raw = __builtin_bswap16((uae_u16)value);
        memcpy(fast_ram_base + addr, &raw, sizeof(raw));
        return;
    }
    const uae_u16 raw = __builtin_bswap16((uae_u16)value);
    memcpy((uae_u8 *)MEMBaseDiff + addr, &raw, sizeof(raw));
}

static __inline__ uae_u32 fast_get_long(uaecptr addr)
{
    if (__builtin_expect(fast_ram_contains(addr, sizeof(uae_u32)), 1)) {
        uae_u32 raw;
        memcpy(&raw, fast_ram_base + addr, sizeof(raw));
        return __builtin_bswap32(raw);
    }
    return slow_get_long(addr);
}

static __inline__ uae_u32 fast_get_word(uaecptr addr)
{
    if (__builtin_expect(fast_ram_contains(addr, sizeof(uae_u16)), 1)) {
        uae_u16 raw;
        memcpy(&raw, fast_ram_base + addr, sizeof(raw));
        return __builtin_bswap16(raw);
    }
    return slow_get_word(addr);
}

static __inline__ void fast_put_long(uaecptr addr, uae_u32 value)
{
    if (__builtin_expect(fast_ram_contains(addr, sizeof(uae_u32)), 1)) {
        const uae_u32 raw = __builtin_bswap32(value);
        memcpy(fast_ram_base + addr, &raw, sizeof(raw));
        return;
    }
    slow_put_long(addr, value);
}

/* Opt-in startup check for the exact arm64 hazards this path must avoid. */
static __inline__ void fast_memory_selftest(void)
{
    const char *enabled = getenv("B2_FAST_MEMORY_SELFTEST");
    if (!enabled || !enabled[0] || strcmp(enabled, "0") == 0)
        return;

    uae_u8 *saved_base = fast_ram_base;
    const uae_u32 saved_size = fast_ram_size;
    uae_u8 storage[9] = { 0 };
    fast_ram_base = storage + 1;
    fast_ram_size = 8;
    fast_put_long(1, 0x12345678u);
    const bool ok = fast_get_long(1) == 0x12345678u &&
        storage[2] == 0x12 && storage[3] == 0x34 &&
        storage[4] == 0x56 && storage[5] == 0x78;
    fast_ram_base = saved_base;
    fast_ram_size = saved_size;
    fprintf(stderr, "B2_FAST_MEMORY_SELFTEST %s (unaligned/big-endian)\\n",
        ok ? "passed" : "FAILED");
}

static __inline__ uae_u8 *do_get_real_address(uaecptr addr)
{
	return (uae_u8 *)MEMBaseDiff + addr;
}
static __inline__ uae_u32 do_get_virtual_address(uae_u8 *addr)
{
	return (uintptr)addr - MEMBaseDiff;
}
/* Low NuBus addresses which alias the host JIT cache/reservation must never
   expose or modify native code.  With more than 128 MiB of guest RAM this
   range is ordinary RAM, matching the mapping policy in main_unix.cpp. */
#define LOW_NUBUS_OPEN_BUS_START 0x0a014000u
#define LOW_NUBUS_OPEN_BUS_END   0x0a815000u
extern uint32 RAMSize;
static __inline__ bool is_low_nubus_open_bus_gap(uaecptr addr)
{
    return RAMSize <= 0x08000000u &&
        addr >= LOW_NUBUS_OPEN_BUS_START && addr < LOW_NUBUS_OPEN_BUS_END;
}
static __inline__ uae_u32 get_long(uaecptr addr)
{
    if (is_low_nubus_open_bus_gap(addr))
        return 0xffffffffu;
    return fast_get_long(addr);
}
#define phys_get_long get_long
static __inline__ uae_u32 get_word(uaecptr addr)
{
    if (is_low_nubus_open_bus_gap(addr))
        return 0xffffu;
    return fast_get_word(addr);
}
#define phys_get_word get_word
static __inline__ uae_u32 fake_50f_status_byte(uaecptr addr, bool *handled)
{
    static int init = 0;
    static int fake_14800 = 0;
    static int fake_01c00 = 0;
    if (!init) {
        const char *e1 = getenv("B2_FAKE_50F14800");
        const char *e2 = getenv("B2_FAKE_50F01C00");
        fake_14800 = e1 && *e1 ? (int)strtol(e1, NULL, 0) : -1;
        fake_01c00 = e2 && *e2 ? (int)strtol(e2, NULL, 0) : -1;
        init = 1;
    }
    if (fake_14800 >= 0 && addr == 0x50f14800) {
        *handled = true;
        return (uae_u32)(fake_14800 & 0xff);
    }
    if (fake_01c00 >= 0 && addr == 0x50f01c00) {
        *handled = true;
        return (uae_u32)(fake_01c00 & 0xff);
    }
    *handled = false;
    return 0;
}
static __inline__ bool is_50f_scanner_status(uaecptr addr)
{
    return (addr & 0xff001fff) == 0x50000002;
}
static __inline__ bool is_50f_scanner_data(uaecptr addr)
{
    return (addr & 0xff001fff) == 0x50000006;
}
static __inline__ uae_u32 get_byte(uaecptr addr)
{
    if (is_low_nubus_open_bus_gap(addr))
        return 0xffu;
    bool handled = false;
    uae_u32 fake = fake_50f_status_byte(addr, &handled);
    if (handled)
        return fake;
    uae_u8 * const m = (uae_u8 *)do_get_real_address(addr);
    uae_u32 v = do_get_mem_byte(m);
    if (is_50f_scanner_status(addr))
        do_put_mem_byte(m, 0);
    return v;
}
#define phys_get_byte get_byte
#if USE_JIT && (defined(CPU_AARCH64) || defined(CPU_aarch64))
extern void jit_notify_guest_memory_write(uae_u32 address, uae_u32 size);
#define JIT_NOTIFY_GUEST_WRITE(addr, size) jit_notify_guest_memory_write((addr), (size))
#else
#define JIT_NOTIFY_GUEST_WRITE(addr, size) do { } while (0)
#endif

static __inline__ void put_long(uaecptr addr, uae_u32 l)
{
    if (trace_write_window_enabled())
        trace_write_log("L", addr, l);
    if (is_low_nubus_open_bus_gap(addr) || addr == 0x5ffffffc)
        return;
    fast_put_long(addr, l);
    JIT_NOTIFY_GUEST_WRITE(addr, 4);
}
#define phys_put_long put_long
static __inline__ void put_word(uaecptr addr, uae_u32 w)
{
    if (trace_write_window_enabled())
        trace_write_log("W", addr, w);
    if (is_low_nubus_open_bus_gap(addr))
        return;
    fast_put_word(addr, w);
    JIT_NOTIFY_GUEST_WRITE(addr, 2);
}
#define phys_put_word put_word
static __inline__ void put_byte(uaecptr addr, uae_u32 b)
{
    if (trace_write_window_enabled())
        trace_write_log("B", addr, b);
    if (is_low_nubus_open_bus_gap(addr) || is_50f_scanner_data(addr))
        return;
    uae_u8 * const m = (uae_u8 *)do_get_real_address(addr);
    do_put_mem_byte(m, b);
    JIT_NOTIFY_GUEST_WRITE(addr, 1);
}
#define phys_put_byte put_byte
static __inline__ uae_u8 *get_real_address(uaecptr addr)
{
	return do_get_real_address(addr);
}
static inline uae_u8 *get_real_address(uaecptr addr, int write, int sz)
{
    return do_get_real_address(addr);
}
static inline uae_u8 *phys_get_real_address(uaecptr addr)
{
    return do_get_real_address(addr);
}
static __inline__ uae_u32 get_virtual_address(uae_u8 *addr)
{
	return do_get_virtual_address(addr);
}
#endif /* DIRECT_ADDRESSING */

static __inline__ void check_ram_boundary(uaecptr addr, int size, bool write) {}
static inline void flush_internals() {}

#endif /* MEMORY_H */

