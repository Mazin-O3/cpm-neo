/*
 * platform/blackpill-f411/bios.c
 * CP/M Neo — STM32F411CEU6 (BlackPill) BIOS
 *
 * The BIOS provides the board clock, DWT time base, UART console,
 * and read-only flash storage.
 *
 * Console:
 *   USART1, polled, 115200 8N1
 *   PA9  TX, AF7
 *   PA10 RX, AF7
 *
 * Storage:
 *   Phase 1 read-only.
 *   The sysgen disk image is stored verbatim at the flash XIP window.
 *
 * Time:
 *   DWT cycle counter at 96 MHz.
 *
 * This file is compiled into BOTH the bootloader and kernel.
 * Do not depend on libc.
 */

#include "bios.h"
#include "board.h"
#include "config.h"
#include "errno.h"

#define MS_PER_SEC_DIV 96000UL
#define UART_BRR_115200 (96000000UL / 115200U)

#define UART_RX_BUF_SIZE 256U

#define PLL_M 25U
#define PLL_N 192U
#define PLL_Q 4U

/* ── Helpers ───────────────────────────────────────────────── */

static void bp_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

/* ── Clock / time ──────────────────────────────────────────── */

static void clock_init(void)
{
    /*
     * Reuse the bootloader clock when it has already configured
     * the PLL and switched SYSCLK to it.
     */

    if ((RCC_CR & RCC_CR_PLLON) && (RCC_CR & RCC_CR_HSERDY) &&
        ((RCC_CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL))
    {
        return;
    }

    /*
     * 96 MHz SYSCLK requires three FLASH wait states.
     */
    FLASH_ACR = FLASH_ACR_LATENCY_3 | FLASH_ACR_PRTFEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC_CR |= RCC_CR_HSEON;

    while (!(RCC_CR & RCC_CR_HSERDY))
        ;

    RCC_CR &= ~RCC_CR_PLLON;

    while (RCC_CR & RCC_CR_PLLRDY)
        ;

    /*
     * 25 MHz HSE:
     *
     *   M = 25
     *   N = 192
     *   P = 2
     *   SYSCLK = 96 MHz
     *   Q = 4
     *   PLL48CLK = 48 MHz
     */
    RCC_PLLCFGR = (PLL_N << 6) | (PLL_Q << 24) | RCC_PLLSRC_HSE | PLL_M;

    RCC_CR |= RCC_CR_PLLON;

    while (!(RCC_CR & RCC_CR_PLLRDY))
        ;

    /*
     * APB1 = 48 MHz
     * APB2 = 96 MHz
     */
    RCC_CFGR = (RCC_CFGR & ~(RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) | RCC_CFGR_PPRE1_DIV2;

    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;

    while ((RCC_CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;
}

static int dwt_init(void)
{
    CoreDebug_DEMCR |= TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;

    if (!(CoreDebug_DEMCR & TRCENA) || !(DWT_CTRL & DWT_CTRL_CYCCNTENA))
        return EIO;

    return EOK;
}

/* ── UART console ──────────────────────────────────────────── */

typedef struct
{
    uint8_t  rx[UART_RX_BUF_SIZE];
    uint16_t rx_in;
    uint16_t rx_out;
    uint16_t rx_count;
} uart_port;

static uart_port uart;

static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1_GPIOA;
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;

    USART1->CR2 = 0;
    USART1->CR3 = 0;

    uart.rx_in = 0;
    uart.rx_out = 0;
    uart.rx_count = 0;

    GPIOA_AFRH =
        (GPIOA_AFRH & ~(GPIO_AF_MASK << GPIOA_AF9_SHIFT) & ~(GPIO_AF_MASK << GPIOA_AF10_SHIFT)) |
        (GPIO_AF_USART1 << GPIOA_AF9_SHIFT) | (GPIO_AF_USART1 << GPIOA_AF10_SHIFT);

    GPIOA_OSPEEDR = (GPIOA_OSPEEDR & ~GPIOA_OSPEEDR_PA9_MASK & ~GPIOA_OSPEEDR_PA10_MASK) |
                    GPIOA_OSPEEDR_PA9_FAST | GPIOA_OSPEEDR_PA10_FAST;

    GPIOA_PUPDR = (GPIOA_PUPDR & ~GPIOA_PUPDR_PA9_MASK & ~GPIOA_PUPDR_PA10_MASK) |
                  GPIOA_PUPDR_PA9_UP | GPIOA_PUPDR_PA10_UP;

    GPIOA_MODER = (GPIOA_MODER & ~GPIOA_MODER_PA9_MASK & ~GPIOA_MODER_PA10_MASK) |
                  GPIOA_MODER_PA9_AF | GPIOA_MODER_PA10_AF;

    USART1->BRR = UART_BRR_115200;

    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_poll(void)
{
    uint32_t sr;

    while ((sr = USART1->SR) & (USART_SR_RXNE | USART_SR_ORE))
    {
        uint8_t c = (uint8_t)USART1->DR;

        if ((sr & USART_SR_ORE) || uart.rx_count >= UART_RX_BUF_SIZE)
        {
            continue;
        }

        uart.rx[uart.rx_in] = c;

        uart.rx_in = (uint16_t)((uart.rx_in + 1) % UART_RX_BUF_SIZE);

        uart.rx_count++;
    }
}

static void uart_putc(uint8_t c)
{
    while (!(USART1->SR & USART_SR_TXE))
        ;

    USART1->DR = c;
}

/* ── BIOS contract ─────────────────────────────────────────── */

int bios_init(void)
{
    clock_init();
    uart_init();

    if (dwt_init() != EOK)
        return EIO;

    return EOK;
}

uint32_t bios_time(void)
{
    return DWT_CYCCNT / MS_PER_SEC_DIV;
}

void bios_conout(int c)
{
    if (c == '\n')
        uart_putc('\r');

    uart_putc((uint8_t)c);
}

int bios_constat(void)
{
    uart_poll();

    return uart.rx_count ? 0xFF : 0;
}

int bios_conin(void)
{
    while (uart.rx_count == 0)
    {
        uart_poll();
    }

    uint8_t c = uart.rx[uart.rx_out];

    uart.rx_out = (uint16_t)((uart.rx_out + 1) % UART_RX_BUF_SIZE);
    uart.rx_count--;

    return (int)c;
}

void bios_consize(uint8_t *cw, uint8_t *ch)
{
    *cw = 80;
    *ch = 24;
}

int bios_read(uint16_t sec, uint8_t *buf)
{
    if (buf == NULL)
        return EINVAL;

    if ((uint32_t)sec >= ((uint32_t)CONFIG_DISK_SIZE * 2U))
    {
        return EINVAL;
    }

    bp_copy(buf, (const uint8_t *)(BP_FLASH_IMAGE_BASE + (uint32_t)sec * BP_FLASH_SECTOR_SIZE),
            BP_FLASH_SECTOR_SIZE);

    return EOK;
}

int bios_write(uint16_t sec, const uint8_t *buf)
{
    if (sec >= ((uint32_t)CONFIG_DISK_SIZE * 2U))
        return EINVAL;

    if (buf == NULL)
        return EINVAL;

    return EIO;
}

int bios_sync(void)
{
    return EIO;
}
