/*
 * cnbox ci controller handling.
 *
 * gpl
 *
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <linux/platform_device.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/firmware.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#include <linux/stpio.h>
#else
#include <linux/stm/pio.h>
#endif

#include <asm/system.h>
#include <asm/io.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/dvb/dmx.h>

#include "dvb_frontend.h"
#include "dvbdev.h"
#include "demux.h"
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvb_net.h"
#include "dvb_ca_en50221.h"

#include "cnbox_cic.h"

static int debug=0;
static int extmoduldetect = 0;

#define TAGDEBUG "[cnbox_cic] "

#define dprintk(level, x...) \
do \
{ \
	if ((debug) && (debug >= level)) \
	{ \
		printk(TAGDEBUG x); \
	} \
} while (0)

#define EMIConfigBaseAddress 0xfe700000

#define EMI_LCK           0x0020
#define EMI_GEN_CFG       0x0028

#define EMI_FLASH_CLK_SEL 0x0050  /* WO: 00, 10, 01 */
#define EMI_CLK_EN        0x0068  /* WO: must only be set once !!*/

#define EMIBank0          0x100
#define EMIBank1          0x140
#define EMIBank2          0x180
#define EMIBank3          0x1C0
#define EMIBank4          0x200
#define EMIBank5          0x240  /* virtual */

#define EMI_CFG_DATA0     0x0000
#define EMI_CFG_DATA1     0x0008
#define EMI_CFG_DATA2     0x0010
#define EMI_CFG_DATA3     0x0018

unsigned long reg_config = 0;

/* ***** end 7111 emi */

volatile unsigned long slot_attr_mem[cNumberSlots];
volatile unsigned long slot_ctrl_mem[cNumberSlots];

static struct cnbox_cic_core ci_core;
static struct cnbox_cic_state ci_state;

static int waitMS = 200;

static int cnbox_cic_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address);

/* *************************** */
/* map, write & read functions */
/* *************************** */
#define address_not_mapped

unsigned char cnbox_read_register_u8(unsigned long address)
{
	unsigned char result;

#ifdef address_not_mapped
	volatile unsigned long mapped_register = (unsigned long)ioremap_nocache(address, 1);
#else
	volatile unsigned long mapped_register = address;
#endif

	dprintk(200, "%s > address = 0x%.8lx, mapped = 0x%.8lx\n", __func__, (unsigned long)address, mapped_register);
	result = readb(mapped_register);
#ifdef address_not_mapped
	iounmap((void*)mapped_register);
#endif
	return result;
}

void cnbox_write_register_u8(unsigned long address, unsigned char value)
{
#ifdef address_not_mapped
	volatile unsigned long mapped_register = (unsigned long)ioremap_nocache(address, 1);
#else
	volatile unsigned long mapped_register = address;
#endif

	writeb(value, mapped_register);
#ifdef address_not_mapped
	iounmap((void*)mapped_register);
#endif
}

void cnbox_write_register_u32(unsigned long address, unsigned int value)
{
#ifdef address_not_mapped
	volatile unsigned long mapped_register = (unsigned long)ioremap_nocache(address, 4);
#else
	volatile unsigned long mapped_register = address;
#endif

	writel(value, mapped_register);
#ifdef address_not_mapped
	iounmap((void*)mapped_register);
#endif
}

u32 cnbox_read_register_u32(unsigned long address)
{
	u32 res;
#ifdef address_not_mapped
	volatile unsigned long mapped_register = (unsigned long)ioremap_nocache(address, 4);
#else
	volatile unsigned long mapped_register = address;
#endif

	res = readl(mapped_register);
#ifdef address_not_mapped
	iounmap((void*)mapped_register);
#endif
	return res;
}

#if 0
void  cnbox_irq(struct stpio_pin *pin, void* dev_id)
{
	printk("%s\n", __func__);
}
#endif

