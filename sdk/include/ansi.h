/* ANSI/VT100 escape-sequence mnemonics for the CP/M Neo console. */

#ifndef ANSI_H
#define ANSI_H

/* CSI = Control Sequence Introducer (ESC [) */

#define CSI_HOME    "\x1B[H"          /* Cursor home                    */
#define CSI_CUP     "\x1B[%d;%dH"     /* Cursor position (row, col)     */
#define CSI_CUP_ROW "\x1B[%dH"        /* Cursor position (row only)     */
#define CSI_EL      "\x1B[K"          /* Erase to end of line           */
#define CSI_CLS     "\x1B[2J\x1B[H"   /* Clear screen + home            */
#define CSI_HIDE    "\x1B[?25l"       /* Hide cursor                    */
#define CSI_SHOW    "\x1B[?25h"       /* Show cursor                    */
#define CSI_IL      "\x1B[%dL"        /* Insert N lines                 */
#define CSI_DL      "\x1B[%dM"        /* Delete N lines                 */
#define CSI_DECSTBM "\x1B[%d;%dr"     /* Set scroll region (top,bottom) */
#define CSI_RST_SCR "\x1B[r"          /* Reset scroll region to full scr */
#define CSI_IND     "\x1B" "D"        /* Index — scroll up in region     */
#define CSI_RI      "\x1B" "M"        /* Reverse index — scroll down     */
#define CSI_REVERSE "\x1B[7m"         /* Reverse video (SGR 7)           */
#define CSI_RESET   "\x1B[0m"         /* Reset attributes (SGR 0)        */

#endif /* ANSI_H */