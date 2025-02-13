/*-
 * Copyright (c) 2000 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <efi.h>
#include <efilib.h>
#include <teken.h>

#include "bootstrap.h"

static EFI_GUID simple_input_ex_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
static SIMPLE_TEXT_OUTPUT_INTERFACE	*conout;
static SIMPLE_INPUT_INTERFACE		*conin;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *coninex;

static tf_bell_t	efi_cons_bell;
static tf_cursor_t	efi_text_cursor;
static tf_putchar_t	efi_text_putchar;
static tf_fill_t	efi_text_fill;
static tf_copy_t	efi_text_copy;
static tf_param_t	efi_text_param;
static tf_respond_t	efi_cons_respond;

static teken_funcs_t tf = {
	.tf_bell	= efi_cons_bell,
	.tf_cursor	= efi_text_cursor,
	.tf_putchar	= efi_text_putchar,
	.tf_fill	= efi_text_fill,
	.tf_copy	= efi_text_copy,
	.tf_param	= efi_text_param,
	.tf_respond	= efi_cons_respond,
};

teken_t teken;
teken_pos_t tp;

struct text_pixel {
	teken_char_t c;
	teken_attr_t a;
};

static struct text_pixel *buffer;

#define	KEYBUFSZ 10
static unsigned keybuf[KEYBUFSZ];	/* keybuf for extended codes */
static int key_pending;

static const unsigned char teken_color_to_efi_color[16] = {
	EFI_BLACK,
	EFI_RED,
	EFI_GREEN,
	EFI_BROWN,
	EFI_BLUE,
	EFI_MAGENTA,
	EFI_CYAN,
	EFI_LIGHTGRAY,
	EFI_DARKGRAY,
	EFI_LIGHTRED,
	EFI_LIGHTGREEN,
	EFI_YELLOW,
	EFI_LIGHTBLUE,
	EFI_LIGHTMAGENTA,
	EFI_LIGHTCYAN,
	EFI_WHITE
};

static void efi_cons_probe(struct console *);
static int efi_cons_init(int);
void efi_cons_putchar(int);
int efi_cons_getchar(void);
void efi_cons_efiputchar(int);
int efi_cons_poll(void);

struct console efi_console = {
	"efi",
	"EFI console",
	C_WIDEOUT,
	efi_cons_probe,
	efi_cons_init,
	efi_cons_putchar,
	efi_cons_getchar,
	efi_cons_poll
};

/*
 * Not implemented.
 */
static void
efi_cons_bell(void *s __unused)
{
}

static void
efi_text_cursor(void *s __unused, const teken_pos_t *p)
{
	UINTN row, col;

	(void) conout->QueryMode(conout, conout->Mode->Mode, &col, &row);

	if (p->tp_col == col)
		col = p->tp_col - 1;
	else
		col = p->tp_col;

	if (p->tp_row == row)
		row = p->tp_row - 1;
	else
		row = p->tp_row;

	conout->SetCursorPosition(conout, col, row);
}

static void
efi_text_printchar(const teken_pos_t *p)
{
	UINTN a, attr;
	struct text_pixel *px;
	teken_color_t fg, bg, tmp;

	px = buffer + p->tp_col + p->tp_row * tp.tp_col;
	a = conout->Mode->Attribute;

	fg = teken_256to16(px->a.ta_fgcolor);
	bg = teken_256to16(px->a.ta_bgcolor);
	if (px->a.ta_format & TF_BOLD)
		fg |= TC_LIGHT;
	if (px->a.ta_format & TF_BLINK)
		bg |= TC_LIGHT;

	if (px->a.ta_format & TF_REVERSE) {
		tmp = fg;
		fg = bg;
		bg = tmp;
	}

	attr = EFI_TEXT_ATTR(teken_color_to_efi_color[fg],
	    teken_color_to_efi_color[bg] & 0x7);

	conout->SetCursorPosition(conout, p->tp_col, p->tp_row);

	/* to prvent autoscroll, skip print of lower right char */
	if (p->tp_row == tp.tp_row - 1 &&
	    p->tp_col == tp.tp_col - 1)
		return;

	(void) conout->SetAttribute(conout, attr);
	efi_cons_efiputchar(px->c);
	(void) conout->SetAttribute(conout, a);
}

