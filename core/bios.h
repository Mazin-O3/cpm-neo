/*
 * kernel/bios.h
 * CP/M Neo — Kernel interface to the BIOS layer
 */

#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>

/* Initialize the BIOS layer. Returns 0 on success. */
int bios_init(void);

/* Output one character to the console. */
void bios_conout(int c);

/* Read one character from the console (Blocking). */
int bios_conin(void);

/* Return the console input status (e.g., 0xFF if char ready, 0 if empty). */
int bios_constat(void);

/* Get the console width and height in characters. */
void bios_consize(uint8_t *cw, uint8_t *ch);

/* Read one sector from the disk into buf. Returns 0 on success. */
int bios_read(uint16_t sec, uint8_t *buf);

/* Write one sector from buf to the disk. Returns 0 on success. */
int bios_write(uint16_t sec, const uint8_t *buf);

/* Persistence barrier. Makes all previously accepted bios_write() calls
 * committed according to the platform's storage persistence contract.
 * A successful return is the durability guarantee. Returns 0 on success. */
int bios_sync(void);

/* Platform-specific time value.*/
uint32_t bios_time(void);

#endif /* BIOS_H */