static int cnbox_cic_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct cnbox_cic_state *state = ca->data;
	int                     slot_status = 0;
	unsigned int            result;

	dprintk(100, "%s (%d; open = %d) >\n", __func__, slot, open);

	result = stpio_get_pin(state->module_detect);

	dprintk(100, "Slot %d Status = 0x%x\n", slot, result);

	if (result == 0x00)
	{
		slot_status = 1;
	}
	if (slot_status != state->module_present[slot])
	{
		if (slot_status)
		{
			stpio_set_pin(state->ci_enable, 1);
			msleep(waitMS);
			stpio_set_pin(state->slot_reset[slot], 1);
			msleep(waitMS);
			stpio_set_pin(state->slot_reset[slot], 0);
			msleep(waitMS * 2);
			dprintk(1, "Module now present\n");
			state->module_present[slot] = 1;
		}
		else
		{
			dprintk(1, "Module now not present\n");
			state->module_present[slot] = 0;
			stpio_set_pin(state->ci_enable, 0);
			stpio_set_pin(state->slot_reset[slot], 0);
		}
	}

	if (slot_status == 1)
	{
		if (state->module_ready[slot] == 0)
		{
			if (state->detection_timeout[slot] == 0)
			{
				/* detected module insertion, set the detection
				 *  timeout (500 ms)
				 */
				state->detection_timeout[slot] = jiffies + HZ / 2;
			}
			else
			{
				/* timeout in progress */
				if (time_after(jiffies, state->detection_timeout[slot]))
				{
					/* timeout expired, report module present */
					state->module_ready[slot] = 1;
				}
			}
		}
		/* else: state->module_ready[slot] == 1 */
	}
	else
	{
		/* module not present, reset everything */
		state->module_ready[slot] = 0;
		state->detection_timeout[slot] = 0;
	}
	slot_status = slot_status ? DVB_CA_EN50221_POLL_CAM_PRESENT : 0;

	if (slot_status == DVB_CA_EN50221_POLL_CAM_PRESENT)
	{
		slot_status |= DVB_CA_EN50221_POLL_CAM_READY;
	}
	dprintk(1, "Module %c (%d): present = %d, ready = %d\n", slot ? 'B' : 'A', slot, slot_status, state->module_ready[slot]);
	return slot_status;
}

static int cnbox_cic_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct cnbox_cic_state *state = ca->data;

	dprintk(100, "%s >\n", __func__);

	/* reset status variables because module detection has to
	 * be reported after a delay
	 */
	state->module_ready[slot] = 0;
	state->module_present[slot] = 0;
	state->detection_timeout[slot] = 0;

	stpio_set_pin(state->slot_reset[slot], 1);
	msleep(waitMS);
	stpio_set_pin(state->slot_reset[slot], 0);
	msleep(waitMS);
	dprintk(100, "%s <\n", __func__);
	return 0;
}

static int cnbox_cic_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
	unsigned char res = 0;

	res = cnbox_read_register_u8(slot_attr_mem[slot] + address);
	dprintk(100, "%s > slot = %d, address = 0x%.8lx\n", __func__, slot, (unsigned long)slot_attr_mem[slot] + address);
	if (address <= 2)
	{
		dprintk (100, "address = %d: res = 0x%.x\n", address, res);
	}
	else
	{
		if ((res > 31) && (res < 127))
		{
			dprintk(100, "%c", res);
		}
		else
		{
			dprintk(150, ".");
		}
	}
	return (int)res;
}

static int cnbox_cic_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	dprintk(100, "%s > slot = %d, address = 0x%.8lx, value = 0x%.x\n", __func__, slot, (unsigned long)slot_attr_mem[slot] + address, value);
	cnbox_write_register_u8(slot_attr_mem[slot] + address, value);
	return 0;
}

