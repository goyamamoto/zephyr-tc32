/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_TC32_INCLUDE_OFFSETS_SHORT_ARCH_H_
#define ZEPHYR_ARCH_TC32_INCLUDE_OFFSETS_SHORT_ARCH_H_

#include <zephyr/offsets.h>

#define _thread_offset_to_sp \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_sp_OFFSET)
#define _thread_offset_to_lr \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_lr_OFFSET)
#define _thread_offset_to_r4 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r4_OFFSET)
#define _thread_offset_to_r5 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r5_OFFSET)
#define _thread_offset_to_r6 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r6_OFFSET)
#define _thread_offset_to_r7 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r7_OFFSET)
#define _thread_offset_to_r8 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r8_OFFSET)
#define _thread_offset_to_r9 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r9_OFFSET)
#define _thread_offset_to_r10 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r10_OFFSET)
#define _thread_offset_to_r11 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r11_OFFSET)
#define _thread_offset_to_r12 \
	(___thread_t_callee_saved_OFFSET + ___callee_saved_t_r12_OFFSET)

#endif /* ZEPHYR_ARCH_TC32_INCLUDE_OFFSETS_SHORT_ARCH_H_ */
