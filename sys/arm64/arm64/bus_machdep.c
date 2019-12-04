/*-
 * Copyright (c) 2014 Andrew Turner
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
 *
 */

#define	KCSAN_RUNTIME

#include "opt_platform.h"

#include <sys/param.h>
__FBSDID("$FreeBSD$");

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#ifdef KCFI
#define	BS_FUNC(name) cfi_ ##name

#define	_BS_R(width, type)						\
type generic_bs_r_##width(void *, bus_space_handle_t, bus_size_t);	\
static type								\
cfi_bs_r_##width(void *c, bus_space_handle_t h, bus_size_t o)		\
{									\
	return (generic_bs_r_##width(c, h, o));				\
}

#define	_BS_RM(width, type, func)					\
void generic_bs_##func##_##width(void *, bus_space_handle_t,		\
    bus_size_t, type *, bus_size_t);					\
static void								\
cfi_bs_##func##_##width(void *c, bus_space_handle_t h, bus_size_t o,	\
    type *p, bus_size_t l)						\
{									\
	generic_bs_##func##_##width(c, h, o, p, l);			\
}

#define	_BS_W(width, type)						\
void generic_bs_w_##width(void *, bus_space_handle_t, bus_size_t, type); \
static void								\
cfi_bs_w_##width(void *c, bus_space_handle_t h, bus_size_t o, type v)	\
{									\
	generic_bs_w_##width(c, h, o, v);				\
}

#define	_BS_WM(width, type, func)					\
void generic_bs_##func##_##width(void *, bus_space_handle_t,		\
    bus_size_t, const type *, bus_size_t);				\
static void								\
cfi_bs_##func##_##width(void *c, bus_space_handle_t h, bus_size_t o,	\
    const type *p, bus_size_t l)					\
{									\
	generic_bs_##func##_##width(c, h, o, p, l);			\
}

#else
#define	BS_FUNC(name) generic_ ##name

#define	_BS_R(width, type)						\
type generic_bs_r_##width(void *, bus_space_handle_t, bus_size_t);

#define	_BS_RM(width, type, func)					\
void generic_bs_##func##_##width(void *, bus_space_handle_t,		\
    bus_size_t, type *, bus_size_t);

#define	_BS_W(width, type)						\
void generic_bs_w_##width(void *, bus_space_handle_t, bus_size_t, type);

#define	_BS_WM(width, type, func)					\
void generic_bs_##func##_##width(void *, bus_space_handle_t,		\
    bus_size_t, const type *, bus_size_t);
#endif /* KCFI */

#define	BS(width, type)							\
    _BS_R(width, type)							\
    _BS_RM(width, type, rm)						\
    _BS_RM(width, type, rr)						\
    _BS_W(width, type)							\
    _BS_WM(width, type, wm)						\
    _BS_WM(width, type, wr)

BS(1, uint8_t)
BS(2, uint16_t)
BS(4, uint32_t)
BS(8, uint64_t)

static int
generic_bs_map(void *t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	void *va;

	va = pmap_mapdev(bpa, size);
	if (va == NULL)
		return (ENOMEM);
	*bshp = (bus_space_handle_t)va;
	return (0);
}

static void
generic_bs_unmap(void *t, bus_space_handle_t bsh, bus_size_t size)
{

	pmap_unmapdev(bsh, size);
}

static void
generic_bs_barrier(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, int flags)
{
}

static int
generic_bs_subregion(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

struct bus_space memmap_bus = {
	/* cookie */
	.bs_cookie = NULL,

	/* mapping/unmapping */
	.bs_map = generic_bs_map,
	.bs_unmap = generic_bs_unmap,
	.bs_subregion = generic_bs_subregion,

	/* allocation/deallocation */
	.bs_alloc = NULL,
	.bs_free = NULL,

	/* barrier */
	.bs_barrier = generic_bs_barrier,

	/* read single */
	.bs_r_1 = BS_FUNC(bs_r_1),
	.bs_r_2 = BS_FUNC(bs_r_2),
	.bs_r_4 = BS_FUNC(bs_r_4),
	.bs_r_8 = BS_FUNC(bs_r_8),

	/* read multiple */
	.bs_rm_1 = BS_FUNC(bs_rm_1),
	.bs_rm_2 = BS_FUNC(bs_rm_2),
	.bs_rm_4 = BS_FUNC(bs_rm_4),
	.bs_rm_8 = BS_FUNC(bs_rm_8),

	/* read region */
	.bs_rr_1 = BS_FUNC(bs_rr_1),
	.bs_rr_2 = BS_FUNC(bs_rr_2),
	.bs_rr_4 = BS_FUNC(bs_rr_4),
	.bs_rr_8 = BS_FUNC(bs_rr_8),

	/* write single */
	.bs_w_1 = BS_FUNC(bs_w_1),
	.bs_w_2 = BS_FUNC(bs_w_2),
	.bs_w_4 = BS_FUNC(bs_w_4),
	.bs_w_8 = BS_FUNC(bs_w_8),

	/* write multiple */
	.bs_wm_1 = BS_FUNC(bs_wm_1),
	.bs_wm_2 = BS_FUNC(bs_wm_2),
	.bs_wm_4 = BS_FUNC(bs_wm_4),
	.bs_wm_8 = BS_FUNC(bs_wm_8),

	/* write region */
	.bs_wr_1 = BS_FUNC(bs_wr_1),
	.bs_wr_2 = BS_FUNC(bs_wr_2),
	.bs_wr_4 = BS_FUNC(bs_wr_4),
	.bs_wr_8 = BS_FUNC(bs_wr_8),

	/* set multiple */
	.bs_sm_1 = NULL,
	.bs_sm_2 = NULL,
	.bs_sm_4 = NULL,
	.bs_sm_8 = NULL,

	/* set region */
	.bs_sr_1 = NULL,
	.bs_sr_2 = NULL,
	.bs_sr_4 = NULL,
	.bs_sr_8 = NULL,

	/* copy */
	.bs_c_1 = NULL,
	.bs_c_2 = NULL,
	.bs_c_4 = NULL,
	.bs_c_8 = NULL,

	/* read single stream */
	.bs_r_1_s = NULL,
	.bs_r_2_s = NULL,
	.bs_r_4_s = NULL,
	.bs_r_8_s = NULL,

	/* read multiple stream */
	.bs_rm_1_s = BS_FUNC(bs_rm_1),
	.bs_rm_2_s = BS_FUNC(bs_rm_2),
	.bs_rm_4_s = BS_FUNC(bs_rm_4),
	.bs_rm_8_s = BS_FUNC(bs_rm_8),

	/* read region stream */
	.bs_rr_1_s = NULL,
	.bs_rr_2_s = NULL,
	.bs_rr_4_s = NULL,
	.bs_rr_8_s = NULL,

	/* write single stream */
	.bs_w_1_s = NULL,
	.bs_w_2_s = NULL,
	.bs_w_4_s = NULL,
	.bs_w_8_s = NULL,

	/* write multiple stream */
	.bs_wm_1_s = BS_FUNC(bs_wm_1),
	.bs_wm_2_s = BS_FUNC(bs_wm_2),
	.bs_wm_4_s = BS_FUNC(bs_wm_4),
	.bs_wm_8_s = BS_FUNC(bs_wm_8),

	/* write region stream */
	.bs_wr_1_s = NULL,
	.bs_wr_2_s = NULL,
	.bs_wr_4_s = NULL,
	.bs_wr_8_s = NULL,
};

#ifdef FDT
bus_space_tag_t fdtbus_bs_tag = &memmap_bus;
#endif