static void
efi_text_putchar(void *s __unused, const teken_pos_t *p, teken_char_t c,
    const teken_attr_t *a)
{
	EFI_STATUS status;
	int idx;

	idx = p->tp_col + p->tp_row * tp.tp_col;
	buffer[idx].c = c;
	buffer[idx].a = *a;
	efi_text_printchar(p);
}

static void
efi_text_fill(void *s, const teken_rect_t *r, teken_char_t c,
    const teken_attr_t *a)
{
	teken_pos_t p;
	UINTN row, col;

	(void) conout->QueryMode(conout, conout->Mode->Mode, &col, &row);

	conout->EnableCursor(conout, FALSE);
	for (p.tp_row = r->tr_begin.tp_row; p.tp_row < r->tr_end.tp_row;
	    p.tp_row++)
		for (p.tp_col = r->tr_begin.tp_col;
		    p.tp_col < r->tr_end.tp_col; p.tp_col++)
			efi_text_putchar(s, &p, c, a);
	conout->EnableCursor(conout, TRUE);
}

static bool
efi_same_pixel(struct text_pixel *px1, struct text_pixel *px2)
{
	if (px1->c != px2->c)
		return (false);

	if (px1->a.ta_format != px2->a.ta_format)
		return (false);
	if (px1->a.ta_fgcolor != px2->a.ta_fgcolor)
		return (false);
	if (px1->a.ta_bgcolor != px2->a.ta_bgcolor)
		return (false);

	return (true);
}

static void
efi_text_copy(void *ptr __unused, const teken_rect_t *r, const teken_pos_t *p)
{
	int srow, drow;
	int nrow, ncol, x, y; /* Has to be signed - >= 0 comparison */
	teken_pos_t d, s;

	/*
	 * Copying is a little tricky. We must make sure we do it in
	 * correct order, to make sure we don't overwrite our own data.
	 */

	nrow = r->tr_end.tp_row - r->tr_begin.tp_row;
	ncol = r->tr_end.tp_col - r->tr_begin.tp_col;

	conout->EnableCursor(conout, FALSE);
	if (p->tp_row < r->tr_begin.tp_row) {
		/* Copy from bottom to top. */
		for (y = 0; y < nrow; y++) {
			d.tp_row = p->tp_row + y;
			s.tp_row = r->tr_begin.tp_row + y;
			drow = d.tp_row * tp.tp_col;
			srow = s.tp_row * tp.tp_col;
			for (x = 0; x < ncol; x++) {
				d.tp_col = p->tp_col + x;
				s.tp_col = r->tr_begin.tp_col + x;

				if (!efi_same_pixel(
				    &buffer[d.tp_col + drow],
				    &buffer[s.tp_col + srow])) {
					buffer[d.tp_col + drow] =
					    buffer[s.tp_col + srow];
					efi_text_printchar(&d);
				}
			}
		}
	} else {
		/* Copy from top to bottom. */
		if (p->tp_col < r->tr_begin.tp_col) {
			/* Copy from right to left. */
			for (y = nrow - 1; y >= 0; y--) {
				d.tp_row = p->tp_row + y;
				s.tp_row = r->tr_begin.tp_row + y;
				drow = d.tp_row * tp.tp_col;
				srow = s.tp_row * tp.tp_col;
				for (x = 0; x < ncol; x++) {
					d.tp_col = p->tp_col + x;
					s.tp_col = r->tr_begin.tp_col + x;

					if (!efi_same_pixel(
					    &buffer[d.tp_col + drow],
					    &buffer[s.tp_col + srow])) {
						buffer[d.tp_col + drow] =
						    buffer[s.tp_col + srow];
						efi_text_printchar(&d);
					}
				}
			}
		} else {
			/* Copy from left to right. */
			for (y = nrow - 1; y >= 0; y--) {
				d.tp_row = p->tp_row + y;
				s.tp_row = r->tr_begin.tp_row + y;
				drow = d.tp_row * tp.tp_col;
				srow = s.tp_row * tp.tp_col;
				for (x = ncol - 1; x >= 0; x--) {
					d.tp_col = p->tp_col + x;
					s.tp_col = r->tr_begin.tp_col + x;

					if (!efi_same_pixel(
					    &buffer[d.tp_col + drow],
					    &buffer[s.tp_col + srow])) {
						buffer[d.tp_col + drow] =
						    buffer[s.tp_col + srow];
						efi_text_printchar(&d);
					}
				}
			}
		}
	}
	conout->EnableCursor(conout, TRUE);
}