static int cnbox_cic_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	unsigned char res = 0;

	dprintk(100, "%s > slot = %d, address = 0x%.8lx\n", __func__, slot, (unsigned long)slot_ctrl_mem[slot] + address);
	res = cnbox_read_register_u8(slot_ctrl_mem[slot] + address);

	if (address <= 2)
	{
		dprintk (100, "address = 0x%x: res = 0x%x (0x%x)\n", address, res, (int)res);
	}
	else
	{
		if ((res > 31) && (res < 127))
		{
			dprintk(100, "%c", res);
		}
		else
		{
			dprintk(150, ".");
		}
	}
	return (int)res;
}

static int cnbox_cic_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
	dprintk(100, "%s > slot = %d, address = 0x%.8lx, value = 0x%.x\n", __func__, slot, (unsigned long)slot_ctrl_mem[slot] + address, value);
	cnbox_write_register_u8(slot_ctrl_mem[slot] + address, value);

	// without this some modules are not working (unicam evo, unicam twin, zetaCam)
	// i have tested with 9 modules an all working with this code
	if (extmoduldetect == 1 && value == 8 && address == 1)
	{
		cnbox_write_register_u8(slot_ctrl_mem[slot] + address, 0);
	}
	return 0;
}

static int cnbox_cic_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct cnbox_cic_state *state = ca->data;

	dprintk(20, "%s > slot = %d\n", __func__, slot);
	return 0;
}

static int cnbox_cic_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct cnbox_cic_state *state = ca->data;

	dprintk(20, "%s > slot = %d\n", __func__, slot);
	return 0;
}

int setCiSource(int slot, int source)
{
	return 0;
}

void getCiSource(int slot, int* source)
{
	*source = 0;
}

int setMuxSource(int source)
{
	struct cnbox_cic_state *state = &ci_state;
	u32 reg;

	printk("%s: source %d\n", __func__, source);

	reg = cnbox_read_register_u32(0xfe001114);

	/* e2 request source tuner not ci */
	if (source == 0)
	{
		cnbox_write_register_u32(0xfe001114, reg & ~0x1);
 
		stpio_set_pin(state->cam_enable, 1);
		stpio_set_pin(state->ts_enable, 0);
#if 0
		cnbox_write_register_u32(0xfd023000, 0x6b); /* pio3 */

		cnbox_write_register_u32(0xfd022030, 0xf3); /* pio2 */
		cnbox_write_register_u32(0xfd022020, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022040, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022000, 0xfb); /* pio2 */

		cnbox_write_register_u32(0xfd025030, 0x8f); /* pio5 */
		cnbox_write_register_u32(0xfd025020, 0x0); /* pio5 */
		cnbox_write_register_u32(0xfd025040, 0xc0); /* pio5 */
		cnbox_write_register_u32(0xfd025000, 0x2f); /* pio5 */

		cnbox_write_register_u32(0xfd021030, 0x84); /* pio1 */
		cnbox_write_register_u32(0xfd021020, 0x80); /* pio1 */
		cnbox_write_register_u32(0xfd021040, 0x80); /* pio1 */
		cnbox_write_register_u32(0xfd021000, 0x4); /* pio1 */

		cnbox_write_register_u32(0xfd022030, 0xfb); /* pio2 */
		cnbox_write_register_u32(0xfd022020, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022040, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022000, 0xfb); /* pio2 */

		cnbox_write_register_u32(0xfd025030, 0xaf); /* pio5 */
		cnbox_write_register_u32(0xfd025020, 0x0); /* pio5 */
		cnbox_write_register_u32(0xfd025040, 0xc0); /* pio5 */
		cnbox_write_register_u32(0xfd025000, 0x2f); /* pio5 */
#endif
	}
	else
	{
		/* source = CI0 = descrambled service */
		cnbox_write_register_u32(0xfe001114, reg | 0x1);

		stpio_set_pin(state->cam_enable, 0);
		stpio_set_pin(state->ts_enable, 1);
#if 0
		cnbox_write_register_u32(0xfd023000, 0x63); /* pio3 */

		cnbox_write_register_u32(0xfe001114, 0x3f820101);

		cnbox_write_register_u32(0xfd022030, 0xb); /* pio2 */
		cnbox_write_register_u32(0xfd022020, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022040, 0x3); /* pio2 */

		cnbox_write_register_u32(0xfd025030, 0xa0); /* pio5 */
		cnbox_write_register_u32(0xfd025020, 0x00); /* pio5 */
		cnbox_write_register_u32(0xfd025040, 0xc0); /* pio5 */

		cnbox_write_register_u32(0xfd021030, 0x80); /* pio1 */
		cnbox_write_register_u32(0xfd021020, 0x80); /* pio1 */
		cnbox_write_register_u32(0xfd021040, 0x80); /* pio1 */

		cnbox_write_register_u32(0xfd022030, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022020, 0x3); /* pio2 */
		cnbox_write_register_u32(0xfd022040, 0x3); /* pio2 */

		cnbox_write_register_u32(0xfd025030, 0x80); /* pio5 */
		cnbox_write_register_u32(0xfd025020, 0x00); /* pio5 */
		cnbox_write_register_u32(0xfd025040, 0xc0); /* pio5 */
#endif
	}
	return 0;
}

