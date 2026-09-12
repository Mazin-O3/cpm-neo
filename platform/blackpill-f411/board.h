/*
 * platform/blackpill-f411/board.h
 * CP/M Neo — STM32F411CEU6 (Black Pill) register map
 *
 * Self-contained: no ST CMSIS headers.
 * Register addresses and bit definitions are taken from RM0383.
 *
 * Board-specific configuration belongs in config.h.
 * This file contains fixed STM32F411 hardware definitions.
 */

#ifndef BPF411_BOARD_H
#define BPF411_BOARD_H

#include <stddef.h>
#include <stdint.h>

/* ── Register helpers ─────────────────────────────────────── */

#define _BPREG(a) (*(volatile uint32_t *)(a))
#define _BPREG16(a) (*(volatile uint16_t *)(a))

/* ── HSE / clocks ─────────────────────────────────────────── */

#define BP_HSE_FREQ 25000000UL

/* ── RCC @ 0x40023800 ─────────────────────────────────────── */

#define RCC_CR _BPREG(0x40023800UL)
#define RCC_PLLCFGR _BPREG(0x40023804UL)
#define RCC_CFGR _BPREG(0x40023808UL)

#define RCC_AHB1RSTR _BPREG(0x40023810UL)
#define RCC_AHB2RSTR _BPREG(0x40023814UL)
#define RCC_APB2RSTR _BPREG(0x40023824UL)

#define RCC_AHB1ENR _BPREG(0x40023830UL)
#define RCC_AHB2ENR _BPREG(0x40023834UL)
#define RCC_APB2ENR _BPREG(0x40023844UL)

/* RCC_CR */
#define RCC_CR_HSEON (1UL << 16)
#define RCC_CR_HSERDY (1UL << 17)
#define RCC_CR_PLLON (1UL << 24)
#define RCC_CR_PLLRDY (1UL << 25)

/* RCC_PLLCFGR */
#define RCC_PLLSRC_HSE (1UL << 22)

/* RCC_CFGR */
#define RCC_CFGR_SW (0x3UL << 0)
#define RCC_CFGR_SW_PLL (0x2UL << 0)
#define RCC_CFGR_SWS (0x3UL << 2)
#define RCC_CFGR_SWS_PLL (0x2UL << 2)

#define RCC_CFGR_PPRE1 (0x7UL << 10)
#define RCC_CFGR_PPRE1_DIV2 (0x4UL << 10)
#define RCC_CFGR_PPRE2 (0x7UL << 13)

/* RCC_AHB1ENR */
#define RCC_AHB1_GPIOA (1UL << 0)
#define RCC_AHB1_GPIOC (1UL << 2)

/* RCC_APB2ENR */
#define RCC_APB2ENR_USART1EN (1UL << 4)

/* RCC_APB2RSTR */
#define RCC_APB2RSTR_USART1RST (1UL << 4)

/* ── FLASH @ 0x40023C00 ───────────────────────────────────── */

#define FLASH_ACR _BPREG(0x40023C00UL)

#define FLASH_ACR_LATENCY_3 (3UL << 0)
#define FLASH_ACR_PRTFEN (1UL << 8)
#define FLASH_ACR_ICEN (1UL << 9)
#define FLASH_ACR_DCEN (1UL << 10)
#define FLASH_ACR_READY (1UL << 16)

/* ── GPIOA @ 0x40020000 ───────────────────────────────────── */

#define GPIOA_MODER _BPREG(0x40020000UL)
#define GPIOA_OSPEEDR _BPREG(0x40020008UL)
#define GPIOA_PUPDR _BPREG(0x4002000CUL)
#define GPIOA_ODR _BPREG(0x40020014UL)
#define GPIOA_AFRH _BPREG(0x40020024UL)

/* ── GPIOC @ 0x40020800 ───────────────────────────────────── */

#define GPIOC_MODER _BPREG(0x40020800UL)
#define GPIOC_BSRR _BPREG(0x40020818UL)

/* ── USB OTG FS pins ──────────────────────────────────────── */

#define GPIO_AF_OTG_FS 10U
#define GPIOA_AF11_SHIFT 12U
#define GPIOA_AF12_SHIFT 16U
#define GPIO_AF_MASK 0xFUL

/* ── On-board LED ──────────────────────────────────────────── */

#define GPIOC_MODER_PC13 (0x3UL << 26)
#define GPIOC_MODER_PC13_OUT (0x1UL << 26)

#define GPIOC_BSRR_PC13_SET (1UL << 13)
#define GPIOC_BSRR_PC13_RESET (1UL << 29)

/* ── DWT cycle counter ────────────────────────────────────── */

#define DWT_CTRL _BPREG(0xE0001000UL)
#define DWT_CYCCNT _BPREG(0xE0001004UL)

#define CoreDebug_DEMCR _BPREG(0xE000EDFCUL)

#define TRCENA (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

/* ── System control block ─────────────────────────────────── */

#define SCB_VTOR _BPREG(0xE000ED08UL)

/* ── USART1 @ 0x40011000 ──────────────────────────────────── */

#define USART1_BASE 0x40011000UL

typedef struct
{
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART;

#define USART1 ((USART *)USART1_BASE)

/* USART_SR */
#define USART_SR_TXE (1UL << 7)
#define USART_SR_TC (1UL << 6)
#define USART_SR_RXNE (1UL << 5)
#define USART_SR_ORE (1UL << 3)

/* USART_CR1 */
#define USART_CR1_RE (1UL << 2)
#define USART_CR1_TE (1UL << 3)
#define USART_CR1_UE (1UL << 13)

/* ── USART1 GPIO configuration ────────────────────────────── */

#define GPIO_AF_USART1 7U

/*
 * AFRH contains pins 8..15:
 *
 *   PA9  -> nibble 1 -> bits 7:4
 *   PA10 -> nibble 2 -> bits 11:8
 */
#define GPIOA_AF9_SHIFT 4U
#define GPIOA_AF10_SHIFT 8U

#define GPIOA_MODER_PA9_MASK (0x3UL << 18)
#define GPIOA_MODER_PA9_OUT (0x1UL << 18)
#define GPIOA_MODER_PA9_AF (0x2UL << 18)

#define GPIOA_MODER_PA10_MASK (0x3UL << 20)
#define GPIOA_MODER_PA10_AF (0x2UL << 20)

#define GPIOA_OSPEEDR_PA9_MASK (0x3UL << 18)
#define GPIOA_OSPEEDR_PA9_FAST (0x1UL << 18)

#define GPIOA_OSPEEDR_PA10_MASK (0x3UL << 20)
#define GPIOA_OSPEEDR_PA10_FAST (0x1UL << 20)

#define GPIOA_PUPDR_PA9_MASK (0x3UL << 18)
#define GPIOA_PUPDR_PA9_UP (0x1UL << 18)

#define GPIOA_PUPDR_PA10_MASK (0x3UL << 20)
#define GPIOA_PUPDR_PA10_UP (0x1UL << 20)

#define GPIOA_ODR_PA9 (1UL << 9)

/* ── Flash storage ────────────────────────────────────────── */

/*
 * Read-only phase-1 disk storage.
 *
 * The disk image is mapped at the XIP base defined by config.h.
 */
#define BP_FLASH_IMAGE_BASE 0x08001000UL
#define BP_FLASH_SECTOR_SIZE 512U

#endif /* BPF411_BOARD_H */