static void
efi_text_param(void *s __unused, int cmd, unsigned int value)
{
	switch (cmd) {
	case TP_SETLOCALCURSOR:
		/*
		 * 0 means normal (usually block), 1 means hidden, and
		 * 2 means blinking (always block) for compatibility with
		 * syscons.  We don't support any changes except hiding,
		 * so must map 2 to 0.
		 */
		value = (value == 1) ? 0 : 1;
		/* FALLTHROUGH */
	case TP_SHOWCURSOR:
		if (value == 1)
			conout->EnableCursor(conout, TRUE);
		else
			conout->EnableCursor(conout, FALSE);
		break;
	default:
		/* Not yet implemented */
		break;
	}
}

/*
 * Not implemented.
 */
static void
efi_cons_respond(void *s __unused, const void *buf __unused,
    size_t len __unused)
{
}

static void
efi_cons_probe(struct console *cp)
{
	cp->c_flags |= C_PRESENTIN | C_PRESENTOUT;
}

bool
efi_cons_update_mode(void)
{
	UINTN cols, rows;
	const teken_attr_t *a;
	EFI_STATUS status;
	char env[8];

	status = conout->QueryMode(conout, conout->Mode->Mode, &cols, &rows);
	if (EFI_ERROR(status)) {
		cols = 80;
		rows = 24;
	}

	if (buffer != NULL) {
		if (tp.tp_row == rows && tp.tp_col == cols)
			return (true);
		free(buffer);
	} else {
		teken_init(&teken, &tf, NULL);
	}

	tp.tp_row = rows;
	tp.tp_col = cols;
	buffer = malloc(rows * cols * sizeof(*buffer));
	if (buffer == NULL)
		return (false);

	teken_set_winsize(&teken, &tp);
	a = teken_get_defattr(&teken);

	for (int row = 0; row < rows; row++)
		for (int col = 0; col < cols; col++) {
			buffer[col + row * tp.tp_col].c = ' ';
			buffer[col + row * tp.tp_col].a = *a;
		}

	snprintf(env, sizeof (env), "%u", (unsigned)rows);
	setenv("LINES", env, 1);
	snprintf(env, sizeof (env), "%u", (unsigned)cols);
	setenv("COLUMNS", env, 1);

	return (true);
}