int cic_init_hw(void)
{
	struct cnbox_cic_state *state = &ci_state;
	int i;
	u32 reg;

	state->ci_enable     = stpio_request_pin (1, 3, "CI_ENABLE", STPIO_OUT);
	state->slot_reset[0] = stpio_request_pin (3, 6, "SLOT_RESET", STPIO_OUT);
	state->module_detect = stpio_request_pin (1, 5, "CI_DETECT", STPIO_IN);
	state->cam_enable    = stpio_request_pin (4, 6, "CAM_ENABLE", STPIO_OUT);
	state->ts_enable     = stpio_request_pin (6, 1, "TS_ENABLE", STPIO_OUT);

	if (state->cam_enable == NULL)
	{
		printk("\n\nCAM_ENABLE port open failed(4,6)!\n\n\n");
		while(1);
	}
	if (state->ts_enable == NULL)
	{
		printk("\n\nTS_ENABLE port open failed(6,1)!\n\n\n");
		while(1);
	}

	stpio_set_pin(state->ci_enable, 0);
	stpio_set_pin(state->slot_reset[0], 0);

	cnbox_write_register_u32(EMIConfigBaseAddress + EMIBank2 + EMI_CFG_DATA0, 0xc447f9);
	cnbox_write_register_u32(EMIConfigBaseAddress + EMIBank2 + EMI_CFG_DATA1, 0xff86a8a8);
	cnbox_write_register_u32(EMIConfigBaseAddress + EMIBank2 + EMI_CFG_DATA2, 0xff86a8a8);
	cnbox_write_register_u32(EMIConfigBaseAddress + EMIBank2 + EMI_CFG_DATA3, 0xa);
	cnbox_write_register_u32(EMIConfigBaseAddress + EMI_GEN_CFG, 0x08);

	reg = cnbox_read_register_u32(0xfe001100);
	reg |= (1 << 0);
	cnbox_write_register_u32(0xfe001100, reg);
	reg = cnbox_read_register_u32(0xfe001114);
	reg |= (1 << 0) | (1 << 17) | (1 << 23);
	cnbox_write_register_u32(0xfe001114, reg);
	stpio_set_pin(state->cam_enable, 1);
	stpio_set_pin(state->ts_enable, 0);
	cnbox_write_register_u32(0xfe001134, 0x600000); /* lmi */
	cnbox_write_register_u32(0xfe0011a8, 0x66f379b); /* lmi */
	cnbox_write_register_u32(0xfe0011ac, 0x1800019b); /* lmi */
	slot_attr_mem[0] = 0x6008000;
	slot_ctrl_mem[0] = 0x6000000;

	for (i = 0; i < cNumberSlots; i++)
	{
		state->module_ready[i] = 0;
		state->module_present[i] = 0;
		state->detection_timeout[i] = 0;
	}
	stpio_set_pin(state->ci_enable, 1);

#if 0
	if (stpio_flagged_request_irq(state->slot_reset[0], 0, cnbox_irq, NULL ,IRQ_DISABLED) < 0)
	{
		printk("%s: failed to init irq\n", __func__);
	}
	if (stpio_flagged_request_irq(state->ci_enable, 0, cnbox_irq, NULL ,IRQ_DISABLED) < 0)
	{
		printk("%s: failed to init irq\n", __func__);
	}
#endif
	return 0;
}
EXPORT_SYMBOL(cic_init_hw);

