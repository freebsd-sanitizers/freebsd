/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Andrew Turner
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
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

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

struct store_loc {
	const char *file;
	uint32_t line;
	uint32_t column;
};

struct ubsan_type {
	uint16_t type_kind;
	uint16_t type_info;
	char type_name[1];
};

#define	CFI_KIND_ICALL	4

struct cfi_check_fail_data {
	uint8_t kind;
	struct store_loc loc;
	struct ubsan_type *type;
};

void __cfi_slowpath_diag(uint64_t, void *, void *);
void __cfi_slowpath(uint64_t, void *);
void __cfi_check_fail(void *, void *);

static SYSCTL_NODE(_debug, OID_AUTO, kcfi, CTLFLAG_RD, 0, "Kernel CFI");

static int cfi_panic = 0;
SYSCTL_INT(_debug_kcfi, OID_AUTO, panic,
    CTLFLAG_RDTUN, &cfi_panic, 0,
    "Panic on CFI failure");

void
__cfi_slowpath_diag(uint64_t id, void *ptr, void *diag)
{
	panic("__cfi_slowpath_diag");
}

void
__cfi_slowpath(uint64_t id, void *ptr)
{
	panic("__cfi_slowpath");
}

void
__cfi_check_fail(void *data, void *ptr)
{
	panic("__cfi_check_fail");
}

void __ubsan_handle_cfi_check_fail(struct cfi_check_fail_data *, void *,
    void *);
void __ubsan_handle_cfi_check_fail_abort(struct cfi_check_fail_data *, void *,
    void *);

static void
handle_cfi_check_fail_icall(struct cfi_check_fail_data *data, void *ptr)
{
	const char *name;
#ifdef DDB
	c_db_sym_t sym;
	db_expr_t offset;
#endif

#ifdef DDB
	sym = db_search_symbol((vm_offset_t)ptr, DB_STGY_PROC, &offset);
	db_symbol_values(sym, &name, NULL);
#else
	name = "unknown";
#endif

	printf("CFI icall failed %s:%d: function pointer %p (%s) is not a %s\n",
	    data->loc.file, data->loc.line, ptr, name, data->type->type_name);
}

static void
cfi_check_fail_common(struct cfi_check_fail_data *data, void *ptr, void *vtable,
    bool abort)
{

	if (data->kind == CFI_KIND_ICALL) {
		handle_cfi_check_fail_icall(data, ptr);
	} else {
		printf(
		    "CFI check failed in %s:%d: kind: %d ptr: %p vtable: %p\n",
		    data->loc.file, data->loc.line, data->kind, ptr, vtable);
	}
	if (cfi_panic)
		panic("CFI");
}

void
__ubsan_handle_cfi_check_fail(struct cfi_check_fail_data *data, void *ptr,
    void *vtable)
{

	cfi_check_fail_common(data, ptr, vtable, false);
}

void
__ubsan_handle_cfi_check_fail_abort(struct cfi_check_fail_data *data,
    void *ptr, void *vtable)
{

	cfi_check_fail_common(data, ptr, vtable, true);
}