static int
efi_cons_init(int arg)
{
	EFI_STATUS status;

	if (conin != NULL)
		return (0);

	conout = ST->ConOut;
	conin = ST->ConIn;

	conout->EnableCursor(conout, TRUE);
	status = BS->OpenProtocol(ST->ConsoleInHandle, &simple_input_ex_guid,
	    (void **)&coninex, IH, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (status != EFI_SUCCESS)
		coninex = NULL;

	if (efi_cons_update_mode())
		return (0);

	return (1);
}

void
efi_cons_putchar(int c)
{
	unsigned char ch = c;

	if (buffer != NULL)
		teken_input(&teken, &ch, sizeof (ch));
	else
		efi_cons_efiputchar(c);
}

static int
keybuf_getchar(void)
{
	int i, c = 0;

	for (i = 0; i < KEYBUFSZ; i++) {
		if (keybuf[i] != 0) {
			c = keybuf[i];
			keybuf[i] = 0;
			break;
		}
	}

	return (c);
}

static bool
keybuf_ischar(void)
{
	int i;

	for (i = 0; i < KEYBUFSZ; i++) {
		if (keybuf[i] != 0)
			return (true);
	}
	return (false);
}

/*
 * We are not reading input before keybuf is empty, so we are safe
 * just to fill keybuf from the beginning.
 */
static void
keybuf_inschar(EFI_INPUT_KEY *key)
{

	switch (key->ScanCode) {
	case SCAN_UP: /* UP */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'A';
		break;
	case SCAN_DOWN: /* DOWN */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'B';
		break;
	case SCAN_RIGHT: /* RIGHT */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'C';
		break;
	case SCAN_LEFT: /* LEFT */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'D';
		break;
	case SCAN_DELETE:
		keybuf[0] = CHAR_BACKSPACE;
		break;
	case SCAN_ESC:
		keybuf[0] = 0x1b;	/* esc */
		break;
	default:
		keybuf[0] = key->UnicodeChar;
		break;
	}
}

static bool
efi_readkey(void)
{
	EFI_STATUS status;
	EFI_INPUT_KEY key;

	status = conin->ReadKeyStroke(conin, &key);
	if (status == EFI_SUCCESS) {
		keybuf_inschar(&key);
		return (true);
	}
	return (false);
}

static bool
efi_readkey_ex(void)
{
	EFI_STATUS status;
	EFI_INPUT_KEY *kp;
	EFI_KEY_DATA  key_data;
	uint32_t kss;

	status = coninex->ReadKeyStrokeEx(coninex, &key_data);
	if (status == EFI_SUCCESS) {
		kss = key_data.KeyState.KeyShiftState;
		kp = &key_data.Key;
		if (kss & EFI_SHIFT_STATE_VALID) {

			/*
			 * quick mapping to control chars, replace with
			 * map lookup later.
			 */
			if (kss & EFI_RIGHT_CONTROL_PRESSED ||
			    kss & EFI_LEFT_CONTROL_PRESSED) {
				if (kp->UnicodeChar >= 'a' &&
				    kp->UnicodeChar <= 'z') {
					kp->UnicodeChar -= 'a';
					kp->UnicodeChar++;
				}
			}
		}

		keybuf_inschar(kp);
		return (true);
	}
	return (false);
}

int
efi_cons_getchar(void)
{
	int c;

	if ((c = keybuf_getchar()) != 0)
		return (c);

	key_pending = 0;

	if (coninex == NULL) {
		if (efi_readkey())
			return (keybuf_getchar());
	} else {
		if (efi_readkey_ex())
			return (keybuf_getchar());
	}

	return (-1);
}

int
efi_cons_poll(void)
{
	EFI_STATUS status;

	if (keybuf_ischar() || key_pending)
		return (1);

	/*
	 * Some EFI implementation (u-boot for example) do not support
	 * WaitForKey().
	 * CheckEvent() can clear the signaled state.
	 */
	if (coninex != NULL) {
		if (coninex->WaitForKeyEx == NULL) {
			key_pending = efi_readkey_ex();
		} else {
			status = BS->CheckEvent(coninex->WaitForKeyEx);
			key_pending = status == EFI_SUCCESS;
		}
	} else {
		if (conin->WaitForKey == NULL) {
			key_pending = efi_readkey();
		} else {
			status = BS->CheckEvent(conin->WaitForKey);
			key_pending = status == EFI_SUCCESS;
		}
	}

	return (key_pending);
}

/* Plain direct access to EFI OutputString(). */
void
efi_cons_efiputchar(int c)
{
	CHAR16 buf[2];
	EFI_STATUS status;

	buf[0] = c;
        buf[1] = 0;     /* terminate string */

	status = conout->TestString(conout, buf);
	if (EFI_ERROR(status))
		buf[0] = '?';
	conout->OutputString(conout, buf);
}
