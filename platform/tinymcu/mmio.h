/*
 * platform/tinymcu/mmio.h
 * CP/M Neo - memory-mapped I/O register definitions for TinyMCU
 *
 * Shared by the kernel, libc, and BIOS. Never include from user programs.
 *
 * Unlike platform/vemu (a flat, 0-based emulated address space with the
 * I/O window packed into the top of RAM), TinyMCU is real hardware with a
 * fixed, separate peripheral bus window at PERIPHERALS_BASE, see
 * rtl/tinymcu_pkg.vhd, the authoritative source for every address below.
 * There is no __io_base/IO_BASE indirection here: these addresses never
 * move, regardless of --mem.
 *
 * TinyMCU has no DMA controller, no memory-mapped display, and no
 * separate keyboard controller. The console is the UART (TX_DATA/
 * RX_DATA), and bios_read()/bios_write() are backed by a plain RAM buffer
 * in platform/tinymcu/bios.c, not a hardware disk/DMA register.
 */

#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>
#include "kernel_abi.h"

/* MMIO access macros */

/*
 * All addresses are passed as integers (uint32_t register addresses).
 * Casting an integer directly to a pointer of a different width triggers
 * -Wint-to-pointer-cast on RV32 because pointers are 32 bits wide.
 * The correct idiom is to widen to uintptr_t first, which matches the
 * pointer width on every target, and then cast to the access type.
 */
#define MMIO_W32(addr, val) __asm__ volatile("sw %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint32_t)(val)) : "memory")

#define MMIO_W16(addr, val) __asm__ volatile("sh %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint16_t)(val)) : "memory")

#define MMIO_W8(addr, val) __asm__ volatile("sb %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint8_t)(val)) : "memory")

#define MMIO_R32(addr)                                                                                                 \
    ({                                                                                                                 \
        uintptr_t _a = (uintptr_t)(addr);                                                                              \
        uint32_t  _v;                                                                                                  \
        __asm__ volatile("lw %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                                              \
        _v;                                                                                                            \
    })

#define MMIO_R16(addr)                                                                                                 \
    ({                                                                                                                 \
        uintptr_t _a = (uintptr_t)(addr);                                                                              \
        uint16_t  _v;                                                                                                  \
        __asm__ volatile("lhu %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                                             \
        _v;                                                                                                            \
    })

#define MMIO_R8(addr)                                                                                                  \
    ({                                                                                                                 \
        uintptr_t _a = (uintptr_t)(addr);                                                                              \
        uint8_t   _v;                                                                                                  \
        __asm__ volatile("lbu %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                                             \
        _v;                                                                                                            \
    })

/* Peripheral base addresses (rtl/tinymcu_pkg.vhd) */

#define TINYMCU_GPIO_BASE  0x04000000u
#define TINYMCU_TIMER_BASE 0x04000100u
#define TINYMCU_UART_BASE  0x04000200u

/*
 * UART registers (rtl/peripherals/tinymcu_periph_uart.vhd). This is the
 * BIOS console: bios_conout()/bios_conin()/bios_const() poll STATUS and
 * read/write TX_DATA/RX_DATA directly. INT_CONFIG/INT_STATUS are not used
 * by the BIOS. The console is polled, not interrupt-driven.
 */

#define TINYMCU_UART_CONFIG     (TINYMCU_UART_BASE + 0x00)
#define TINYMCU_UART_BAUDRATE   (TINYMCU_UART_BASE + 0x04)
#define TINYMCU_UART_STATUS     (TINYMCU_UART_BASE + 0x08)
#define TINYMCU_UART_TX_DATA    (TINYMCU_UART_BASE + 0x0C)
#define TINYMCU_UART_RX_DATA    (TINYMCU_UART_BASE + 0x10)
#define TINYMCU_UART_INT_CONFIG (TINYMCU_UART_BASE + 0x14)
#define TINYMCU_UART_INT_STATUS (TINYMCU_UART_BASE + 0x18)

/* CONFIG bits 1:0: data bits ("11" unused, falls back to 8). */
#define TINYMCU_UART_DATABITS_8 (0x0u << 0)
#define TINYMCU_UART_DATABITS_7 (0x1u << 0)
#define TINYMCU_UART_DATABITS_9 (0x2u << 0)

/* CONFIG bits 3:2: stop bits. */
#define TINYMCU_UART_STOPBITS_1 (0x0u << 2)
#define TINYMCU_UART_STOPBITS_2 (0x1u << 2)

/* CONFIG bits 5:4: parity. */
#define TINYMCU_UART_PARITY_NONE (0x0u << 4)
#define TINYMCU_UART_PARITY_EVEN (0x1u << 4)
#define TINYMCU_UART_PARITY_ODD  (0x2u << 4)

/* CONFIG shortcut: 8 data bits, 1 stop bit, no parity. */
#define TINYMCU_UART_CONFIG_8N1 (TINYMCU_UART_DATABITS_8 | TINYMCU_UART_STOPBITS_1 | TINYMCU_UART_PARITY_NONE)

/* STATUS bits. */
#define TINYMCU_UART_STATUS_TX_ACTIVE    (1u << 0)
#define TINYMCU_UART_STATUS_RX_READY     (1u << 1)
#define TINYMCU_UART_STATUS_PARITY_ERROR (1u << 2)

/*
 * Timer registers (rtl/peripherals/tinymcu_periph_timer.vhd). bios_time()
 * reads COUNTER directly and converts ticks to milliseconds using
 * TINYMCU_CLK_KHZ below. There is no CLK_KHZ *register* like vemu's,
 * the clock frequency is a build-time constant instead.
 */

#define TINYMCU_TIMER_CONFIG     (TINYMCU_TIMER_BASE + 0x00)
#define TINYMCU_TIMER_INT_CONFIG (TINYMCU_TIMER_BASE + 0x04)
#define TINYMCU_TIMER_INT_STATUS (TINYMCU_TIMER_BASE + 0x08)
#define TINYMCU_TIMER_COUNTER    (TINYMCU_TIMER_BASE + 0x0C)
#define TINYMCU_TIMER_COMPARE    (TINYMCU_TIMER_BASE + 0x10)

/* CONFIG bits 3:0: CLKSEL (binary-encoded, not one-hot). */
#define TINYMCU_TIMER_CLKSEL_OFF     0x0u
#define TINYMCU_TIMER_CLKSEL_DIV1    0x1u
#define TINYMCU_TIMER_CLKSEL_DIV2    0x2u
#define TINYMCU_TIMER_CLKSEL_DIV4    0x3u
#define TINYMCU_TIMER_CLKSEL_DIV8    0x4u
#define TINYMCU_TIMER_CLKSEL_DIV64   0x5u
#define TINYMCU_TIMER_CLKSEL_DIV256  0x6u
#define TINYMCU_TIMER_CLKSEL_DIV1024 0x7u

/* System clock, in kHz. Set to match the target build (e.g. the Zynq-7010
 * PL clock TinyMCU is synthesized with). Used by bios_time() to convert
 * COUNTER ticks (at CLKSEL_DIV1, i.e. one tick per clk_i cycle) to ms. */
#define TINYMCU_CLK_KHZ 16000u

/*
 * GPIO (rtl/peripherals/tinymcu_periph_gpio.vhd) is not used by the
 * stock BIOS functions. TINYMCU_GPIO_BASE is provided above for
 * platform-specific extensions only.
 */

#endif /* MMIO_H */
