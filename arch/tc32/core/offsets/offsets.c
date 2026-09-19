/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gen_offset.h>
#include <kernel_arch_data.h>
#include <kernel_offsets.h>

GEN_OFFSET_SYM(_callee_saved_t, sp);
GEN_OFFSET_SYM(_callee_saved_t, lr);
GEN_OFFSET_SYM(_callee_saved_t, r4);
GEN_OFFSET_SYM(_callee_saved_t, r5);
GEN_OFFSET_SYM(_callee_saved_t, r6);
GEN_OFFSET_SYM(_callee_saved_t, r7);
GEN_OFFSET_SYM(_callee_saved_t, r8);
GEN_OFFSET_SYM(_callee_saved_t, r9);
GEN_OFFSET_SYM(_callee_saved_t, r10);
GEN_OFFSET_SYM(_callee_saved_t, r11);
GEN_OFFSET_SYM(_callee_saved_t, r12);

GEN_ABSOLUTE_SYM(_callee_saved_t_SIZEOF, sizeof(_callee_saved_t));

GEN_OFFSET_STRUCT(arch_esf, r0);
GEN_OFFSET_STRUCT(arch_esf, r1);
GEN_OFFSET_STRUCT(arch_esf, r2);
GEN_OFFSET_STRUCT(arch_esf, r3);
GEN_OFFSET_STRUCT(arch_esf, r12);
GEN_OFFSET_STRUCT(arch_esf, lr);
GEN_OFFSET_STRUCT(arch_esf, pc);
GEN_OFFSET_STRUCT(arch_esf, sr);

GEN_ABSOLUTE_SYM(__struct_arch_esf_SIZEOF, sizeof(struct arch_esf));

GEN_ABS_SYM_END