int init_ci_controller(struct dvb_adapter* dvb_adap)
{
	struct cnbox_cic_state *state = &ci_state;
	struct cnbox_cic_core *core = &ci_core;
	int result;

	printk("init_cnbox_cic >\n");
	core->dvb_adap = dvb_adap;
	printk("dvb_adap %p\n", dvb_adap);
	printk("core->dvb_adap %p\n", core->dvb_adap);
	memset(&core->ca, 0, sizeof(struct dvb_ca_en50221));
	printk("core->dvb_adap %p\n", core->dvb_adap);

	/* register CI interface */
	core->ca.owner = THIS_MODULE;

	core->ca.read_attribute_mem   = cnbox_cic_read_attribute_mem;
	core->ca.write_attribute_mem  = cnbox_cic_write_attribute_mem;
	core->ca.read_cam_control 	  = cnbox_cic_read_cam_control;
	core->ca.write_cam_control 	  = cnbox_cic_write_cam_control;
	core->ca.slot_shutdown 		  = cnbox_cic_slot_shutdown;
	core->ca.slot_ts_enable 	  = cnbox_cic_slot_ts_enable;

	core->ca.slot_reset 		  = cnbox_cic_slot_reset;
	core->ca.poll_slot_status 	  = cnbox_cic_poll_slot_status;

	state->core 			      = core;
	core->ca.data 			      = state;
	printk("core->dvb_adap %p\n", core->dvb_adap);

	cic_init_hw();
	printk("core->dvb_adap %p\n", core->dvb_adap);

	dprintk(1, "init_cnbox_cic: call dvb_ca_en50221_init\n");

	printk("%p %d\n", core->dvb_adap, cNumberSlots);
	if ((result = dvb_ca_en50221_init(core->dvb_adap, &core->ca, 0, cNumberSlots)) != 0)
	{
		printk(KERN_ERR "ca0 initialisation failed.\n");
		goto error;
	}
	dprintk(1, "cnbox_cic: ca0 interface initialised.\n");
	dprintk(10, "init_cnbox_cic <\n");
	return 0;

error:
	printk("init_cnbox_cic < error\n");
	return result;
}
EXPORT_SYMBOL(init_ci_controller);
EXPORT_SYMBOL(setMuxSource);
EXPORT_SYMBOL(setCiSource);
EXPORT_SYMBOL(getCiSource);

int __init cnbox_cic_init(void)
{
	printk("cnbox_cic loaded\n");
	return 0;
}

static void __exit cnbox_cic_exit(void)
{
	printk("cnbox_cic unloaded\n");
}

module_init (cnbox_cic_init);
module_exit (cnbox_cic_exit);

MODULE_DESCRIPTION ("CI Controller");
MODULE_AUTHOR ("konfetti");
MODULE_LICENSE ("GPL");

module_param(debug, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(debug, "Debug Output 0=disabled >0=enabled(debuglevel)");

module_param(extmoduldetect, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(extmoduldetect, "Ext. Modul detect 0=disabled 1=enabled");

module_param(waitMS, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(waitMS, "waiting time between pio settings for reset/enable purpos in milliseconds (default=200)");
// vim:ts=4
