/* linux/driver/misc/nuc980-ebi.c
 *
 * Copyright (c) 2018 Nuvoton technology corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#if 0
#define ENTRY()                                 printk("[%-20s] : Enter...\n", __FUNCTION__)
#define LEAVE()                                 printk("[%-20s] : Leave...\n", __FUNCTION__)
#else
#define ENTRY()
#define LEAVE()
#endif

#if 0
#define DEBUG(fmt, args...)     printk(fmt, ##args)
#else
#define DEBUG(fmt, args...)     do {} while (0)
#endif



#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <mach/map.h>
#include <mach/regs-gcr.h>
#include <mach/regs-clock.h>
#include <mach/regs-ebi.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/of.h>


#define SRAM_GRANULARITY	32

struct ebi_dev {
	int minor;	// dynamic minor num, so we need this to distinguish between channels
	struct pinctrl *pinctrl;
	struct clk *clk;
	struct clk *hclk;
	unsigned long base_addr[4];
	int bank;
};

static struct ebi_dev *ebi;

static long ebi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct nuc980_set_ebi *pebi,sebi;
	struct ebi_dev *nuc980_ebi = (struct ebi_dev *)filp->private_data;
	pebi=&sebi;
	switch(cmd) {
	case EBI_IOC_SET:
		if(copy_from_user((void *)&sebi, (const void *)arg, sizeof(struct nuc980_set_ebi)))
			return -EFAULT;
		//pebi=(struct nuc980_set_ebi *)(arg);
		DEBUG("set bank=%d\n",pebi->bank);

#ifdef CONFIG_NUC980_EBI_TIMING_FASTEST
		pebi->timing = EBI_TIMING_FASTEST;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_VERYFAST
		pebi->timing = EBI_TIMING_VERYFAST;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_FAST
		pebi->timing =EBI_TIMING_FAST;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_NORMAL
		pebi->timing = EBI_TIMING_NORMAL;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_SLOW
		pebi->timing = EBI_TIMING_SLOW;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_VERYSLOW
		pebi->timing = EBI_TIMING_VERYSLOW;
#endif
#ifdef CONFIG_NUC980_EBI_TIMING_SLOWEST
		pebi->timing = EBI_TIMING_SLOWEST;
#endif

		nuc980_set_ebi_ctl(pebi->bank, pebi->width, pebi->timing, pebi->busmode, pebi->CSActiveLevel);
		nuc980_ebi->bank = pebi->bank;
		nuc980_ebi->base_addr[nuc980_ebi->bank] = pebi->base;

		DEBUG("bank[%d]: width=%d, timing=0x%x, busmode=%d, CSActiveLevel=%d, base=0x%x\n",pebi->bank, pebi->width, pebi->timing, pebi->busmode, pebi->CSActiveLevel, pebi->base);
		// DEBUG("REG_EBI_CTL=0x%08x\n",__raw_readl(REG_EBI_CTL(nuc980_ebi->bank)));
		// DEBUG("REG_EBI_TCTL=0x%08x\n",__raw_readl(REG_EBI_TCTL(nuc980_ebi->bank)));
		break;
	}
	return 0;
}
static int ebi_open(struct inode *inode, struct file *filp)
{
	ENTRY();
	filp->private_data = ebi;
	ebi->hclk = clk_get(NULL, "ebi_hclk");
	if (IS_ERR(ebi->hclk)) {
		printk("failed to get ebi clock mux\n");
		return -EAGAIN;
	}
	clk_prepare(ebi->hclk);
	clk_enable(ebi->hclk);

	LEAVE();
	return 0;
}

static ssize_t ebi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	return 0;
}
static int ebi_release(struct inode *inode, struct file *filp)
{
	ENTRY();
	clk_disable(ebi->hclk);
	clk_put(ebi->hclk);
	filp->private_data = NULL;
	LEAVE();
	return 0;
}
static int ebi_mmap(struct file *filp, struct vm_area_struct * vma);
struct file_operations ebi_fops = {
	.owner		= THIS_MODULE,
	.open		= ebi_open,
	.release	= ebi_release,
	.read		= ebi_read,
	.mmap = ebi_mmap,
	.unlocked_ioctl	= ebi_ioctl,
};

static struct miscdevice ebi_dev[] = {
	[0] = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "ebi",
		.fops = &ebi_fops,
	},
};

static int ebi_mmap(struct file *filp, struct vm_area_struct * vma)
{
	struct ebi_dev *nuc980_ebi = (struct ebi_dev *)filp->private_data;
	unsigned long pageFrameNo = 0, size;

	ENTRY();
	pageFrameNo = __phys_to_pfn(nuc980_ebi->base_addr[nuc980_ebi->bank]);

	size = vma->vm_end - vma->vm_start;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);
	if (remap_pfn_range(vma, vma->vm_start, pageFrameNo,size, vma->vm_page_prot)) {
		printk(KERN_INFO "nuc980_mem_mmap() : remap_pfn_range() failed !\n");
		return -EINVAL;
	}
	LEAVE();
	return 0;
}

static int nuc980_ebi_probe(struct platform_device *pdev)
{
	struct ebi_dev *nuc980_ebi;
	ENTRY();

	dev_info(&pdev->dev, "NUC980 EBI\n");

	nuc980_ebi = devm_kzalloc(&pdev->dev, sizeof(*nuc980_ebi), GFP_KERNEL);
	if (!nuc980_ebi)
		return -ENOMEM;

	nuc980_ebi->clk = devm_clk_get(&pdev->dev, "ebi_hclk");
	if (IS_ERR(nuc980_ebi->clk))
		nuc980_ebi->clk = NULL;
	else
		clk_prepare_enable(nuc980_ebi->clk);

	misc_register(&ebi_dev[0]);
	nuc980_ebi->pinctrl = devm_pinctrl_get(&pdev->dev);
	nuc980_ebi->minor = MINOR(ebi_dev[0].minor);

	DEBUG("nuc980_ebi->minor=%d\n",nuc980_ebi->minor);
	ebi=nuc980_ebi;
	platform_set_drvdata(pdev, nuc980_ebi);

	LEAVE();
	return 0;
}

static int nuc980_ebi_remove(struct platform_device *pdev)
{
	ENTRY();
	misc_deregister(&ebi_dev[0]);
	LEAVE();
	return 0;
}

static const struct of_device_id nuc980_ebi_of_match[] = {
	{ .compatible = "nuvoton,nuc980-ebi" },
	{},
};

static struct platform_driver nuc980_ebi_driver = {
	.driver = {
		.name = "nuc980-ebi",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(nuc980_ebi_of_match),
	},
	.probe = nuc980_ebi_probe,
	.remove = nuc980_ebi_remove,
};

module_platform_driver(nuc980_ebi_driver);
MODULE_AUTHOR("Nuvoton Technology Corp.");
MODULE_ALIAS("platform:nuc980-ebi");
MODULE_LICENSE("GPL");
