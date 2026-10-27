#include <config.h>
#include <fdtdec.h>
#include <init.h>
#include <linux/sizes.h>
#include <asm/armv8/mmu.h>

/* Identity mappings for UART, SDHCI and both DDR windows. */
static struct mm_region mj_mem_map[] = {
	{
		.virt = 0x06430000UL,
		.phys = 0x06430000UL,
		.size = SZ_4K,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE | PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	},
	{
		.virt = CFG_SYS_SDRAM_BASE,
		.phys = CFG_SYS_SDRAM_BASE,
		.size = SZ_1G,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	},
	{
		.virt = 0x80000000UL,
		.phys = 0x80000000UL,
		.size = SZ_2G,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	},
	{
		.virt = 0x0a000000UL,
		.phys = 0x0a000000UL,
		.size = SZ_1M,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE | PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	},
	{ 0 }
};

struct mm_region *mem_map = mj_mem_map;

int board_init(void)
{
	return 0;
}

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

int dram_init_banksize(void)
{
	return fdtdec_setup_memory_banksize();
}
