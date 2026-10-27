#include <dm.h>
#include <sdhci.h>
#include <stdio.h>
#include <linux/delay.h>
#include <linux/errno.h>

struct mj_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

#if CONFIG_IS_ENABLED(MMC_SUPPORTS_TUNING)
#define MAX_TUNING_LOOP	40

static void sdhci_start_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl |= SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_end_tuning(struct sdhci_host *host)
{
	/* Enable only interrupts served by the SD controller */
	sdhci_writel(host, SDHCI_INT_DATA_MASK | SDHCI_INT_CMD_MASK,
		     SDHCI_INT_ENABLE);
	/* Mask all sdhci interrupt sources */
	sdhci_writel(host, 0x0, SDHCI_SIGNAL_ENABLE);
}

static int mj_sdhci_execute_tuning(struct mmc *mmc, u8 opcode)
{
	struct mmc_cmd cmd;
	u16 ctrl;
	u32 blocksize = SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, 64);
	struct sdhci_host *host = dev_get_priv(mmc->dev);
	int i;
	int ret;

	sdhci_start_tuning(host);

	cmd.cmdidx = opcode;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = 0;

	if (opcode == MMC_CMD_SEND_TUNING_BLOCK_HS200 && mmc->bus_width == 8)
		blocksize = SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, 128);

	sdhci_writew(host, blocksize, SDHCI_BLOCK_SIZE);
	sdhci_writew(host, 1, SDHCI_BLOCK_COUNT);
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

	for (i = 0; i < MAX_TUNING_LOOP; i++) {
		ret = mmc_send_cmd(mmc, &cmd, NULL);
		if (ret)
			goto out;

		/* Allow CLK/DATA sampling to settle between SD tuning commands. */
		if (opcode == MMC_CMD_SEND_TUNING_BLOCK)
			udelay(1);

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
			ret = (ctrl & SDHCI_CTRL_TUNED_CLK) ? 0 : -EIO;
			goto out;
		}
	}

	ret = -ETIMEDOUT;

out:
	if (ret) {
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		ctrl &= ~(SDHCI_CTRL_TUNED_CLK | SDHCI_CTRL_EXEC_TUNING);
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
		printf("%s: Tuning failed, opcode = 0x%02x, error = %d\n",
		       __func__, opcode, ret);
	}

	sdhci_end_tuning(host);

	return ret;
}
#endif

static const struct sdhci_ops mj_sdhci_ops = {
#if CONFIG_IS_ENABLED(MMC_SUPPORTS_TUNING)
	.platform_execute_tuning = mj_sdhci_execute_tuning,
#endif
};

static int mj_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct mj_sdhci_plat *plat = dev_get_plat(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	int ret;

	host->name = dev->name;

	host->ioaddr = dev_read_addr_ptr(dev);

	host->bus_width = dev_read_u32_default(dev, "bus-width", 1);

	/* Discover the input clock from SDHCI capabilities, not the card limit. */
	host->max_clk = 0;

	ret = mmc_of_parse(dev, &plat->cfg);
	if (ret)
		return ret;

	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	host->ops = &mj_sdhci_ops;
	host->quirks = SDHCI_QUIRK_BROKEN_VOLTAGE |
		       SDHCI_QUIRK_32BIT_DMA_ADDR |
		       SDHCI_QUIRK_NO_1_8_V;

	host->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;

	ret = sdhci_setup_cfg(&plat->cfg, host, plat->cfg.f_max, 400000);
	if (ret)
		return ret;

	/* The common SDHCI setup adds 4-bit support; enforce the board wiring. */
	if (host->bus_width == 1)
		plat->cfg.host_caps &= ~(MMC_MODE_4BIT | MMC_MODE_8BIT);
	else if (host->bus_width == 4)
		plat->cfg.host_caps &= ~MMC_MODE_8BIT;

	host->mmc->priv = host;
	upriv->mmc = host->mmc;

	return sdhci_probe(dev);
}

static int mj_sdhci_bind(struct udevice *dev)
{
	struct mj_sdhci_plat *plat = dev_get_plat(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id mj_sdhci_ids[] = {
	{ .compatible = "snps,dw-mshci" },
	{ }
};

U_BOOT_DRIVER(mj_sdhci_drv) = {
	.name = "mj_sdhci",
	.id = UCLASS_MMC,
	.of_match = mj_sdhci_ids,
	.ops = &sdhci_ops,
	.bind = mj_sdhci_bind,
	.probe = mj_sdhci_probe,
	.priv_auto = sizeof(struct sdhci_host),
	.plat_auto = sizeof(struct mj_sdhci_plat),
};
