/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/linker-tool.h>
#include <zephyr/linker/sections.h>

#define ROMABLE_REGION FLASH
#define RAMABLE_REGION RAM

#define ROM_BASE CONFIG_FLASH_BASE_ADDRESS

#define ROM_SIZE (CONFIG_FLASH_SIZE * 1024)

#define RAM_BASE DT_CHOSEN_SRAM_ADDR
#define RAM_SIZE DT_CHOSEN_SRAM_SIZE
#define TC32_BOOT_MIRROR_SIZE CONFIG_TC32_BOOT_MIRROR_SIZE
/*
 * Extra low SRAM immediately after the boot-mirrored prefix is reserved for
 * instruction-cache tag/data scratch:
 *   IC tag  = 0x100 bytes
 *   IC data = 0x800 bytes
 */
#define TC32_BOOT_ICACHE_SCRATCH_SIZE 0x900

#define MPU_MIN_SIZE_ALIGN
#define MPU_ALIGN(region_size) . = ALIGN(4)

MEMORY
{
	FLASH (rx)  : ORIGIN = ROM_BASE, LENGTH = ROM_SIZE
	BOOT_MIRROR (rwx) : ORIGIN = RAM_BASE, LENGTH = TC32_BOOT_MIRROR_SIZE
	RAM   (rwx) : ORIGIN = RAM_BASE + TC32_BOOT_MIRROR_SIZE, LENGTH = RAM_SIZE - TC32_BOOT_MIRROR_SIZE
	RAM_CODE (rwx) : ORIGIN = 0x00880000, LENGTH = 64K
	LINKER_DT_REGIONS()
	IDT_LIST (wx) : ORIGIN = 0xFFFFF7FF, LENGTH = 2K
}

ENTRY(CONFIG_KERNEL_ENTRY)

PROVIDE(__memory_base = ORIGIN(RAM));
PROVIDE(__memory_size = LENGTH(RAM));

SECTIONS
{
#include <zephyr/linker/rel-sections.ld>

	. = ROM_BASE;
	__text_region_start = .;

	SECTION_PROLOGUE(.vectors,,)
	{
		KEEP(*(.vectors))
		KEEP(*(".vectors.*"))
	} GROUP_LINK_IN(ROMABLE_REGION)

	. = ALIGN(4);
	_tc32_flash_copy_lma_start = .;
	.tc32_flash_copy ORIGIN(RAM_CODE) + _tc32_flash_copy_lma_start :
	{
		*(.tc32_flash_copy)
		*(".tc32_flash_copy.*")
	} GROUP_DATA_LINK_IN(RAM_CODE, ROMABLE_REGION)
	. = _tc32_flash_copy_lma_start + SIZEOF(.tc32_flash_copy);

	SECTION_PROLOGUE(.boot_ram_mirror,,)
	{
		. = ALIGN(4);
		PROVIDE(__boot_ram_mirror_start = .);
		*(.boot_ram_mirror)
		*(".boot_ram_mirror.*")
		PROVIDE(__boot_ram_mirror_end = .);
	} GROUP_LINK_IN(ROMABLE_REGION)

	/*
	 * .ram_code — low-flash region that the TC32 icache (configured at
	 * reset.S "SET_IC") locks into its cache lines, giving
	 * deterministic single-cycle instruction fetch for hot paths such as
	 * the 802.15.4 RF IRQ handler.  Symbols stay at FLASH addresses (no
	 * LMA/VMA split, no software copy, no veneer thunks).  Anything
	 * before _tc32_boot_copy_lma_end is also covered by the boot-ROM
	 * auto-copy and by the icache range that reset.S derives from
	 * _tc32_boot_copy_size_align_256_.
	 */
	SECTION_PROLOGUE(.ram_code,,)
	{
		. = ALIGN(4);
		PROVIDE(__ram_code_start = .);
		*(.ram_code)
		*(".ram_code.*")
		PROVIDE(__ram_code_end = .);
	} GROUP_LINK_IN(ROMABLE_REGION)

	. = ALIGN(16);
	_tc32_boot_copy_lma_end = .;
	PROVIDE(_rstored_ = .);
	PROVIDE(_ramcode_size_ = .);
	PROVIDE(_ramcode_size_align_256_ = ((. + 255) / 256) * 256);
	PROVIDE(_ramcode_size_div_16_align_256_ = _ramcode_size_align_256_ / 16);

	SECTION_PROLOGUE(_TEXT_SECTION_NAME,,)
	{
		. = ALIGN(4);
		/*
		 * Input sections a board or application puts first
		 * (zephyr_linker_sources(TEXT_SECTIONS ...)): code run often
		 * enough that it should sit together in the instruction cache.
		 */
#include <snippets-text-sections.ld>
		*(.text)
		*(".text.*")
	} GROUP_LINK_IN(ROMABLE_REGION)

	__text_region_end = .;
	. = ALIGN(4);
	_tc32_rom_data_lma_start = .;
	__rodata_region_start = .;

#include <zephyr/linker/common-rom.ld>
#include <snippets-rom-sections.ld>
#include <zephyr/linker/thread-local-storage.ld>

	SECTION_PROLOGUE(_RODATA_SECTION_NAME,,)
	{
		. = ALIGN(4);
		*(.srodata)
		*(".srodata.*")
		*(.rodata)
		*(".rodata.*")
		*(.gnu.linkonce.r.*)
		*(.rodata1)
#include <snippets-rodata.ld>
#include <zephyr/linker/kobject-rom.ld>
		. = ALIGN(4);
	}
	GROUP_LINK_IN(ROMABLE_REGION)

#include <zephyr/linker/cplusplus-rom.ld>

	__rodata_region_end = .;
	__rodata_region_load_start = _tc32_rom_data_lma_start;
	_tc32_rom_data_lma_end = LOADADDR(_RODATA_SECTION_NAME) + SIZEOF(_RODATA_SECTION_NAME);

	_image_ram_start = ORIGIN(RAM);
	. = ORIGIN(RAM);

#include <snippets-ram-sections.ld>

	SECTION_DATA_PROLOGUE(_DATA_SECTION_NAME,,)
	{
		. = ALIGN(4);
		__data_region_start = .;
		__data_start = .;
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d.*)
		*(.sdata)
		*(.sdata.*)
		. = ALIGN(8);
		SORT(CONSTRUCTORS)
#include <snippets-rwdata.ld>
		__data_end = .;
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

	__data_size = __data_end - __data_start;
	/*
	 * LLD reports LOADADDR(datas) as the section end for this
	 * GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION) output section.
	 * The data image is contiguous with rodata in flash, so use the
	 * explicitly tracked rodata LMA instead.  Otherwise TC32 crt0 copies
	 * .data from 0x<data_lma_end>, leaving initialized globals zero/garbled.
	 */
	__data_load_start = _tc32_rom_data_lma_end;
	__data_region_load_start = _tc32_rom_data_lma_end;
	_tc32_data_lma_end = _tc32_rom_data_lma_end + SIZEOF(_DATA_SECTION_NAME);

#include <zephyr/linker/common-ram.ld>
#include <zephyr/linker/kobject-data.ld>
#include <zephyr/linker/cplusplus-ram.ld>
#include <snippets-data-sections.ld>

	__data_region_end = .;

#include <snippets-sections.ld>

	SECTION_DATA_PROLOGUE(_BSS_SECTION_NAME,(NOLOAD),)
	{
		. = ALIGN(4);
		__bss_start = .;
		*(.dynbss)
		*(.sbss)
		*(.sbss.*)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b.*)
		*(.scommon)
		COMMON_SYMBOLS
		__bss_end = ALIGN(4);
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)

