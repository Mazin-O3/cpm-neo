/*
 * core/kernel/bios.h — Kernel interface to the BIOS layer
 */

#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>

/*
 * Initialization
 *
 * Initialize the BIOS layer.  Returns EOK (0) on success, negative
 * errno on failure (the bootloader halts before any console output).
 */
int bios_init(void);

/* Console I/O */
void bios_conout(int c); /* Output one character to the console.       */
int  bios_conin(void);   /* Blocking read from the console.            */
int  bios_constat(void); /* Input status: 0xFF if key ready, 0 empty.  */

/* Disk I/O */
int bios_read(uint16_t sec, uint8_t *buf);        /* Read one sector.       */
int bios_write(uint16_t sec, const uint8_t *buf); /* Write one sector.      */

/*
 * Persistence barrier.  Makes all previously accepted bios_write() calls
 * committed according to the platform's storage persistence contract.
 * A successful return is the durability guarantee.
 */
int bios_sync(void);

/* Timer */
uint32_t bios_millis(void); /* Monotonic milliseconds since power-on.     */

#endif /* BIOS_H */