#include <zephyr/linker/common-noinit.ld>
#include <zephyr/linker/ram-end.ld>

	GROUP_END(RAMABLE_REGION)

#include <zephyr/linker/debug-sections.ld>

	.TC32.attributes 0 :
	{
		*(.TC32.attributes)
	}

	.ARM.attributes 0 :
	{
		KEEP(*(.ARM.attributes))
	}

#if defined(CONFIG_LLVM_USE_LLD)
	.symtab 0 : { *(.symtab) }
	.strtab 0 : { *(.strtab) }
	.shstrtab 0 : { *(.shstrtab) }
#endif

	/*
	 * Nothing unwinds on the TC32. The Thumb build's ARM unwind tables would
	 * otherwise be placed as orphans, possibly between rodata and data in
	 * flash, where __data_region_load_start above does not expect them.
	 */
	/DISCARD/ :
	{
		*(.ARM.exidx*)
		*(.ARM.extab*)
	}

	PROVIDE(_stack_end_ = RAM_BASE + RAM_SIZE);
	PROVIDE(_ram_end_ = .);
	/*
	 * Mirror only the early bootstrap prefix that must execute from the TLSR
	 * low SRAM alias before reset.S switches to the canonical flash/XIP path.
	 * Normal .text now runs directly from flash under XIP. The low SRAM
	 * reserve must still leave room after that prefix for the TLSR827x
	 * instruction-cache tag/data scratch window used by the flash execution
	 * path.
	 */
	PROVIDE(_tc32_boot_copy_size_ = _tc32_boot_copy_lma_end);
	PROVIDE(_tc32_boot_copy_size_align_256_ = ((_tc32_boot_copy_size_ + 255) / 256) * 256);
	PROVIDE(_tc32_boot_copy_size_div_16_align_256_ = _tc32_boot_copy_size_align_256_ / 16);
	PROVIDE(_tc32_icache_lines_ = _tc32_boot_copy_size_align_256_ / 256);
	PROVIDE(_ictag_start_ = RAM_BASE + _tc32_boot_copy_size_align_256_);
	PROVIDE(_ictag_end_ = _ictag_start_ + 0x100);
	PROVIDE(_tc32_boot_mirror_runtime_min_ = _tc32_boot_copy_size_align_256_ + TC32_BOOT_ICACHE_SCRATCH_SIZE);
	ASSERT(_tc32_boot_mirror_runtime_min_ <= TC32_BOOT_MIRROR_SIZE, "TC32 boot mirror reserve is too small for XIP bootstrap + icache scratch")
	/*
	 * The startup in soc/telink/tlsr/tlsr827x/setup/tlsr8278_startup.S
	 * zeroes .bss and copies .data a word at a time up to the end symbol,
	 * copies nothing else, fills the first 32 bytes of .data before the
	 * copy overwrites them, and its loader fills the RAM code 16 bytes at
	 * a time from 0xd0.
	 */
	ASSERT((__bss_start % 4) == 0 && (__bss_end % 4) == 0, "tlsr8278 startup: .bss is not word aligned")
	ASSERT((__data_region_start % 4) == 0 && (__data_region_end % 4) == 0 && (__data_region_load_start % 4) == 0,
	       "tlsr8278 startup: .data is not word aligned")
	ASSERT(__ramfunc_end == __ramfunc_region_start, "tlsr8278 startup: .ramfunc is not empty and nothing copies it")
	ASSERT(__data_region_end - __data_region_start >= 0x20, "tlsr8278 startup: .data is shorter than the 32 bytes the startup fills before the copy")
	ASSERT(_tc32_boot_copy_size_align_256_ >= 0x1c4, "tlsr8278 startup: the RAM code ends inside the startup")
}
