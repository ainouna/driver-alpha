/*
 * STV0900/0903 Multistandard Broadcast Frontend driver
 * Copyright (C) Manu Abraham <abraham.manu@gmail.com>
 *
 * Copyright (C) ST Microelectronics
 *
 * Version for:
 * ADB ITI-2849/2850/2851S(T) STV0903 with tuner STV6110X and
 *                            LNBH23P or MP8125 LNB power controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#if !defined(ADB_2850)
#warning: Wrong receiver model!
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <linux/interrupt.h>

#include <linux/dvb/version.h>

#include <asm/io.h>  /* ctrl_xy */

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#include "stv6110x.h"  /* for demodulator internal modes */

#include "stv090x_reg.h"
#include "stv090x.h"
#include "stv090x_priv.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#  include <linux/stpio.h>
#else
#  include <linux/stm/pio.h>
#endif

#include <linux/gpio.h>

#define MODEL_2850 0
#define MODEL_2849 1

extern char box_type;  // selects LNB power controller: LNBH23 or MP8125

extern int bbgain;
extern short paramDebug;
#define TAGDEBUG "[stv090x] "

#define dprintk(level, x...) do \
	{ \
		if ((paramDebug) && (paramDebug > level)) \
		{ \
			printk(TAGDEBUG x); \
		} \
	} while (0)

static unsigned int verbose = FE_DEBUGREG;

#if defined(ADB_2850)
	#define LNBH23_AUX   0x01
	#define LNBH23_ITEST 0x02
	#define LNBH23_EN    0x04
	#define LNBH23_VSEL  0x08
	#define LNBH23_LLC   0x10
	#define LNBH23_TEN   0x20
	#define LNBH23_TTX   0x40
	#define LNBH23_PCL   0x80
	static unsigned char lnbh23_v = 0;
	static unsigned char lnbh23_t = LNBH23_PCL;
	int writereg_lnb_supply (struct stv090x_state *state, char data);
	static struct stpio_pin *mp8125_en;
	static struct stpio_pin *mp8125_13_18;
	static struct stpio_pin *mp8125_extm;
	static unsigned char mp8125_init = 0;
#endif

struct mutex demod_lock;

/* DVBS1 and DSS C/N Lookup table */
static const struct stv090x_tab stv090x_s1cn_tab[] =
{
	{   0, 8917 },  /*  0.0dB */
	{   5, 8801 },  /*  0.5dB */
	{  10, 8667 },  /*  1.0dB */
	{  15, 8522 },  /*  1.5dB */
	{  20, 8355 },  /*  2.0dB */
	{  25, 8175 },  /*  2.5dB */
	{  30, 7979 },  /*  3.0dB */
	{  35, 7763 },  /*  3.5dB */
	{  40, 7530 },  /*  4.0dB */
	{  45, 7282 },  /*  4.5dB */
	{  50, 7026 },  /*  5.0dB */
	{  55, 6781 },  /*  5.5dB */
	{  60, 6514 },  /*  6.0dB */
	{  65, 6241 },  /*  6.5dB */
	{  70, 5965 },  /*  7.0dB */
	{  75, 5690 },  /*  7.5dB */
	{  80, 5424 },  /*  8.0dB */
	{  85, 5161 },  /*  8.5dB */
	{  90, 4902 },  /*  9.0dB */
	{  95, 4654 },  /*  9.5dB */
	{ 100, 4417 },  /* 10.0dB */
	{ 105, 4186 },  /* 10.5dB */
	{ 110, 3968 },  /* 11.0dB */
	{ 115, 3757 },  /* 11.5dB */
	{ 120, 3558 },  /* 12.0dB */
	{ 125, 3366 },  /* 12.5dB */
	{ 130, 3185 },  /* 13.0dB */
	{ 135, 3012 },  /* 13.5dB */
	{ 140, 2850 },  /* 14.0dB */
	{ 145, 2698 },  /* 14.5dB */
	{ 150, 2550 },  /* 15.0dB */
	{ 160, 2283 },  /* 16.0dB */
	{ 170, 2042 },  /* 17.0dB */
	{ 180, 1827 },  /* 18.0dB */
	{ 190, 1636 },  /* 19.0dB */
	{ 200, 1466 },  /* 20.0dB */
	{ 210, 1315 },  /* 21.0dB */
	{ 220, 1181 },  /* 22.0dB */
	{ 230, 1064 },  /* 23.0dB */
	{ 240,  960 },  /* 24.0dB */
	{ 250,  869 },  /* 25.0dB */
	{ 260,  792 },  /* 26.0dB */
	{ 270,  724 },  /* 27.0dB */
	{ 280,  665 },  /* 28.0dB */
	{ 290,  616 },  /* 29.0dB */
	{ 300,  573 },  /* 30.0dB */
	{ 310,  537 },  /* 31.0dB */
	{ 320,  507 },  /* 32.0dB */
	{ 330,  483 },  /* 33.0dB */
	{ 400,  398 },  /* 40.0dB */
	{ 450,  381 },  /* 45.0dB */
	{ 500,  377 }   /* 50.0dB */
};

/* DVBS2 C/N Lookup table */
static const struct stv090x_tab stv090x_s2cn_tab[] =
{
	{ -30, 13348 },  /* -3.0dB */
	{ -20, 12640 },  /* -2.0dB */
	{ -10, 11883 },  /* -1.0dB */
	{   0, 11101 },  /* -0.0dB */
	{   5, 10718 },  /*  0.5dB */
	{  10, 10339 },  /*  1.0dB */
	{  15,  9947 },  /*  1.5dB */
	{  20,  9552 },  /*  2.0dB */
	{  25,  9183 },  /*  2.5dB */
	{  30,  8799 },  /*  3.0dB */
	{  35,  8422 },  /*  3.5dB */
	{  40,  8062 },  /*  4.0dB */
	{  45,  7707 },  /*  4.5dB */
	{  50,  7353 },  /*  5.0dB */
	{  55,  7025 },  /*  5.5dB */
	{  60,  6684 },  /*  6.0dB */
	{  65,  6331 },  /*  6.5dB */
	{  70,  6036 },  /*  7.0dB */
	{  75,  5727 },  /*  7.5dB */
	{  80,  5437 },  /*  8.0dB */
	{  85,  5164 },  /*  8.5dB */
	{  90,  4902 },  /*  9.0dB */
	{  95,  4653 },  /*  9.5dB */
	{ 100,  4408 },  /* 10.0dB */
	{ 105,  4187 },  /* 10.5dB */
	{ 110,  3961 },  /* 11.0dB */
	{ 115,  3751 },  /* 11.5dB */
	{ 120,  3558 },  /* 12.0dB */
	{ 125,  3368 },  /* 12.5dB */
	{ 130,  3191 },  /* 13.0dB */
	{ 135,  3017 },  /* 13.5dB */
	{ 140,  2862 },  /* 14.0dB */
	{ 145,  2710 },  /* 14.5dB */
	{ 150,  2565 },  /* 15.0dB */
	{ 160,  2300 },  /* 16.0dB */
	{ 170,  2058 },  /* 17.0dB */
	{ 180,  1849 },  /* 18.0dB */
	{ 190,  1663 },  /* 19.0dB */
	{ 200,  1495 },  /* 20.0dB */
	{ 210,  1349 },  /* 21.0dB */
	{ 220,  1222 },  /* 22.0dB */
	{ 230,  1110 },  /* 23.0dB */
	{ 240,  1011 },  /* 24.0dB */
	{ 250,   925 },  /* 25.0dB */
	{ 260,   853 },  /* 26.0dB */
	{ 270,   789 },  /* 27.0dB */
	{ 280,   734 },  /* 28.0dB */
	{ 290,   690 },  /* 29.0dB */
	{ 300,   650 },  /* 30.0dB */
	{ 310,   619 },  /* 31.0dB */
	{ 320,   593 },  /* 32.0dB */
	{ 330,   571 },  /* 33.0dB */
	{ 400,   498 },  /* 40.0dB */
	{ 450,   484 },  /* 45.0dB */
	{ 500,   481 }	 /* 50.0dB */
};

/* RF level C/N lookup table */
static const struct stv090x_tab stv090x_rf_tab[] =
{
	{  -5, 0xcaa1 },  /*  -5dBm */
	{ -10, 0xc229 },  /* -10dBm */
	{ -15, 0xbb08 },  /* -15dBm */
	{ -20, 0xb4bc },  /* -20dBm */
	{ -25, 0xad5a },  /* -25dBm */
	{ -30, 0xa298 },  /* -30dBm */
	{ -35, 0x98a8 },  /* -35dBm */
	{ -40, 0x8389 },  /* -40dBm */
	{ -45, 0x59be },  /* -45dBm */
	{ -50, 0x3a14 },  /* -50dBm */
	{ -55, 0x2d11 },  /* -55dBm */
	{ -60, 0x210d },  /* -60dBm */
	{ -65, 0xa14f },  /* -65dBm */
	{ -70, 0x07aa }	  /* -70dBm */
};

static struct stv090x_reg stv0900_initval[] =
{
	{ STV090x_OUTCFG,         0x00 },
	{ STV090x_MODECFG,        0xff },
	{ STV090x_AGCRF1CFG,      0x11 },
	{ STV090x_AGCRF2CFG,      0x13 },
	{ STV090x_TSGENERAL1X,    0x14 },
	{ STV090x_TSTTNR2,        0x21 },
	{ STV090x_TSTTNR4,        0x21 },
	{ STV090x_P2_DISTXCTL,    0x22 },
	{ STV090x_P2_F22TX,       0xc0 },
	{ STV090x_P2_F22RX,       0xc0 },
	{ STV090x_P2_DISRXCTL,    0x00 },
#if 0
	{ STV090x_P2_TNRSTEPS,    0x87 },
	{ STV090x_P2_TNRGAIN,     0x09 },
#endif
	{ STV090x_P2_DMDCFGMD,    0xF9 },
	{ STV090x_P2_DEMOD,       0x08 },
	{ STV090x_P2_DMDCFG3,     0xc4 },
	{ STV090x_P2_CARFREQ,     0xed },
#if 0
	{ STV090x_P2_TNRCFG2,     0x02 },
	{ STV090x_P2_TNRCFG3,     0x02 },
#endif
	{ STV090x_P2_LDT,         0xd0 },
	{ STV090x_P2_LDT2,        0xb8 },
	{ STV090x_P2_TMGCFG,      0xd2 },
	{ STV090x_P2_TMGTHRISE,   0x20 },
	{ STV090x_P1_TMGCFG,      0xd2 },

	{ STV090x_P2_TMGTHFALL,   0x00 },
	{ STV090x_P2_FECSPY,      0x88 },
	{ STV090x_P2_FSPYDATA,    0x3a },
	{ STV090x_P2_FBERCPT4,    0x00 },
	{ STV090x_P2_FSPYBER,     0x10 },
	{ STV090x_P2_ERRCTRL1,    0x35 },
	{ STV090x_P2_ERRCTRL2,    0x12 },  // 0xc1
	{ STV090x_P2_CFRICFG,     0xf8 },
	{ STV090x_P2_NOSCFG,      0x1c },
	{ STV090x_P2_DMDTOM,      0x20 },
	{ STV090x_P2_CORRELMANT,  0x70 },
	{ STV090x_P2_CORRELABS,   0x88 },
	{ STV090x_P2_AGC2O,       0x5b },
	{ STV090x_P2_AGC2REF,     0x38 },
	{ STV090x_P2_CARCFG,      0xe4 },
	{ STV090x_P2_ACLC,        0x1A },
	{ STV090x_P2_BCLC,        0x09 },
	{ STV090x_P2_CARHDR,      0x08 },
	{ STV090x_P2_KREFTMG,     0xc1 },
	{ STV090x_P2_SFRUPRATIO,  0xf0 },
	{ STV090x_P2_SFRLOWRATIO, 0x70 },
	{ STV090x_P2_SFRSTEP,     0x58 },
	{ STV090x_P2_TMGCFG2,     0x01 },
	{ STV090x_P2_CAR2CFG,     0x26 },
	{ STV090x_P2_BCLC2S2Q,    0x86 },
	{ STV090x_P2_BCLC2S28,    0x86 },
	{ STV090x_P2_SMAPCOEF7,   0x77 },
	{ STV090x_P2_SMAPCOEF6,   0x85 },
	{ STV090x_P2_SMAPCOEF5,   0x77 },
	{ STV090x_P2_TSCFGL,      0x20 },
	{ STV090x_P2_DMDCFG2,     0x3b },
	{ STV090x_P2_MODCODLST0,  0xff },
	{ STV090x_P2_MODCODLST1,  0xff },
	{ STV090x_P2_MODCODLST2,  0xff },
	{ STV090x_P2_MODCODLST3,  0xff },
	{ STV090x_P2_MODCODLST4,  0xff },
	{ STV090x_P2_MODCODLST5,  0xff },
	{ STV090x_P2_MODCODLST6,  0xff },
	{ STV090x_P2_MODCODLST7,  0xcc },
	{ STV090x_P2_MODCODLST8,  0xcc },
	{ STV090x_P2_MODCODLST9,  0xcc },
	{ STV090x_P2_MODCODLSTA,  0xcc },
	{ STV090x_P2_MODCODLSTB,  0xcc },
	{ STV090x_P2_MODCODLSTC,  0xcc },
	{ STV090x_P2_MODCODLSTD,  0xcc },
	{ STV090x_P2_MODCODLSTE,  0xcc },
	{ STV090x_P2_MODCODLSTF,  0xcf },
	{ STV090x_P1_DISTXCTL,    0x22 },
	{ STV090x_P1_F22TX,       0xc0 },
	{ STV090x_P1_F22RX,       0xc0 },
	{ STV090x_P1_DISRXCTL,    0x00 },
#if 0
	{ STV090x_P1_TNRSTEPS,    0x87 },
	{ STV090x_P1_TNRGAIN,     0x09 },
#endif
	{ STV090x_P1_DMDCFGMD,    0xf9 },
	{ STV090x_P1_DEMOD,       0x08 },
	{ STV090x_P1_DMDCFG3,     0xc4 },
	{ STV090x_P1_DMDTOM,      0x20 },
	{ STV090x_P1_CARFREQ,     0xed },
#if 0
	{ STV090x_P1_TNRCFG2,     0x82 },
	{ STV090x_P1_TNRCFG3,     0x02 },
#endif
	{ STV090x_P1_LDT,         0xd0 },
	{ STV090x_P1_LDT2,        0xb8 },
	{ STV090x_P1_TMGCFG,      0xd2 },
	{ STV090x_P1_TMGTHRISE,   0x20 },
	{ STV090x_P1_TMGTHFALL,   0x00 },
	{ STV090x_P1_SFRUPRATIO,  0xf0 },
	{ STV090x_P1_SFRLOWRATIO, 0x70 },
	{ STV090x_P1_TSCFGL,      0x20 },
	{ STV090x_P1_FECSPY,      0x88 },
	{ STV090x_P1_FSPYDATA,    0x3a },
	{ STV090x_P1_FBERCPT4,    0x00 },
	{ STV090x_P1_FSPYBER,     0x10 },
	{ STV090x_P1_ERRCTRL1,    0x35 },
	{ STV090x_P1_ERRCTRL2,    0x12 },  // 0xc1
	{ STV090x_P1_CFRICFG,     0xf8 },
	{ STV090x_P1_NOSCFG,      0x1c },
	{ STV090x_P1_CORRELMANT,  0x70 },
	{ STV090x_P1_CORRELABS,   0x88 },
	{ STV090x_P1_AGC2O,       0x5b },
	{ STV090x_P1_AGC2REF,     0x38 },
	{ STV090x_P1_CARCFG,      0xe4 },
	{ STV090x_P1_ACLC,        0x1A },
	{ STV090x_P1_BCLC,        0x09 },
	{ STV090x_P1_CARHDR,      0x08 },
	{ STV090x_P1_KREFTMG,     0xc1 },
	{ STV090x_P1_SFRSTEP,     0x58 },
	{ STV090x_P1_TMGCFG2,     0x01 },
	{ STV090x_P1_CAR2CFG,     0x26 },
	{ STV090x_P1_BCLC2S2Q,    0x86 },
	{ STV090x_P1_BCLC2S28,    0x86 },
	{ STV090x_P1_SMAPCOEF7,   0x77 },
	{ STV090x_P1_SMAPCOEF6,   0x85 },
	{ STV090x_P1_SMAPCOEF5,   0x77 },
	{ STV090x_P1_DMDCFG2,     0x3b },
	{ STV090x_P1_MODCODLST0,  0xff },
	{ STV090x_P1_MODCODLST1,  0xff },
	{ STV090x_P1_MODCODLST2,  0xff },
	{ STV090x_P1_MODCODLST3,  0xff },
	{ STV090x_P1_MODCODLST4,  0xff },
	{ STV090x_P1_MODCODLST5,  0xff },
	{ STV090x_P1_MODCODLST6,  0xff },
	{ STV090x_P1_MODCODLST7,  0xcc },
	{ STV090x_P1_MODCODLST8,  0xcc },
	{ STV090x_P1_MODCODLST9,  0xcc },
	{ STV090x_P1_MODCODLSTA,  0xcc },
	{ STV090x_P1_MODCODLSTB,  0xcc },
	{ STV090x_P1_MODCODLSTC,  0xcc },
	{ STV090x_P1_MODCODLSTD,  0xcc },
	{ STV090x_P1_MODCODLSTE,  0xcc },
	{ STV090x_P1_MODCODLSTF,  0xcf },
	{ STV090x_GENCFG,         0x1d },
	{ STV090x_NBITER_NF4,     0x37 },
	{ STV090x_NBITER_NF5,     0x29 },
	{ STV090x_NBITER_NF6,     0x37 },
	{ STV090x_NBITER_NF7,     0x33 },
	{ STV090x_NBITER_NF8,     0x31 },
	{ STV090x_NBITER_NF9,     0x2f },
	{ STV090x_NBITER_NF10,    0x39 },
	{ STV090x_NBITER_NF11,    0x3a },
	{ STV090x_NBITER_NF12,    0x29 },
	{ STV090x_NBITER_NF13,    0x37 },
	{ STV090x_NBITER_NF14,    0x33 },
	{ STV090x_NBITER_NF15,    0x2f },
	{ STV090x_NBITER_NF16,    0x39 },
	{ STV090x_NBITER_NF17,    0x3a },
	{ STV090x_NBITERNOERR,    0x04 },
	{ STV090x_GAINLLR_NF4,    0x0C },
	{ STV090x_GAINLLR_NF5,    0x0F },
	{ STV090x_GAINLLR_NF6,    0x11 },
	{ STV090x_GAINLLR_NF7,    0x14 },
	{ STV090x_GAINLLR_NF8,    0x17 },
	{ STV090x_GAINLLR_NF9,    0x19 },
	{ STV090x_GAINLLR_NF10,   0x20 },
	{ STV090x_GAINLLR_NF11,   0x21 },
	{ STV090x_GAINLLR_NF12,   0x0D },
	{ STV090x_GAINLLR_NF13,   0x0F },
	{ STV090x_GAINLLR_NF14,   0x13 },
	{ STV090x_GAINLLR_NF15,   0x1A },
	{ STV090x_GAINLLR_NF16,   0x1F },
	{ STV090x_GAINLLR_NF17,   0x21 },
	{ STV090x_RCCFGH,         0x20 },
	{ STV090x_P1_FECM,        0x01 },  /* disable DSS modes */
	{ STV090x_P2_FECM,        0x01 },  /* disable DSS modes */
	{ STV090x_P1_PRVIT,       0x2F },  /* disable PR 6/7 */
	{ STV090x_P2_PRVIT,       0x2F },  /* disable PR 6/7 */
};

#define STV090x_P1_TNRSTEPS 0xf4e7
#define STV090x_P2_TNRSTEPS 0xf2e7
#define STV090x_P1_TNRGAIN  0xf4e8
#define STV090x_P2_TNRGAIN  0xf2e8
#define STV090x_P1_TNRCFG3  0xf4ee
#define STV090x_P2_TNRCFG3  0xf2ee

#if 0
// freebox oryginalne dane init z adb2850
static struct stv090x_reg stx7111_initval_[] =
{
	{ 0xF416, 0x5C },
	{ 0xF216, 0x5C },
	{ 0xF4E0, 0x6C },
	{ 0xF2E0, 0x6F },
	{ 0xF12A, 0x25 },
	{ 0xF12B, 0x25 },
	{ 0xF1C2, 0x05 },
	{ 0xF1B6, 0x02 },
	{ 0xF11C, 0x00 },
	{ 0xF152, 0x11 },
	{ 0xF156, 0x13 },
	{ 0xF630, 0x00 },
	{ 0xF190, 0xA2 },
	{ 0xF199, 0xC0 },
	{ 0xF19A, 0xC0 },
	{ 0xF191, 0x00 },
	{ 0xF2E7, 0x87 },
	{ 0xF2E8, 0x09 },
	{ 0xF214, 0xF9 },
	{ 0xF210, 0x08 },
	{ 0xF21E, 0x48 },
	{ 0xF23D, 0x88 },
	{ 0xF2E1, 0x82 },
	{ 0xF2EE, 0x02 },
	{ 0xF23F, 0xD0 },
	{ 0xF240, 0xB0 },
	{ 0xF250, 0xD3 },
	{ 0xF253, 0x20 },
	{ 0xF254, 0x00 },
	{ 0xF2C6, 0xA9 },
	{ 0xF3A0, 0x88 },
	{ 0xF3A2, 0x3A },
	{ 0xF3A8, 0x00 },
	{ 0xF3B2, 0x10 },
	{ 0xF372, 0x40 },
	{ 0xF398, 0x35 },
	{ 0xF39C, 0xC1 },
	{ 0xF241, 0xF8 },
	{ 0xF201, 0x0C },
	{ 0xF217, 0x20 },
	{ 0xF22C, 0x5B },
	{ 0xF22D, 0x38 },
	{ 0xF238, 0xE4 },
	{ 0xF239, 0x1A },
	{ 0xF23A, 0x09 },
	{ 0xF23B, 0x00 },
	{ 0xF23C, 0xC0 },
	{ 0xF23E, 0x20 },
	{ 0xF258, 0x87 },
	{ 0xF255, 0xF0 },
	{ 0xF256, 0x70 },
	{ 0xF259, 0x58 },
	{ 0xF290, 0x26 },
	{ 0xF29C, 0xA5 },
	{ 0xF29D, 0xA5 },
	{ 0xF300, 0xFE },
	{ 0xF301, 0x00 },
	{ 0xF302, 0xFF },
	{ 0xF215, 0x3B },
	{ 0xF2B0, 0xF0 },
	{ 0xF2B1, 0x00 },
	{ 0xF2B2, 0x00 },
	{ 0xF2B3, 0x00 },
	{ 0xF2B4, 0x00 },
	{ 0xF2B5, 0x00 },
	{ 0xF2B6, 0x00 },
	{ 0xF2B7, 0x00 },
	{ 0xF2B8, 0x00 },
	{ 0xF2B9, 0x00 },
	{ 0xF2BA, 0x00 },
	{ 0xF2BB, 0x00 },
	{ 0xF2BC, 0x00 },
	{ 0xF2BD, 0x00 },
	{ 0xF2BE, 0x00 },
	{ 0xF2BF, 0x0F },
	{ 0xF1A0, 0xA2 },
	{ 0xF1A9, 0xC0 },
	{ 0xF1AA, 0xC0 },
	{ 0xF1A1, 0x00 },
	{ 0xF4E7, 0x87 },
	{ 0xF4E8, 0x09 },
	{ 0xF414, 0xF9 },
	{ 0xF410, 0x08 },
	{ 0xF41E, 0x48 },
	{ 0xF417, 0x20 },
	{ 0xF43D, 0x88 },
	{ 0xF4E1, 0x02 },
	{ 0xF4EE, 0x02 },
	{ 0xF43F, 0xD0 },
	{ 0xF440, 0xB0 },
	{ 0xF450, 0xD3 },
	{ 0xF453, 0x20 },
	{ 0xF454, 0x00 },
	{ 0xF455, 0xF0 },
	{ 0xF456, 0x70 },
	{ 0xF5A0, 0x88 },
	{ 0xF5A2, 0x3A },
	{ 0xF5A8, 0x00 },
	{ 0xF5B2, 0x10 },
	{ 0xF572, 0x40 },
	{ 0xF598, 0x35 },
	{ 0xF59C, 0xC1 },
	{ 0xF441, 0xF8 },
	{ 0xF401, 0x0C },
	{ 0xF42C, 0x5B },
	{ 0xF42D, 0x38 },
	{ 0xF438, 0xE4 },
	{ 0xF439, 0x1A },
	{ 0xF43A, 0x09 },
	{ 0xF43B, 0x00 },
	{ 0xF43C, 0xC0 },
	{ 0xF43E, 0x20 },
	{ 0xF453, 0x20 },
	{ 0xF458, 0x87 },
	{ 0xF454, 0x00 },
	{ 0xF459, 0x58 },
	{ 0xF490, 0x26 },
	{ 0xF49C, 0xA5 },
	{ 0xF49D, 0xA5 },
	{ 0xF4C6, 0xA9 },
	{ 0xF500, 0xFE },
	{ 0xF501, 0x00 },
	{ 0xF502, 0xFF },
	{ 0xF415, 0x3B },
	{ 0xF4B0, 0xF0 },
	{ 0xF4B1, 0x00 },
	{ 0xF4B2, 0x00 },
	{ 0xF4B3, 0x00 },
	{ 0xF4B4, 0x00 },
	{ 0xF4B5, 0x00 },
	{ 0xF4B6, 0x00 },
	{ 0xF4B7, 0x00 },
	{ 0xF4B8, 0x00 },
	{ 0xF4B9, 0x00 },
	{ 0xF4BA, 0x00 },
	{ 0xF4BB, 0x00 },
	{ 0xF4BC, 0x00 },
	{ 0xF4BD, 0x00 },
	{ 0xF4BE, 0x00 },
	{ 0xF4BF, 0x0F },
	{ 0xFA3F, 0x04 },
	{ 0xFA43, 0x0F },
	{ 0xFA44, 0x13 },
	{ 0xFA45, 0x15 },
	{ 0xFA46, 0x1A },
	{ 0xFA47, 0x1F },
	{ 0xFA48, 0x20 },
	{ 0xFA49, 0x26 },
	{ 0xFA4A, 0x28 },
	{ 0xFA4B, 0x0D },
	{ 0xFA4C, 0x0F },
	{ 0xFA4D, 0x13 },
	{ 0xFA4E, 0x19 },
	{ 0xFA4F, 0x20 },
	{ 0xFA50, 0x20 },
	{ 0xFA03, 0x38 },
	{ 0xFA04, 0x2C },
	{ 0xFA05, 0x3B },
	{ 0xFA06, 0x38 },
	{ 0xFA07, 0x36 },
	{ 0xFA08, 0x35 },
	{ 0xFA09, 0x41 },
	{ 0xFA0A, 0x41 },
	{ 0xFA0B, 0x1D },
	{ 0xFA0C, 0x27 },
	{ 0xFA0D, 0x25 },
	{ 0xFA0E, 0x23 },
	{ 0xFA0F, 0x2B },
	{ 0xFA10, 0x2B },
	{ 0xF2C0, 0xAC },
	{ 0xF2C1, 0x2C },
	{ 0xF2C2, 0xAC },
	{ 0xF2C3, 0x00 },
	{ 0xF4C0, 0xAC },
	{ 0xF4C1, 0x2C },
	{ 0xF4C2, 0xAC },
	{ 0xF4C3, 0x00 },
	{ 0xF374, 0x30 },
	{ 0xF574, 0x30 },
	{ 0xFF11, 0x80 },
	{ 0xFF11, 0x00 },
	{ 0xF1E0, 0x26 },
	{ 0xF1E2, 0x24 },
};
#endif
	
static struct stv090x_reg stx7111_initval[] =
{
	/* demod2 */
	{ STV090x_OUTCFG,         0x00 },
	{ STV090x_AGCRF1CFG,      0x11 },
	{ STV090x_AGCRF2CFG,      0x13 },
	{ STV090x_TSGENERAL,      0x00 },
	{ STV090x_P2_DISTXCTL,    0x22 },
	{ STV090x_P2_F22TX,       0xc0 },
	{ STV090x_P2_F22RX,       0xc0 },
	{ STV090x_P2_DISRXCTL,    0x00 },
	{ STV090x_P2_TNRSTEPS,    0x87 },
	{ STV090x_P2_TNRGAIN,     0x09 },
	{ STV090x_P2_DMDCFGMD,    0xf9 },
	{ STV090x_P2_DEMOD,       0x0E },
	{ STV090x_P2_DMDCFG3,     0x48 },
	{ STV090x_P2_CARFREQ,     0x88 },
	{ STV090x_P2_TNRCFG2,     0x02 },
	{ STV090x_P2_TNRCFG3,     0x02 },

	{ STV090x_P2_LDT,         0xd0 },
	{ STV090x_P2_LDT2,        0xb0 },
	{ STV090x_P2_TMGCFG,      0xd3 },
	{ STV090x_P2_TMGTHRISE,   0x20 },
	{ STV090x_P2_TMGTHFALL,   0x00 },

	{ STV090x_P2_FECSPY,      0x88 },
	{ STV090x_P2_FSPYDATA,    0x3a },
	{ STV090x_P2_FBERCPT4,    0x00 },
	{ STV090x_P2_FSPYBER,     0x10 },
	{ STV090x_P2_TSCFGH,      0x40 },
	{ STV090x_P2_ERRCTRL1,    0x35 },
	{ STV090x_P2_ERRCTRL2,    0xc1 }, // 0xc1
	{ STV090x_P2_CFRICFG,     0xf8 },
	{ STV090x_P2_NOSCFG,      0x0c },
	{ STV090x_P2_DMDTOM,      0x20 },
	{ STV090x_P2_AGC2O,       0x5b },
	{ STV090x_P2_AGC2REF,     0x38 },
	{ STV090x_P2_CARCFG,      0xe4 },
	{ STV090x_P2_ACLC,        0x1A },
	{ STV090x_P2_BCLC,        0x09 },
	{ 0xf43b,                 0x00 },
	{ 0xf43c,                 0xc0 },
	{ STV090x_P2_CARHDR,      0x20 },
#if 0
	{ STV090x_P2_TMGTHRISE,   0x20 },
#endif
	{ STV090x_P2_KREFTMG,     0x87 },
	{ STV090x_P2_SFRUPRATIO,  0xf0 },
	{ STV090x_P2_SFRLOWRATIO, 0x70 },

#if 0
	{ STV090x_P2_TMGTHFALL,   0x00 },
#endif

	{ STV090x_P2_SFRSTEP,     0x58 },
	{ STV090x_P2_CAR2CFG,     0x26 },
#if 0
	{ STV090x_P2_BCLC2S2Q,    0x86 },
	{ STV090x_P2_BCLC2S28,    0x86 },
#else
	{ STV090x_P2_BCLC2S2Q,    0xa5 },
	{ STV090x_P2_BCLC2S28,    0xa5 },
#endif
	{ STV090x_P2_DMDRESCFG,   0xa9 },
	{ STV090x_P2_SMAPCOEF7,   0xfe },
	{ STV090x_P2_SMAPCOEF6,   0x00 },
	{ STV090x_P2_SMAPCOEF5,   0xff },
	{ STV090x_P2_DMDCFG2,     0x3b },
	{ STV090x_P2_MODCODLST0,  0xff },
	{ STV090x_P2_MODCODLST1,  0xff },
	{ STV090x_P2_MODCODLST2,  0xff },
	{ STV090x_P2_MODCODLST3,  0xff },
	{ STV090x_P2_MODCODLST4,  0xff },
	{ STV090x_P2_MODCODLST5,  0xff },
	{ STV090x_P2_MODCODLST6,  0xff },
	{ STV090x_P2_MODCODLST7,  0xcc },
	{ STV090x_P2_MODCODLST8,  0xcc },
	{ STV090x_P2_MODCODLST9,  0xcc },
	{ STV090x_P2_MODCODLSTA,  0xcc },
	{ STV090x_P2_MODCODLSTB,  0xcc },
	{ STV090x_P2_MODCODLSTC,  0xcc },
	{ STV090x_P2_MODCODLSTD,  0xcc },
	{ STV090x_P2_MODCODLSTE,  0xff },
	{ STV090x_P2_MODCODLSTF,  0xff },
	/* demod1 */
	{ STV090x_P1_DISTXCTL,    0x22 },
	{ STV090x_P1_F22TX,       0xc0 },
	{ STV090x_P1_F22RX,       0xc0 },
	{ STV090x_P1_DISRXCTL,    0x00 },

	{ STV090x_P1_TNRSTEPS,    0x87 },
	{ STV090x_P1_TNRGAIN,     0x09 },
	{ STV090x_P1_DMDCFGMD,    0xf9 },
	{ STV090x_P1_DEMOD,       0x0E },
	{ STV090x_P1_DMDCFG3,     0x48 },
	{ STV090x_P1_DMDTOM,      0x20 },
	{ STV090x_P1_CARFREQ,     0x88 },
	{ STV090x_P1_TNRCFG2,     0x02 },
	{ STV090x_P1_TNRCFG3,     0x02 },

	{ STV090x_P1_LDT,         0xd0 },
	{ STV090x_P1_LDT2,        0xb0 },
	{ STV090x_P1_TMGCFG,      0xd3 },
	{ STV090x_P1_TMGTHRISE,   0x20 },
	{ STV090x_P1_TMGTHFALL,   0x00 },
	{ STV090x_P2_SFRUPRATIO,  0xf0 },
	{ STV090x_P2_SFRLOWRATIO, 0x70 },

	{ STV090x_P1_FECSPY,      0x88 },
	{ STV090x_P1_FSPYDATA,    0x3a },
	{ STV090x_P1_FBERCPT4,    0x00 },
	{ STV090x_P1_FSPYBER,     0x10 },

#if 0
	{ STV090x_P1_TSCFGH,      0x40 },
#else
	{ STV090x_P1_TSCFGH,      0x90 },
#endif
	{ STV090x_P1_ERRCTRL1,    0x35 },
	{ STV090x_P1_ERRCTRL2,    0xc1 },  // 0xc1
	{ STV090x_P1_CFRICFG,     0xf8 },
	{ STV090x_P1_NOSCFG,      0x0c },
	{ STV090x_P1_DMDTOM,      0x20 },
	{ STV090x_P1_AGC2O,       0x5b },
	{ STV090x_P1_AGC2REF,     0x38 },
	{ STV090x_P1_CARCFG,      0xe4 },
	{ STV090x_P1_ACLC,        0x1A },
	{ STV090x_P1_BCLC,        0x09 },
	{ 0xf43b,                 0x00 },
	{ 0xf43c,                 0xc0 },
	{ STV090x_P1_CARHDR,      0x20 },
	{ STV090x_P1_TMGTHRISE,   0x20 },
	{ STV090x_P1_KREFTMG,     0x87 },
	{ STV090x_P1_TMGTHFALL,   0x00 },
	{ STV090x_P1_SFRSTEP,     0x58 },
	{ STV090x_P1_CAR2CFG,     0x26 },
	{ STV090x_P1_BCLC2S2Q,    0x86 },
	{ STV090x_P1_BCLC2S28,    0x86 },

	{ STV090x_P1_DMDRESCFG,   0xa9 },
	{ STV090x_P1_SMAPCOEF7,   0xfe },
	{ STV090x_P1_SMAPCOEF6,   0x00 },
	{ STV090x_P1_SMAPCOEF5,   0xff },
	{ STV090x_P1_DMDCFG2,     0x3b },
	{ STV090x_P1_MODCODLST0,  0xff },
	{ STV090x_P1_MODCODLST1,  0xff },
	{ STV090x_P1_MODCODLST2,  0xff },
	{ STV090x_P1_MODCODLST3,  0xff },
	{ STV090x_P1_MODCODLST4,  0xff },
	{ STV090x_P1_MODCODLST5,  0xff },
	{ STV090x_P1_MODCODLST6,  0xff },
	{ STV090x_P1_MODCODLST7,  0xcc },
	{ STV090x_P1_MODCODLST8,  0xcc },
	{ STV090x_P1_MODCODLST9,  0xcc },
	{ STV090x_P1_MODCODLSTA,  0xcc },
	{ STV090x_P1_MODCODLSTB,  0xcc },
	{ STV090x_P1_MODCODLSTC,  0xcc },
	{ STV090x_P1_MODCODLSTD,  0xcc },
	{ STV090x_P1_MODCODLSTE,  0xff },
	{ STV090x_P1_MODCODLSTF,  0xff },

	{ STV090x_NBITERNOERR,    0x04 },
	{ STV090x_GAINLLR_NF4,    0x0f },
	{ STV090x_GAINLLR_NF5,    0x13 },
	{ STV090x_GAINLLR_NF6,    0x15 },
	{ STV090x_GAINLLR_NF7,    0x1a },
	{ STV090x_GAINLLR_NF8,    0x1F },
	{ STV090x_GAINLLR_NF9,    0x20 },
	{ STV090x_GAINLLR_NF10,   0x26 },
	{ STV090x_GAINLLR_NF11,   0x28 },
	{ STV090x_GAINLLR_NF12,   0x0D },
	{ STV090x_GAINLLR_NF13,   0x0F },
	{ STV090x_GAINLLR_NF14,   0x13 },
	{ STV090x_GAINLLR_NF15,   0x19 },
	{ STV090x_GAINLLR_NF16,   0x20 },
	{ STV090x_GAINLLR_NF17,   0x20 },
	{ STV090x_NBITER_NF4,     0x38 },
	{ STV090x_NBITER_NF5,     0x2C },
	{ STV090x_NBITER_NF6,     0x3b },
	{ STV090x_NBITER_NF7,     0x38 },
	{ STV090x_NBITER_NF8,     0x36 },
	{ STV090x_NBITER_NF9,     0x35 },

	{ STV090x_NBITER_NF10,    0x41 },
	{ STV090x_NBITER_NF11,    0x41 },
	{ STV090x_NBITER_NF12,    0x1d },
	{ STV090x_NBITER_NF13,    0x27 },
	{ STV090x_NBITER_NF14,    0x25 },
	{ STV090x_NBITER_NF15,    0x23 },
	{ STV090x_NBITER_NF16,    0x2b },
	{ STV090x_NBITER_NF17,    0x2b },

	{ STV090x_P2_GAUSSR0,     0xac },
	{ STV090x_P2_CCIR0,       0x2c },
	{ STV090x_P2_CCIQUANT,    0xac },
	{ 0xf2c3,                 0x00 },

	{ STV090x_P1_GAUSSR0,     0xac },
	{ STV090x_P1_CCIR0,       0x2c },
	{ STV090x_P1_CCIQUANT,    0xac },
	{ 0xf4c3,                 0x00 },

	{ STV090x_P2_TSCFGL,      0x30 },
	{ STV090x_P1_TSCFGL,      0x30 },
};

static struct stv090x_reg stv0903_initval[] =
{
	{ STV090x_OUTCFG,         0x00 },
	{ STV090x_AGCRF1CFG,      0x11 },  // for tuner STV6110
	{ STV090x_STOPCLK1,       0x48 },
	{ STV090x_STOPCLK2,       0x14 },
	{ STV090x_TSTTNR1,        0x27 },
	{ STV090x_TSTTNR2,        0x21 },
	{ STV090x_P1_DISTXCTL,    0x22 },
	{ STV090x_P1_F22TX,       0xc0 },
	{ STV090x_P1_F22RX,       0xc0 },
	{ STV090x_P1_DISRXCTL,    0x00 },
	/* __TDT__*/
	{ STV090x_P1_TNRSTEPS,    0x87 },
	{ STV090x_P1_TNRGAIN,     0x09 },

	/* TDT
	{ STV090x_P1_DMDCFGMD,    0xF9 }, */
	{ STV090x_P1_DMDCFGMD,    0xc9 },
	{ STV090x_P1_DEMOD,       0x08 },
	{ STV090x_P1_DMDCFG3,     0xc4 },
	{ STV090x_P1_CARFREQ,     0xed },
	{ STV090x_P1_TNRCFG2,     0x82 },
	/* __TDT__ */
	{ STV090x_P1_TNRCFG3,     0x03 },

	{ STV090x_P1_LDT,         0xd0 },
	{ STV090x_P1_LDT2,        0xb8 },
	{ STV090x_P1_TMGCFG,      0xd2 },
	{ STV090x_P1_TMGTHRISE,   0x20 },
	{ STV090x_P1_TMGTHFALL,   0x00 },
	{ STV090x_P1_SFRUPRATIO,  0xf0 },
	{ STV090x_P1_SFRLOWRATIO, 0x70 },
	{ STV090x_P1_TSCFGL,      0x20 },
	{ STV090x_P1_FECSPY,      0x88 },
	{ STV090x_P1_FSPYDATA,    0x3a },
	{ STV090x_P1_FBERCPT4,    0x00 },
	{ STV090x_P1_FSPYBER,     0x10 },
	{ STV090x_P1_ERRCTRL1,    0x35 },
	{ STV090x_P1_ERRCTRL2,    0x12 },  //0xc1
	{ STV090x_P1_CFRICFG,     0xf8 },
	{ STV090x_P1_NOSCFG,      0x1c },
	{ STV090x_P1_DMDTOM,      0x20 },
	{ STV090x_P1_CORRELMANT,  0x70 },
	{ STV090x_P1_CORRELABS,   0x88 },
	{ STV090x_P1_AGC2O,       0x5b },
	{ STV090x_P1_AGC2REF,     0x38 },
	{ STV090x_P1_CARCFG,      0xe4 },
	{ STV090x_P1_ACLC,        0x1A },
	{ STV090x_P1_BCLC,        0x09 },
	{ STV090x_P1_CARHDR,      0x08 },
	{ STV090x_P1_KREFTMG,     0xc1 },
	{ STV090x_P1_SFRSTEP,     0x58 },
	{ STV090x_P1_TMGCFG2,     0x01 },
	{ STV090x_P1_CAR2CFG,     0x26 },
	{ STV090x_P1_BCLC2S2Q,    0x86 },
	{ STV090x_P1_BCLC2S28,    0x86 },
	{ STV090x_P1_SMAPCOEF7,   0x77 },
	{ STV090x_P1_SMAPCOEF6,   0x85 },
	{ STV090x_P1_SMAPCOEF5,   0x77 },
	{ STV090x_P1_DMDCFG2,     0x3b },
	{ STV090x_P1_MODCODLST0,  0xff },
	{ STV090x_P1_MODCODLST1,  0xff },
	{ STV090x_P1_MODCODLST2,  0xff },
	{ STV090x_P1_MODCODLST3,  0xff },
	{ STV090x_P1_MODCODLST4,  0xff },
	{ STV090x_P1_MODCODLST5,  0xff },
	{ STV090x_P1_MODCODLST6,  0xff },
	{ STV090x_P1_MODCODLST7,  0xcc },
	{ STV090x_P1_MODCODLST8,  0xcc },
	{ STV090x_P1_MODCODLST9,  0xcc },
	{ STV090x_P1_MODCODLSTA,  0xcc },
	{ STV090x_P1_MODCODLSTB,  0xcc },
	{ STV090x_P1_MODCODLSTC,  0xcc },
	{ STV090x_P1_MODCODLSTD,  0xcc },
	{ STV090x_P1_MODCODLSTE,  0xcc },
	{ STV090x_P1_MODCODLSTF,  0xcf },
	{ STV090x_GENCFG,         0x1c },
	{ STV090x_NBITER_NF4,     0x37 },
	{ STV090x_NBITER_NF5,     0x29 },
	{ STV090x_NBITER_NF6,     0x37 },
	{ STV090x_NBITER_NF7,     0x33 },
	{ STV090x_NBITER_NF8,     0x31 },
	{ STV090x_NBITER_NF9,     0x2f },
	{ STV090x_NBITER_NF10,    0x39 },
	{ STV090x_NBITER_NF11,    0x3a },
	{ STV090x_NBITER_NF12,    0x29 },
	{ STV090x_NBITER_NF13,    0x37 },
	{ STV090x_NBITER_NF14,    0x33 },
	{ STV090x_NBITER_NF15,    0x2f },
	{ STV090x_NBITER_NF16,    0x39 },
	{ STV090x_NBITER_NF17,    0x3a },
	{ STV090x_NBITERNOERR,    0x04 },
	{ STV090x_GAINLLR_NF4,    0x0C },
	{ STV090x_GAINLLR_NF5,    0x0F },
	{ STV090x_GAINLLR_NF6,    0x11 },
	{ STV090x_GAINLLR_NF7,    0x14 },
	{ STV090x_GAINLLR_NF8,    0x17 },
	{ STV090x_GAINLLR_NF9,    0x19 },
	{ STV090x_GAINLLR_NF10,   0x20 },
	{ STV090x_GAINLLR_NF11,   0x21 },
	{ STV090x_GAINLLR_NF12,   0x0D },
	{ STV090x_GAINLLR_NF13,   0x0F },
	{ STV090x_GAINLLR_NF14,   0x13 },
	{ STV090x_GAINLLR_NF15,   0x1A },
	{ STV090x_GAINLLR_NF16,   0x1F },
	{ STV090x_GAINLLR_NF17,   0x21 },
	{ STV090x_RCCFGH,         0x20 },
	{ STV090x_P1_FECM,        0x01 },  /* disable the DSS mode */
	{ STV090x_P1_PRVIT,       0x2f }   /* disable puncture rate 6/7 */
};

static struct stv090x_reg stv0900_cut20_val[] =
{
	{ STV090x_P2_DMDCFG3,     0xe8 },
	{ STV090x_P2_DMDCFG4,     0x10 },
	{ STV090x_P2_CARFREQ,     0x38 },
	{ STV090x_P2_CARHDR,      0x20 },
	{ STV090x_P2_KREFTMG,     0x5a },
	{ STV090x_P2_SMAPCOEF7,   0x06 },
	{ STV090x_P2_SMAPCOEF6,   0x00 },
	{ STV090x_P2_SMAPCOEF5,   0x04 },
	{ STV090x_P2_NOSCFG,      0x0c },
	{ STV090x_P1_DMDCFG3,     0xe8 },
	{ STV090x_P1_DMDCFG4,     0x10 },
	{ STV090x_P1_CARFREQ,     0x38 },
	{ STV090x_P1_CARHDR,      0x20 },
	{ STV090x_P1_KREFTMG,     0x5a },
	{ STV090x_P1_SMAPCOEF7,   0x06 },
	{ STV090x_P1_SMAPCOEF6,   0x00 },
	{ STV090x_P1_SMAPCOEF5,   0x04 },
	{ STV090x_P1_NOSCFG,      0x0c },
	{ STV090x_GAINLLR_NF4,    0x21 },
	{ STV090x_GAINLLR_NF5,    0x21 },
	{ STV090x_GAINLLR_NF6,    0x20 },
	{ STV090x_GAINLLR_NF7,    0x1F },
	{ STV090x_GAINLLR_NF8,    0x1E },
	{ STV090x_GAINLLR_NF9,    0x1E },
	{ STV090x_GAINLLR_NF10,   0x1D },
	{ STV090x_GAINLLR_NF11,   0x1B },
	{ STV090x_GAINLLR_NF12,   0x20 },
	{ STV090x_GAINLLR_NF13,   0x20 },
	{ STV090x_GAINLLR_NF14,   0x20 },
	{ STV090x_GAINLLR_NF15,   0x20 },
	{ STV090x_GAINLLR_NF16,   0x20 },
	{ STV090x_GAINLLR_NF17,   0x21 },
};

static struct stv090x_reg stv0903_cut20_val[] =
{
	{ STV090x_P1_DMDCFG3,     0xe8 },
	{ STV090x_P1_DMDCFG4,     0x10 },
	{ STV090x_P1_CARFREQ,     0x38 },
	{ STV090x_P1_CARHDR,      0x20 },
	{ STV090x_P1_KREFTMG,     0x5a },
	{ STV090x_P1_SMAPCOEF7,   0x06 },
	{ STV090x_P1_SMAPCOEF6,   0x00 },
	{ STV090x_P1_SMAPCOEF5,   0x04 },
	{ STV090x_P1_NOSCFG,      0x0c },
	{ STV090x_GAINLLR_NF4,    0x21 },
	{ STV090x_GAINLLR_NF5,    0x21 },
	{ STV090x_GAINLLR_NF6,    0x20 },
	{ STV090x_GAINLLR_NF7,    0x1F },
	{ STV090x_GAINLLR_NF8,    0x1E },
	{ STV090x_GAINLLR_NF9,    0x1E },
	{ STV090x_GAINLLR_NF10,   0x1D },
	{ STV090x_GAINLLR_NF11,   0x1B },
	{ STV090x_GAINLLR_NF12,   0x20 },
	{ STV090x_GAINLLR_NF13,   0x20 },
	{ STV090x_GAINLLR_NF14,   0x20 },
	{ STV090x_GAINLLR_NF15,   0x20 },
	{ STV090x_GAINLLR_NF16,   0x20 },
	{ STV090x_GAINLLR_NF17,   0x21 }
};

/* Cut 2.0 Long Frame Tracking CR loop */
static struct stv090x_long_frame_crloop stv090x_s2_crl_cut20[] =
{
	/* MODCOD           2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_12,  0x1f, 0x3f,  0x1e, 0x3f,  0x3d,  0x1f,   0x3d,  0x3e,   0x3d,  0x1e },
	{ STV090x_QPSK_35,  0x2f, 0x3f,  0x2e, 0x2f,  0x3d,  0x0f,   0x0e,  0x2e,   0x3d,  0x0e },
	{ STV090x_QPSK_23,  0x2f, 0x3f,  0x2e, 0x2f,  0x0e,  0x0f,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_QPSK_34,  0x3f, 0x3f,  0x3e, 0x1f,  0x0e,  0x3e,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_QPSK_45,  0x3f, 0x3f,  0x3e, 0x1f,  0x0e,  0x3e,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_QPSK_56,  0x3f, 0x3f,  0x3e, 0x1f,  0x0e,  0x3e,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_QPSK_89,  0x3f, 0x3f,  0x3e, 0x1f,  0x1e,  0x3e,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_QPSK_910, 0x3f, 0x3f,  0x3e, 0x1f,  0x1e,  0x3e,   0x0e,  0x1e,   0x3d,  0x3d },
	{ STV090x_8PSK_35,  0x3c, 0x3e,  0x1c, 0x2e,  0x0c,  0x1e,   0x2b,  0x2d,   0x1b,  0x1d },
	{ STV090x_8PSK_23,  0x1d, 0x3e,  0x3c, 0x2e,  0x2c,  0x1e,   0x0c,  0x2d,   0x2b,  0x1d },
	{ STV090x_8PSK_34,  0x0e, 0x3e,  0x3d, 0x2e,  0x0d,  0x1e,   0x2c,  0x2d,   0x0c,  0x1d },
	{ STV090x_8PSK_56,  0x2e, 0x3e,  0x1e, 0x2e,  0x2d,  0x1e,   0x3c,  0x2d,   0x2c,  0x1d },
	{ STV090x_8PSK_89,  0x3e, 0x3e,  0x1e, 0x2e,  0x3d,  0x1e,   0x0d,  0x2d,   0x3c,  0x1d },
	{ STV090x_8PSK_910, 0x3e, 0x3e,  0x1e, 0x2e,  0x3d,  0x1e,   0x1d,  0x2d,   0x0d,  0x1d }
};

/* Cut 2.0 Long Frame Tracking CR loop */
static struct stv090x_long_frame_crloop stx7111_s2_crl_cut20[] =
{
	/* MODCOD          2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{STV090x_QPSK_12,  0x1c, 0x3c,  0x1b, 0x3c,  0x3a,  0x1c,   0x0b,  0x1c,   0x0b,  0x3b},
	{STV090x_QPSK_35,  0x2c, 0x3c,  0x2b, 0x2c,  0x3a,  0x0c,   0x0b,  0x1c,   0x0b,  0x3b},
	{STV090x_QPSK_23,  0x2c, 0x3c,  0x2b, 0x2c,  0x0b,  0x0c,   0x0b,  0x0c,   0x0b,  0x2b},
	{STV090x_QPSK_34,  0x3c, 0x3c,  0x3b, 0x1c,  0x0b,  0x3b,   0x0b,  0x0c,   0x0b,  0x2b},
	{STV090x_QPSK_45,  0x3c, 0x3c,  0x3b, 0x1c,  0x0b,  0x3b,   0x0b,  0x0c,   0x0b,  0x2b},
	{STV090x_QPSK_56,  0x3c, 0x3c,  0x3b, 0x1c,  0x0b,  0x3b,   0x0b,  0x0c,   0x0b,  0x2b},
	{STV090x_QPSK_89,  0x3c, 0x3c,  0x3b, 0x1c,  0x1b,  0x3b,   0x1b,  0x0c,   0x0b,  0x2b},
	{STV090x_QPSK_910, 0x0d, 0x3c,  0x3b, 0x1c,  0x1b,  0x3b,   0x1b,  0x0c,   0x0b,  0x2b},
	{STV090x_8PSK_35,  0x39, 0x19,  0x19, 0x09,  0x09,  0x38,   0x09,  0x09,   0x38,  0x29},
	{STV090x_8PSK_23,  0x1a, 0x19,  0x39, 0x19,  0x29,  0x38,   0x29,  0x29,   0x19,  0x38},
	{STV090x_8PSK_34,  0x0b, 0x28,  0x3a, 0x19,  0x0a,  0x28,   0x1a,  0x28,   0x0a,  0x28},
	{STV090x_8PSK_56,  0x2b, 0x0c,  0x1b, 0x2b,  0x2a,  0x1b,   0x3a,  0x2b,   0x2a,  0x1b},
	{STV090x_8PSK_89,  0x0c, 0x0c,  0x1b, 0x2b,  0x3a,  0x1b,   0x0b,  0x3b,   0x3a,  0x1b},
	{STV090x_8PSK_910, 0x0c, 0x0c,  0x1b, 0x2b,  0x3a,  0x1b,   0x0b,  0x3b,   0x3a,  0x1b}
};

/* Cut 3.0 Long Frame Tracking CR loop */
static	struct stv090x_long_frame_crloop stv090x_s2_crl_cut30[] =
{
	/* MODCOD           2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_12,  0x3c, 0x2c,  0x0c, 0x2c,  0x1b,  0x2c,   0x1b,  0x1c,   0x0b,  0x3b },
	{ STV090x_QPSK_35,  0x0d, 0x0d,  0x0c, 0x0d,  0x1b,  0x3c,   0x1b,  0x1c,   0x0b,  0x3b },
	{ STV090x_QPSK_23,  0x1d, 0x0d,  0x0c, 0x1d,  0x2b,  0x3c,   0x1b,  0x1c,   0x0b,  0x3b },
	{ STV090x_QPSK_34,  0x1d, 0x1d,  0x0c, 0x1d,  0x2b,  0x3c,   0x1b,  0x1c,   0x0b,  0x3b },
	{ STV090x_QPSK_45,  0x2d, 0x1d,  0x1c, 0x1d,  0x2b,  0x3c,   0x2b,  0x0c,   0x1b,  0x3b },
	{ STV090x_QPSK_56,  0x2d, 0x1d,  0x1c, 0x1d,  0x2b,  0x3c,   0x2b,  0x0c,   0x1b,  0x3b },
	{ STV090x_QPSK_89,  0x3d, 0x2d,  0x1c, 0x1d,  0x3b,  0x3c,   0x2b,  0x0c,   0x1b,  0x3b },
	{ STV090x_QPSK_910, 0x3d, 0x2d,  0x1c, 0x1d,  0x3b,  0x3c,   0x2b,  0x0c,   0x1b,  0x3b },
	{ STV090x_8PSK_35,  0x39, 0x29,  0x39, 0x19,  0x19,  0x19,   0x19,  0x19,   0x09,  0x19 },
	{ STV090x_8PSK_23,  0x2a, 0x39,  0x1a, 0x0a,  0x39,  0x0a,   0x29,  0x39,   0x29,  0x0a },
	{ STV090x_8PSK_34,  0x2b, 0x3a,  0x1b, 0x1b,  0x3a,  0x1b,   0x1a,  0x0b,   0x1a,  0x3a },
	{ STV090x_8PSK_56,  0x0c, 0x1b,  0x3b, 0x3b,  0x1b,  0x3b,   0x3a,  0x3b,   0x3a,  0x1b },
	{ STV090x_8PSK_89,  0x0d, 0x3c,  0x2c, 0x2c,  0x2b,  0x0c,   0x0b,  0x3b,   0x0b,  0x1b },
	{ STV090x_8PSK_910, 0x0d, 0x0d,  0x2c, 0x3c,  0x3b,  0x1c,   0x0b,  0x3b,   0x0b,  0x1b }
};

/* Cut 2.0 Long Frame Tracking CR Loop */
static struct stv090x_long_frame_crloop stv090x_s2_apsk_crl_cut20[] =
{
	/* MODCOD             2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_16APSK_23,  0x0c, 0x0c,  0x0c, 0x0c,  0x1d,  0x0c,   0x3c,  0x0c,   0x2c,  0x0c },
	{ STV090x_16APSK_34,  0x0c, 0x0c,  0x0c, 0x0c,  0x0e,  0x0c,   0x2d,  0x0c,   0x1d,  0x0c },
	{ STV090x_16APSK_45,  0x0c, 0x0c,  0x0c, 0x0c,  0x1e,  0x0c,   0x3d,  0x0c,   0x2d,  0x0c },
	{ STV090x_16APSK_56,  0x0c, 0x0c,  0x0c, 0x0c,  0x1e,  0x0c,   0x3d,  0x0c,   0x2d,  0x0c },
	{ STV090x_16APSK_89,  0x0c, 0x0c,  0x0c, 0x0c,  0x2e,  0x0c,   0x0e,  0x0c,   0x3d,  0x0c },
	{ STV090x_16APSK_910, 0x0c, 0x0c,  0x0c, 0x0c,  0x2e,  0x0c,   0x0e,  0x0c,   0x3d,  0x0c },
	{ STV090x_32APSK_34,  0x0c, 0x0c,  0x0c, 0x0c,  0x0c,  0x0c,   0x0c,  0x0c,   0x0c,  0x0c },
	{ STV090x_32APSK_45,  0x0c, 0x0c,  0x0c, 0x0c,  0x0c,  0x0c,   0x0c,  0x0c,   0x0c,  0x0c },
	{ STV090x_32APSK_56,  0x0c, 0x0c,  0x0c, 0x0c,  0x0c,  0x0c,   0x0c,  0x0c,   0x0c,  0x0c },
	{ STV090x_32APSK_89,  0x0c, 0x0c,  0x0c, 0x0c,  0x0c,  0x0c,   0x0c,  0x0c,   0x0c,  0x0c },
	{ STV090x_32APSK_910, 0x0c, 0x0c,  0x0c, 0x0c,  0x0c,  0x0c,   0x0c,  0x0c,   0x0c,  0x0c }
};

/* Cut 3.0 Long Frame Tracking CR Loop */
static struct stv090x_long_frame_crloop	stv090x_s2_apsk_crl_cut30[] =
{
	/* MODCOD             2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_16APSK_23,  0x0a, 0x0a,  0x0a, 0x0a,  0x1a,  0x0a,   0x3a,  0x0a,   0x2a,  0x0a },
	{ STV090x_16APSK_34,  0x0a, 0x0a,  0x0a, 0x0a,  0x0b,  0x0a,   0x3b,  0x0a,   0x1b,  0x0a },
	{ STV090x_16APSK_45,  0x0a, 0x0a,  0x0a, 0x0a,  0x1b,  0x0a,   0x3b,  0x0a,   0x2b,  0x0a },
	{ STV090x_16APSK_56,  0x0a, 0x0a,  0x0a, 0x0a,  0x1b,  0x0a,   0x3b,  0x0a,   0x2b,  0x0a },
	{ STV090x_16APSK_89,  0x0a, 0x0a,  0x0a, 0x0a,  0x2b,  0x0a,   0x0c,  0x0a,   0x3b,  0x0a },
	{ STV090x_16APSK_910, 0x0a, 0x0a,  0x0a, 0x0a,  0x2b,  0x0a,   0x0c,  0x0a,   0x3b,  0x0a },
	{ STV090x_32APSK_34,  0x0a, 0x0a,  0x0a, 0x0a,  0x0a,  0x0a,   0x0a,  0x0a,   0x0a,  0x0a },
	{ STV090x_32APSK_45,  0x0a, 0x0a,  0x0a, 0x0a,  0x0a,  0x0a,   0x0a,  0x0a,   0x0a,  0x0a },
	{ STV090x_32APSK_56,  0x0a, 0x0a,  0x0a, 0x0a,  0x0a,  0x0a,   0x0a,  0x0a,   0x0a,  0x0a },
	{ STV090x_32APSK_89,  0x0a, 0x0a,  0x0a, 0x0a,  0x0a,  0x0a,   0x0a,  0x0a,   0x0a,  0x0a },
	{ STV090x_32APSK_910, 0x0a, 0x0a,  0x0a, 0x0a,  0x0a,  0x0a,   0x0a,  0x0a,   0x0a,  0x0a }
};

static struct stv090x_long_frame_crloop stv090x_s2_lowqpsk_crl_cut20[] =
{
	/* MODCOD           2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_14,  0x0f, 0x3f,  0x0e, 0x3f,  0x2d,  0x2f,   0x2d,  0x1f,   0x3d,  0x3e },
	{ STV090x_QPSK_13,  0x0f, 0x3f,  0x0e, 0x3f,  0x2d,  0x2f,   0x3d,  0x0f,   0x3d,  0x2e },
	{ STV090x_QPSK_25,  0x1f, 0x3f,  0x1e, 0x3f,  0x3d,  0x1f,   0x3d,  0x3e,   0x3d,  0x2e }
};

static struct stv090x_long_frame_crloop	stv090x_s2_lowqpsk_crl_cut30[] =
{
	/* MODCOD           2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon 20MPoff 30MPon 30MPoff */
	{ STV090x_QPSK_14,  0x0c, 0x3c,  0x0b, 0x3c,  0x2a,  0x2c,   0x2a,  0x1c,   0x3a,  0x3b },
	{ STV090x_QPSK_13,  0x0c, 0x3c,  0x0b, 0x3c,  0x2a,  0x2c,   0x3a,  0x0c,   0x3a,  0x2b },
	{ STV090x_QPSK_25,  0x1c, 0x3c,  0x1b, 0x3c,  0x3a,  0x1c,   0x3a,  0x3b,   0x3a,  0x2b }
};

/* Cut 2.0 Short Frame Tracking CR Loop */
static struct stv090x_short_frame_crloop stv090x_s2_short_crl_cut20[] =
{
	/* MODCOD         2M    5M    10M   20M   30M */
	{ STV090x_QPSK,   0x2f, 0x2e, 0x0e, 0x0e, 0x3d },
	{ STV090x_8PSK,   0x3e, 0x0e, 0x2d, 0x0d, 0x3c },
	{ STV090x_16APSK, 0x1e, 0x1e, 0x1e, 0x3d, 0x2d },
	{ STV090x_32APSK, 0x1e, 0x1e, 0x1e, 0x3d, 0x2d }
};

/* Cut 3.0 Short Frame Tracking CR Loop */
static struct stv090x_short_frame_crloop stv090x_s2_short_crl_cut30[] =
{
	/* MODCOD         2M    5M    10M   20M   30M */
	{ STV090x_QPSK,   0x2C, 0x2B, 0x0B, 0x0B, 0x3A },
	{ STV090x_8PSK,   0x3B, 0x0B, 0x2A, 0x0A, 0x39 },
	{ STV090x_16APSK, 0x1B, 0x1B, 0x1B, 0x3A, 0x2A },
	{ STV090x_32APSK, 0x1B, 0x1B, 0x1B, 0x3A, 0x2A }
};


/****************************************************
 *
 * DiSEqC PWM by plfreebox@gmail.com
 *
 */
unsigned long pwm_registers;

#define PWM0_VAL         (pwm_registers + 0x00)
#define PWM1_VAL         (pwm_registers + 0x04)
#define PWM0_CPT_VAL     (pwm_registers + 0x10)
#define PWM1_CPT_VAL     (pwm_registers + 0x14)
#define PWM0_CMP_VAL     (pwm_registers + 0x20)
#define PWM1_CMP_VAL     (pwm_registers + 0x24)
#define PWM0_CPT_EDGE    (pwm_registers + 0x30)
#define PWM1_CPT_EDGE    (pwm_registers + 0x34)
#define PWM0_CMP_OUT_VAL (pwm_registers + 0x40)
#define PWM1_CMP_OUT_VAL (pwm_registers + 0x44)
#define PWM_CTRL         (pwm_registers + 0x50)
#define PWM_INT_EN       (pwm_registers + 0x54)
#define PWM_INT_STA      (pwm_registers + 0x58)
#define PWM_INT_ACK      (pwm_registers + 0x5C)
#define PWM_CNT PWM      (pwm_registers + 0x60)
#define PWM_CPT_CMP_CNT  (pwm_registers + 0x64)
#define PWM_CTRL_PWM_EN  (1 << 9)

static volatile unsigned char pwm_diseqc_buf1[200];
static volatile unsigned char pwm_diseqc_buf1_len = 0;
static volatile unsigned char pwm_diseqc_buf1_pos = 0;

static irqreturn_t pwm_diseqc_irq(int irq, void *dev_id)
{
	writel(0x001, PWM_INT_ACK);

	if (pwm_diseqc_buf1_len == 0)
	{
		stpio_set_pin (mp8125_extm, 0);
		writel(0x000, PWM_INT_EN);
		return IRQ_HANDLED;
	}
	if (pwm_diseqc_buf1_len > 0)
	{
		if (pwm_diseqc_buf1[pwm_diseqc_buf1_pos] == 1)
		{
			stpio_set_pin (mp8125_extm, 1);
		}
		else
		{
			stpio_set_pin (mp8125_extm, 0);
		}
		pwm_diseqc_buf1_pos++;
		pwm_diseqc_buf1_len--;
	}
	return IRQ_HANDLED;
}

static int pwm_wait_diseqc1_idle(int timeout)
{
	unsigned long start = jiffies;
	int status;

	while (1)
	{
		if (pwm_diseqc_buf1_len == 0)
		{
			break;
		}
		if (jiffies - start > timeout)
		{
			dprintk(1, "%s: Timeout on DiSEqC idle!\n", __func__);
			return -ETIMEDOUT;
		}
		msleep(10);
	}
	return 0;
}

static int pwm_send_diseqc1_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t burst)
{
	int i, j;

	if (pwm_wait_diseqc1_idle(100) < 0)
	{
		return -ETIMEDOUT;
	}
	pwm_diseqc_buf1_pos = 1;
	pwm_diseqc_buf1_len = 0;

	// adding an empty overflow for the counter overflow time
	pwm_diseqc_buf1_len++;
	pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;

	switch (burst)
	{
		case SEC_MINI_A:
		{
			dprintk(10, "%s Tone = A\n", __func__);
			for (i = 0; i < 8; i++)
			{
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			}
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			break;
		}
		case SEC_MINI_B:
		{
			dprintk(10, "%s Tone = B\n", __func__);
			for (i = 0; i < 8; i++)
			{
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
			}
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			break;
		}
	}
	pwm_diseqc_buf1_len++;
	pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
	writel(0x001, PWM_INT_EN);

	if (pwm_wait_diseqc1_idle(100) < 0)
	{
		return -ETIMEDOUT;
	}
	return 0;
}

static int pwm_diseqc1_send_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *m)
{
	int i, j;

	dprintk(50, "%s > (msg_len = %d)\n", __func__, m->msg_len);

	if (pwm_wait_diseqc1_idle(100) < 0)
	{
		return -ETIMEDOUT;
	}
	pwm_diseqc_buf1_pos = 1;
	pwm_diseqc_buf1_len = 0;

	// adding an empty overflow for the counter overflow time
	pwm_diseqc_buf1_len++;
	pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;

	for (j = 0; j < m->msg_len; j++)
	{
		unsigned char byte = m->msg[j];
		unsigned char parity = 0;

		for (i = 0; i < 8; i++)
		{
			if ((byte & 128) == 128)
			{
				//DiSEqC 1
				parity++;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
			}
			else
			{
				//DiSEqC 0
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
				pwm_diseqc_buf1_len++;
				pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
			}
			byte = byte << 1;
		}
		if ((parity & 1) == 1)
		{
			// DiSEqC 0
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
		}
		else
		{
			// DiSEqC 1
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 1;
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
			pwm_diseqc_buf1_len++;
			pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
		}
	}
	pwm_diseqc_buf1_len++;
	pwm_diseqc_buf1[pwm_diseqc_buf1_len] = 0;
	writel(0x001, PWM_INT_EN);

	if (pwm_wait_diseqc1_idle(100) < 0)
	{
		return -ETIMEDOUT;
	}
	return 0;
}

int pwm_diseqc_init(void)
{
	u32 reg;

	dprintk(10, "PWM Diseqc Init\n");
	pwm_registers = (unsigned long)ioremap(0xFD010000, 0x100);

	if (request_irq(evt2irq(0x11c0), pwm_diseqc_irq, IRQF_DISABLED , "timer_pwm", NULL)) 
	{
		dprintk(1, "FAIL : request irq pwm\n");
		goto err;
	}

	// 500us = 2000hz
	// 27000000 / 2000 / 256 = 52
	// 100us = 10000hz
	// 27000000 / 10000 = 2700 / 256 = 10
	// reg = 52;  // 500us
	// reg = 10;  // 100us
	// 30000000 / 2000 /256 = 58
	reg = 58;
	reg = (reg & 0x0f) + ((reg & 0xf0) << (11 - 4)) + PWM_CTRL_PWM_EN;
	//dprintk(10, "reg div = 0x%x\n", reg);

	writel(reg, PWM_CTRL);  // generating an interrupt every 500us
	writel(0x000, PWM_INT_EN);

	pwm_diseqc_buf1_pos = 1;
	pwm_diseqc_buf1_len = 0;

	dprintk(50, "PWM DiSeqC Init: OK\n");
	return 0;

err:
	iounmap((void *)pwm_registers);
	return -ENODEV;
}
// DiSEqC end

static inline s32 comp2(s32 __x, s32 __width)
{
	if (__width == 32)
	{
		return __x;
	}
	else
	{
		return (__x >= (1 << (__width - 1))) ? (__x - (1 << __width)) : __x;
	}
}

static int stv090x_read_reg(struct stv090x_state *state, unsigned int reg)
{
	const struct stv090x_config *config = state->config;
	int ret;

	u8 b0[] = { reg >> 8, reg & 0xff };
	u8 buf;
	struct i2c_msg msg[] =
	{
		{ .addr	= config->address, .flags = 0,        .buf = b0,   .len = 2 },
		{ .addr	= config->address, .flags = I2C_M_RD, .buf = &buf, .len = 1 }
	};
	dprintk(150, "%s config->address = 0x%x \n", __func__, config->address);

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2)
	{
		if (ret != -ERESTARTSYS)
		{
			dprintk(1, "Read error, Reg = [0x%02x], Status = %d\n", reg, ret);
		}
		return ret < 0 ? ret : -EREMOTEIO;
	}
	dprintk(200, "%s Reg = [0x%02x], result = 0x%02x\n", __func__, reg, buf);
	return (unsigned int) buf;
}

static int stv090x_write_regs(struct stv090x_state *state, unsigned int reg, u8 *data, u32 count)
{
	const struct stv090x_config *config = state->config;
	int ret;
	int i;
	u8 buf[2 + count];
	struct i2c_msg i2c_msg = { .addr = config->address, .flags = 0, .buf = buf, .len = 2 + count };

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	memcpy(&buf[2], data, count);

	dprintk(150, "%s [reg = 0x%04x, count = %d]:", __func__, reg, count);
	dprintk(150, "%s 0x%02x, 0x%02x", __func__, (reg >> 8) & 0xff, reg & 0xff);
	for (i = 0; i < count; i++)
	{
		dprintk(150, " 0x%02x", data[i]);
	}
	dprintk(150, "\n");

	ret = i2c_transfer(state->i2c, &i2c_msg, 1);

	if (ret != 1)
	{
		if (ret != -ERESTARTSYS)
		{
			dprintk(1, "Reg = [0x%04x], Data = [0x%02x ...], Count = %u, Status = %d\n", reg, data[0], count, ret);
		}
		return ret < 0 ? ret : -EREMOTEIO;
	}
	return 0;
}

static int stv090x_write_reg(struct stv090x_state *state, unsigned int reg, u8 data)
{
	return stv090x_write_regs(state, reg, &data, 1);
}

static int stv090x_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	dprintk(00, "state->i2c->nr %d\n", state->i2c->nr);

	reg = STV090x_READ_DEMOD(state, I2CRPT);
	if (enable)
	{
		dprintk(250, "Enable Gate\n");
		STV090x_SETFIELD_Px(reg, I2CT_ON_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, I2CRPT, reg) < 0)
		{
			goto err;
		}
	}
	else
	{
		if (state->device != STX7111)
		{
			dprintk(250, "Disable Gate\n");
			STV090x_SETFIELD_Px(reg, I2CT_ON_FIELD, 0);
			if ((STV090x_WRITE_DEMOD(state, I2CRPT, reg)) < 0)
			{
				goto err;
			}
		}
	}
	return 0;

err:
	dprintk(1, "stv090x_i2c_gate_ctrl: I/O error\n");
	return -1;
}

static void stv090x_get_lock_tmg(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);

	switch (state->algo)
	{
		case STV090x_BLIND_SEARCH:
		{
			dprintk(50, "Blind Search\n");
			if (state->srate <= 1500000)
			{
				/*10Msps< SR <=15Msps*/
				state->DemodTimeout = 1500;
				state->FecTimeout = 400;
			}
			else if (state->srate <= 5000000)
			{
				/*10Msps< SR <=15Msps*/
				state->DemodTimeout = 1000;
				state->FecTimeout = 300;
			}
			else
			{
				/*SR >20Msps*/
				state->DemodTimeout = 700;
				state->FecTimeout = 100;
			}
			break;
		}
		case STV090x_COLD_SEARCH:
		case STV090x_WARM_SEARCH:
		default:
		{
			dprintk(50, "Normal Search\n");
			if (state->srate <= 1000000)
			{
				/*SR <=1Msps*/
				state->DemodTimeout = 4500;
				state->FecTimeout = 1700;
			}
			else if (state->srate <= 2000000)
			{
				/*1Msps < SR <= 2Msps */
				state->DemodTimeout = 2500;
				state->FecTimeout = 1100;
			}
			else if (state->srate <= 5000000)
			{
				/*2Msps < SR <= 5Msps */
				state->DemodTimeout = 1000;
				state->FecTimeout = 550;
			}
			else if (state->srate <= 10000000)
			{
				/*5Msps < SR <= 10Msps */
				state->DemodTimeout = 700;
				state->FecTimeout = 250;
			}
			else if (state->srate <= 20000000)
			{
				/*10Msps < SR <= 20Msps */
				state->DemodTimeout = 400;
				state->FecTimeout = 130;
			}
			else
			{
				/*SR >20Msps*/
				state->DemodTimeout = 300;
				state->FecTimeout = 100;
			}
			break;
		}
	}
	if (state->algo == STV090x_WARM_SEARCH)
	{
		state->DemodTimeout /= 2;
	}
	dprintk(100, "%s <\n", __func__);
}

static int stv090x_set_srate(struct stv090x_state *state, u32 srate)
{
	u32 sym;

	dprintk(100, "%s >\n", __func__);

	if (srate > 60000000)
	{
		sym  = (srate << 4); /* SR * 2^16 / master_clk */
		sym /= (state->mclk >> 12);
	}
	else if (srate > 6000000)
	{
		sym  = (srate << 6);
		sym /= (state->mclk >> 10);
	}
	else
	{
		sym  = (srate << 9);
		sym /= (state->mclk >> 7);
	}
	dprintk(100, "0x%x\n", (sym >> 8) & 0xff);
	dprintk(100, "0x%x\n", (sym & 0xff));

	if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0x7f) < 0) /* MSB */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRINIT0, (sym & 0xff)) < 0) /* LSB */
	{
		goto err;
	}
	dprintk(010, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_set_max_srate(struct stv090x_state *state, u32 clk, u32 srate)
{
	u32 sym;

	dprintk(100, "%s >\n", __func__);

	srate = 105 * (srate / 100);
	if (srate > 60000000)
	{
		sym  = (srate << 4); /* SR * 2^16 / master_clk */
		sym /= (state->mclk >> 12);
	}
	else if (srate > 6000000)
	{
		sym  = (srate << 6);
		sym /= (state->mclk >> 10);
	}
	else
	{
		sym  = (srate << 9);
		sym /= (state->mclk >> 7);
	}

	if (sym < 0x7fff)
	{
		if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0) /* MSB */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0) /* LSB */
		{
			goto err;
		}
	}
	else
	{
		if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x7f) < 0) /* MSB */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRUP0, 0xff) < 0) /* LSB */
		{
			goto err;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_set_min_srate(struct stv090x_state *state, u32 clk, u32 srate)
{
	u32 sym;

	dprintk(100, "%s >\n", __func__);

	srate = 95 * (srate / 100);
	if (srate > 60000000)
	{
		sym  = (srate << 4); /* SR * 2^16 / master_clk */
		sym /= (state->mclk >> 12);
	}
	else if (srate > 6000000)
	{
		sym  = (srate << 6);
		sym /= (state->mclk >> 10);
	}
	else
	{
		sym  = (srate << 9);
		sym /= (state->mclk >> 7);
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, ((sym >> 8) & 0x7f)) < 0) /* MSB */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, (sym & 0xff)) < 0) /* LSB */
	{
		goto err;
	}
	dprintk(010, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static u32 stv090x_car_width(u32 srate, enum stv090x_rolloff rolloff)
{
	u32 ro;

	dprintk(100, "%s >\n", __func__);

	switch (rolloff)
	{
		case STV090x_RO_20:
		{
			ro = 20;
			break;
		}
		case STV090x_RO_25:
		{
			ro = 25;
			break;
		}
		case STV090x_RO_35:
		default:
		{
			ro = 35;
			break;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return srate + (srate * ro) / 100;
}

static int stv090x_set_vit_thacq(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);
	if (STV090x_WRITE_DEMOD(state, VTH12, 0x96) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH23, 0x64) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH34, 0x36) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH56, 0x23) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH67, 0x1e) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH78, 0x19) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_set_vit_thtracq(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);
	if (STV090x_WRITE_DEMOD(state, VTH12, 0xd0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH23, 0x7d) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH34, 0x53) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH56, 0x2f) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH67, 0x24) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, VTH78, 0x1f) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_set_viterbi(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);
	switch (state->search_mode)
	{
		case STV090x_SEARCH_AUTO:
		{
			if (state->device != STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, FECM, 0x00) < 0) /* DVB-S and DVB-S2 */
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, PRVIT, 0x2f) < 0) /* all puncture rate */
				{
					goto err;
				}
			}
			else
			{
				if (STV090x_WRITE_DEMOD(state, FECM, 0x10) < 0) /* DVB-S and DVB-S2 */
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, PRVIT, 0x3f) < 0) /* all puncture rate */
				{
					goto err;
				}
			}
			break;
		}
		case STV090x_SEARCH_DVBS1:
		{
			if (STV090x_WRITE_DEMOD(state, FECM, 0x00) < 0) /* disable DSS */
			{
				goto err;
			}
			switch (state->fec)
			{
				case STV090x_PR12:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x01) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR23:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x02) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR34:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x04) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR56:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x08) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR78:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x20) < 0)
					{
						goto err;
					}
					break;
				}
				default:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x2f) < 0) /* all */
					{
						goto err;
					}
					break;
				}
			}
			break;
		}
		case STV090x_SEARCH_DSS:
		{
			if (STV090x_WRITE_DEMOD(state, FECM, 0x80) < 0)
			{
				goto err;
			}
			switch (state->fec)
			{
				case STV090x_PR12:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x01) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR23:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x02) < 0)
					{
						goto err;
					}
					break;
				}
				case STV090x_PR67:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x10) < 0)
					{
						goto err;
					}
					break;
				}
				default:
				{
					if (STV090x_WRITE_DEMOD(state, PRVIT, 0x13) < 0) /* 1/2, 2/3, 6/7 */
					{
						goto err;
					}
					break;
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_stop_modcod(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);
	if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xff) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_activate_modcod(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);

	if (state->device == STX7111)
	{
		if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xff) < 0)
		{
			goto err;
		}
	}
	else
	{
		if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xfc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xcc) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xcf) < 0)
		{
			goto err;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_activate_modcod_single(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);

	if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xf0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0x0f) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_vitclk_ctl(struct stv090x_state *state, int enable)
{
	u32 reg;

	dprintk(100, "%s >\n", __func__);

	switch (state->demod)
	{
		case STV090x_DEMODULATOR_0:
		{
			mutex_lock(&demod_lock);
			reg = stv090x_read_reg(state, STV090x_STOPCLK2);
			STV090x_SETFIELD(reg, STOP_CLKVIT1_FIELD, enable);
			if (stv090x_write_reg(state, STV090x_STOPCLK2, reg) < 0)
			{
				goto err;
			}
			mutex_unlock(&demod_lock);
			break;
		}
		case STV090x_DEMODULATOR_1:
		{
			mutex_lock(&demod_lock);
			reg = stv090x_read_reg(state, STV090x_STOPCLK2);
			STV090x_SETFIELD(reg, STOP_CLKVIT2_FIELD, enable);
			if (stv090x_write_reg(state, STV090x_STOPCLK2, reg) < 0)
			{
				goto err;
			}
			mutex_unlock(&demod_lock);
			break;
		}
		default:
		{
			dprintk(1, "%s Wrong demodulator!\n", __func__);
			break;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	mutex_unlock(&demod_lock);
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_dvbs_track_crl(struct stv090x_state *state)
{
	dprintk(100, "%s >\n", __func__);

	if (state->dev_ver >= 0x30)
	{
		/* Set ACLC BCLC optimised value vs SR */
		if (state->srate >= 15000000)
		{
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x2b) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x1a) < 0)
			{
				goto err;
			}
		}
		else if ((state->srate >= 7000000)
		&&       (15000000 > state->srate))
		{
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x0c) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x1b) < 0)
			{
				goto err;
			}
		}
		else if (state->srate < 7000000)
		{
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x2c) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x1c) < 0)
			{
				goto err;
			}
		}
	}
	else
	{
		/* Cut 2.0 */
		if (state->device == STX7111)
		{
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x2b) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x1a) < 0)
			{
				goto err;
			}
		}
		else
		{
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x1a) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x09) < 0)
			{
				goto err;
			}
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_delivery_search(struct stv090x_state *state)
{
	u32 reg;

	dprintk(1-0, "%s >\n", __func__);

	switch (state->search_mode)
	{
		case STV090x_SEARCH_DVBS1:
		case STV090x_SEARCH_DSS:
		{
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			/* Activate Viterbi decoder in legacy search,
			 * do not use FRESVIT1, might impact VITERBI2
			 */
			if (stv090x_vitclk_ctl(state, 0) < 0)
			{
				goto err;
			}
			if (stv090x_dvbs_track_crl(state) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x22) < 0)  /* disable DVB-S2 */
			{
				goto err;
			}
			if (stv090x_set_vit_thacq(state) < 0)
			{
				goto err;
			}
			if (stv090x_set_viterbi(state) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_SEARCH_DVBS2:
		{
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			if (stv090x_vitclk_ctl(state, 1) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, ACLC, 0x1a) < 0) /* stop DVB-S CR loop */
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0x09) < 0)
			{
				goto err;
			}
			if ((state->dev_ver <= 0x20) || (state->device == STX7111))
			{
				/* enable S2 carrier loop */
				if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x26) < 0)
				{
					goto err;
				}
			}
			else
			{
				/* > Cut 3: Stop carrier 3 */
				if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x66) < 0)
				{
					goto err;
				}
			}
			if (state->device == STX7111)
			{
				if (stv090x_write_reg(state, STV090x_GENCFG, 0x16) < 0)
				{
					goto err;
				}
				reg = stv090x_read_reg(state, STV090x_TSTRES0);
				STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x01); /* ldpc reset */
				if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				{
					goto err;
				}
				STV090x_SETFIELD(reg, FRESFEC_FIELD, 0);
				if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				{
					goto err;
				}
				if (stv090x_activate_modcod(state) < 0)
				{
					goto err;
				}
			}
			else
			{
				if (state->demod_mode != STV090x_SINGLE)
				{
					/* Cut 2: enable link during search */
					if (stv090x_activate_modcod(state) < 0)
					{
						goto err;
					}
				}
				else
				{
					/* Single demodulator
					 * Authorize SHORT and LONG frames,
					 * QPSK, 8PSK, 16APSK and 32APSK
					 */
					if (stv090x_activate_modcod_single(state) < 0)
					{
						goto err;
					}
				}
			}
			break;
		}
		case STV090x_SEARCH_AUTO:
		default:
		{
			/* enable DVB-S2 and DVB-S2 in Auto MODE */

			/* first disable ... */
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
#ifndef FS9000
			if (stv090x_vitclk_ctl(state, 0) < 0)
			{
				goto err;
			}
#else
			reg = stv090x_read_reg(state, STV090x_TSTRES0);
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			{
				goto err;
			}
#endif
			if (stv090x_dvbs_track_crl(state) < 0)
			{
				goto err;
			}
			if ((state->dev_ver <= 0x20) || (state->device == STX7111))
			{
				/* enable S2 carrier loop */
				if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x26) < 0)
				{
					goto err;
				}
			}
			else
			{
				/* > Cut 3: Stop carrier 3 */
				if (STV090x_WRITE_DEMOD(state, CAR2CFG, 0x66) < 0)
				{
					goto err;
				}
			}
			if (state->device != STX7111)
			{
				if (state->demod_mode != STV090x_SINGLE)
				{
					/* Cut 2: enable link during search */
					if (stv090x_activate_modcod(state) < 0)
					{
						goto err;
					}
				}
				else
				{
					/* Single demodulator
					 * Authorize SHORT and LONG frames,
					 * QPSK, 8PSK, 16APSK and 32APSK
					 */
					if (stv090x_activate_modcod_single(state) < 0)
					{
						goto err;
					}
				}
				if (stv090x_set_vit_thacq(state) < 0)
				{
					goto err;
				}
				if (stv090x_set_viterbi(state) < 0)
				{
					goto err;
				}
			}
			else
			{
				if (stv090x_set_vit_thacq(state) < 0)
				{
					goto err;
				}
				if (stv090x_set_viterbi(state) < 0)
				{
					goto err;
				}
				if (state->demod_mode != STV090x_SINGLE)
				{
					/* Cut 2: enable link during search */
					if (stv090x_activate_modcod(state) < 0)
					{
						goto err;
					}
				}
				else
				{
					/* Single demodulator
					 * Authorize SHORT and LONG frames,
					 * QPSK, 8PSK, 16APSK and 32APSK
					 */
					if (stv090x_activate_modcod_single(state) < 0)
					{
						goto err;
					}
				}
			}
			break;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_start_search(struct stv090x_state *state)
{
	u32 reg, freq_abs;
	s16 freq;

	dprintk(100, "%s >\n", __func__);

	/* Reset demodulator */
	reg = STV090x_READ_DEMOD(state, DMDISTATE);
	STV090x_SETFIELD_Px(reg, I2C_DEMOD_MODE_FIELD, 0x1f);
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, reg) < 0)
	{
		goto err;
	}
	if (state->dev_ver <= 0x20)
	{
		if (state->srate <= 5000000)
		{
			if (state->device == STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, CARCFG, 0x46) < 0)
				{
					goto err;
				}
			}
			else
			{
				if (STV090x_WRITE_DEMOD(state, CARCFG, 0x44) < 0)
				{
					goto err;
				}
			}
			if (STV090x_WRITE_DEMOD(state, CFRUP1, 0x0f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRUP0, 0xff) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRLOW1, 0xf0) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRLOW0, 0x00) < 0)
			{
				goto err;
			}
			/* Enlarge the timing bandwidth for Low SR */
			if (STV090x_WRITE_DEMOD(state, RTCS2, 0x68) < 0)
			{
				goto err;
			}
		}
		else
		{
			if (state->device == STX7111)
			{
				/* If the symbol rate is >5 Msps
				Set The carrier search up and low to auto mode */
				if (STV090x_WRITE_DEMOD(state, CARCFG, 0xc6) < 0)
				{
					goto err;
				}
			}
			else
			{
				/* If the symbol rate is >5 Msps
				Set The carrier search up and low to auto mode */
				if (STV090x_WRITE_DEMOD(state, CARCFG, 0xc4) < 0)
				{
					goto err;
				}
			}
			/* Reduce the timing bandwidth for high SR */
			if (STV090x_WRITE_DEMOD(state, RTCS2, 0x44) < 0)
			{
				goto err;
			}
		}
	}
	else
	{
		/* >= Cut 3 */
		if (state->srate <= 5000000)
		{
			/* Enlarge the timing bandwidth for Low SR */
			STV090x_WRITE_DEMOD(state, RTCS2, 0x68);
		}
		else
		{
			/* Reduce timing bandwidth for high SR */
			STV090x_WRITE_DEMOD(state, RTCS2, 0x44);
		}
		/* Set CFR min and max to manual mode */
		STV090x_WRITE_DEMOD(state, CARCFG, 0x46);

		if (state->algo == STV090x_WARM_SEARCH)
		{
			/* WARM Start
			 * CFR min = -1MHz,
			 * CFR max = +1MHz
			 */
			freq_abs  = 1000 << 16;
			freq_abs /= (state->mclk / 1000);
			freq      = (s16) freq_abs;
		}
		else
		{
			/* COLD Start
			 * CFR min =- (SearchRange / 2 + 600KHz)
			 * CFR max = +(SearchRange / 2 + 600KHz)
			 * (600KHz for the tuner step size)
			 */
			freq_abs  = (state->search_range / 2000) + 600;
			freq_abs  = freq_abs << 16;
			freq_abs /= (state->mclk / 1000);
			freq      = (s16) freq_abs;
		}
		if (STV090x_WRITE_DEMOD(state, CFRUP1, MSB(freq)) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRUP0, LSB(freq)) < 0)
		{
			goto err;
		}
		freq *= -1;

		if (STV090x_WRITE_DEMOD(state, CFRLOW1, MSB(freq)) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRLOW0, LSB(freq)) < 0)
		{
			goto err;
		}
	}
	if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0) < 0)
	{
		goto err;
	}
	if (state->dev_ver >= 0x20)
	{
		if (STV090x_WRITE_DEMOD(state, EQUALCFG, 0x41) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, FFECFG, 0x41) < 0)
		{
			goto err;
		}

		if ((state->search_mode == STV090x_SEARCH_DVBS1)
		||  (state->search_mode == STV090x_SEARCH_DSS)
		||  (state->search_mode == STV090x_SEARCH_AUTO))
		{
			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x82) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x00) < 0)
			{
				goto err;
			}
		}
	}
	if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x00) < 0)
	{
		goto err;
	}
	if (state->device != STX7111)
	{
		if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0xe0) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0xc0) < 0)
		{
			goto err;
		}
	}
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 0);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DMDCFG2);
	STV090x_SETFIELD_Px(reg, S1S2_SEQUENTIAL_FIELD, 0x0);
	if (STV090x_WRITE_DEMOD(state, DMDCFG2, reg) < 0)
	{
		goto err;
	}
	if (state->device == STX7111)
	{
		if (STV090x_WRITE_DEMOD(state, RTC, 0x88) < 0)
		{
			goto err;
		}
	}
	if (state->dev_ver >= 0x20)
	{
		/* Frequency offset detector setting */
		if (state->srate < 2000000)
		{
			/* konfetti comment: hmmmmmmmmmm the above if clause checks for
			 * >= 0x20 and here we check against <= ... interesting ;)
			 */
			if (state->dev_ver <= 0x20)  // only true if exactly 20...
			{
				/* Cut 2 */
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x39) < 0)
				{
					goto err;
				}
			}
			else // 21 or higher...
			{
				/* Cut 2 */
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x89) < 0)
				{
					goto err;
				}
			}
			if (STV090x_WRITE_DEMOD(state, CARHDR, 0x40) < 0)
			{
				goto err;
			}
		}
		if (state->srate < 10000000)
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x4c) < 0)
			{
				goto err;
			}
			if (state->device != STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, CARHDR, 0x20) < 0)
				{
					goto err;
				}
			}
		}
		else
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x4b) < 0)
			{
				goto err;
			}
			if (state->device != STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, CARHDR, 0x20) < 0)
				{
					goto err;
				}
			}
		}
	}
	else
	{
		if (state->srate < 10000000)
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xef) < 0)
			{
				goto err;
			}
		}
		else
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0xed) < 0)
			{
				goto err;
			}
		}
	}

	switch (state->algo)
	{
		case STV090x_WARM_SEARCH:
		{
			/* The symbol rate and the exact
			 * carrier Frequency are known
			 */
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_COLD_SEARCH:
		{
			/* The symbol rate is known */
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
			{
				goto err;
			}
			break;
		}
		default:
		{
			break;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_get_agc2_min_level(struct stv090x_state *state)
{
	u32 agc2_min = 0xffff, agc2 = 0, freq_init, freq_step, reg;
	s32 i, j, steps, dir;

	dprintk(100, "%s >\n", __func__);

	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 0);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x83) < 0) /* SR = 65 Msps Max */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRUP0, 0xc0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, 0x82) < 0) /* SR= 400 ksps Min */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, 0xa0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x00) < 0) /* stop acq @ coarse carrier state */
	{
		goto err;
	}
	if (stv090x_set_srate(state, 1000000) < 0)
	{
		goto err;
	}
	steps  = state->search_range / 1000000;
	if (steps <= 0)
	{
		steps = 1;
	}
	dir = 1;
	freq_step = (1000000 * 256) / (state->mclk / 256);
	freq_init = 0;

	for (i = 0; i < steps; i++)
	{
		if (dir > 0)
		{
			freq_init = freq_init + (freq_step * i);
		}
		else
		{
			freq_init = freq_init - (freq_step * i);
		}
		dir *= -1;

		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5c) < 0) /* Demod RESET */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, (freq_init >> 8) & 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, freq_init & 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x58) < 0) /* Demod RESET */
		{
			goto err;
		}
		msleep(10);

		agc2 = 0;
		for (j = 0; j < 10; j++)
		{
			agc2 += (STV090x_READ_DEMOD(state, AGC2I1) << 8)
			      | STV090x_READ_DEMOD(state, AGC2I0);
		}
		agc2 /= 10;
		if (agc2 < agc2_min)
		{
			agc2_min = agc2;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return agc2_min;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static u32 stv090x_get_srate(struct stv090x_state *state, u32 clk)
{
	u8 r3, r2, r1, r0;
	s32 srate, int_1, int_2, tmp_1, tmp_2;

	dprintk(100, "%s >\n", __func__);

	r3 = STV090x_READ_DEMOD(state, SFR3);
	r2 = STV090x_READ_DEMOD(state, SFR2);
	r1 = STV090x_READ_DEMOD(state, SFR1);
	r0 = STV090x_READ_DEMOD(state, SFR0);

	srate = ((r3 << 24) | (r2 << 16) | (r1 <<  8) | r0);

	int_1 = clk >> 16;
	int_2 = srate >> 16;

	tmp_1 = clk % 0x10000;
	tmp_2 = srate % 0x10000;

	srate = (int_1 * int_2) + ((int_1 * tmp_2) >> 16) + ((int_2 * tmp_1) >> 16);
	dprintk(100, "%s srate %d<\n", __func__, srate);
	return srate;
}

static u32 stv090x_srate_srch_coarse(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	int tmg_lock = 0, i;
	s32 tmg_cpt = 0, dir = 1, steps, cur_step = 0, freq;
	u32 srate_coarse = 0, agc2 = 0, car_step = 1200, reg;
	u32 agc2th;

	dprintk(100, "%s >\n", __func__);

	if (state->dev_ver >= 0x30)
	{
		agc2th = 0x2e00;
	}
	else
	{
		agc2th = 0x1f00;
	}
	reg = STV090x_READ_DEMOD(state, DMDISTATE);
	STV090x_SETFIELD_Px(reg, I2C_DEMOD_MODE_FIELD, 0x1f); /* Demod RESET */
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, reg) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGCFG, 0x12) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0xc0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0xf0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0xe0) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 1);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x83) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRUP0, 0xc0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, 0x82) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, SFRLOW0, 0xa0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x50) < 0)
	{
		goto err;
	}
	if (state->dev_ver >= 0x30)
	{
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x99) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x98) < 0)
		{
			goto err;
		}
	}
	else if (state->dev_ver >= 0x20)
	{
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x6a) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRSTEP, 0x95) < 0)
		{
			goto err;
		}
	}
	if (state->srate <= 2000000)
	{
		car_step = 1000;
	}
	else if (state->srate <= 5000000)
	{
		car_step = 2000;
	}
	else if (state->srate <= 12000000)
	{
		car_step = 3000;
	}
	else
	{
		car_step = 5000;
	}
	steps  = -1 + ((state->search_range / 1000) / car_step);
	steps /= 2;
	steps  = (2 * steps) + 1;
	if (steps < 0)
	{
		steps = 1;
	}
	else if (steps > 10)
	{
		steps = 11;
		car_step = (state->search_range / 1000) / 10;
	}
	cur_step = 0;
	dir = 1;
	freq = state->frequency;

	while ((!tmg_lock) && (cur_step < steps))
	{
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5f) < 0) /* Demod RESET */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0x00) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0x00) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRINIT1, 0x00) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, SFRINIT0, 0x00) < 0)
		{
			goto err;
		}
		/* trigger acquisition */
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x40) < 0)
		{
			goto err;
		}
		msleep(50);
		for (i = 0; i < 10; i++)
		{
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			if (STV090x_GETFIELD_Px(reg, TMGLOCK_QUALITY_FIELD) >= 2)
			{
				tmg_cpt++;
			}
			agc2 += (STV090x_READ_DEMOD(state, AGC2I1) << 8)
			      |  STV090x_READ_DEMOD(state, AGC2I0);
		}
		agc2 /= 10;
		srate_coarse = stv090x_get_srate(state, state->mclk);
		cur_step++;
		dir *= -1;
		if ((tmg_cpt >= 5)
		&&  (agc2 < agc2th)
		&&  (srate_coarse < 50000000)
		&&  (srate_coarse > 850000))
		{
			tmg_lock = 1;
		}
		else if (cur_step < steps)
		{
			if (dir > 0)
			{
				freq += cur_step * car_step;
			}
			else
			{
				freq -= cur_step * car_step;
			}
			/* Setup tuner */
			if (state->config->tuner_set_frequency)
			{
				if (state->config->tuner_set_frequency(fe, freq) < 0)
				{
					goto err;
				}
			}
			if (state->config->tuner_set_bandwidth)
			{
				if (state->config->tuner_set_bandwidth(fe, state->tuner_bw) < 0)
				{
					goto err;
				}
			}
			msleep(1);
			if (state->config->tuner_get_status)
			{
				if (state->config->tuner_get_status(fe, &reg) < 0)
				{
					goto err;
				}
			}
			if (reg)
			{
				dprintk(10, "Tuner phase locked\n");
			}
			else
			{
				dprintk(10, "Tuner unlocked\n");
			}
		}
	}
	if (!tmg_lock)
	{
		srate_coarse = 0;
	}
	else
	{
		srate_coarse = stv090x_get_srate(state, state->mclk);
	}
	dprintk(50, "%s srate_coarse %d<\n", __func__, srate_coarse);
	return srate_coarse;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static u32 stv090x_srate_srch_fine(struct stv090x_state *state)
{
	u32 srate_coarse, freq_coarse, sym, reg;

	srate_coarse = stv090x_get_srate(state, state->mclk);
	freq_coarse  = STV090x_READ_DEMOD(state, CFR2) << 8;
	freq_coarse |= STV090x_READ_DEMOD(state, CFR1);
	sym = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */

	dprintk(100, "%s >\n", __func__);

	if (sym < state->srate)
	{
		srate_coarse = 0;
	}
	else
	{
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0) /* Demod RESET */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0xc1) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0x20) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0x00) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGCFG, 0xd2) < 0)
		{
			goto err;
		}
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
		{
			goto err;
		}
		if (state->dev_ver >= 0x30)
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x79) < 0)
			{
				goto err;
			}
		}
		else if (state->dev_ver >= 0x20)
		{
			if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
			{
				goto err;
			}
		}
		if (srate_coarse > 3000000)
		{
			sym  = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */
			sym  = (sym / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0)
			{
				goto err;
			}
			sym  = 10 * (srate_coarse / 13); /* SFRLOW = SFR - 30% */
			sym  = (sym / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRLOW1, (sym >> 8) & 0x7f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRLOW0, sym & 0xff) < 0)
			{
				goto err;
			}
			sym  = (srate_coarse / 1000) * 65536;
			sym /= (state->mclk / 1000);
			if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0xff) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRINIT0, sym & 0xff) < 0)
			{
				goto err;
			}
		}
		else
		{
			sym  = 13 * (srate_coarse / 10); /* SFRUP = SFR + 30% */
			sym  = (sym / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRUP1, (sym >> 8) & 0x7f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRUP0, sym & 0xff) < 0)
			{
				goto err;
			}
			sym  = 10 * (srate_coarse / 14); /* SFRLOW = SFR - 30% */
			sym  = (sym / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRLOW1, (sym >> 8) & 0x7f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRLOW0, sym & 0xff) < 0)
			{
				goto err;
			}
			sym  = (srate_coarse / 100) * 65536;
			sym /= (state->mclk / 100);
			if (STV090x_WRITE_DEMOD(state, SFRINIT1, (sym >> 8) & 0xff) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, SFRINIT0, sym & 0xff) < 0)
			{
				goto err;
			}
		}
		if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x20) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, (freq_coarse >> 8) & 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, freq_coarse & 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0) /* trigger acquisition */
		{
			goto err;
		}
	}
	dprintk(100, "%s <\n", __func__);
	return srate_coarse;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_get_dmdlock(struct stv090x_state *state, s32 timeout)
{
	s32 timer = 0, lock = 0;
	u32 reg;
	u8 stat;

	dprintk(100, "%s >\n", __func__);
	while ((timer < timeout) && (!lock))
	{
		reg = STV090x_READ_DEMOD(state, DMDSTATE);
		stat = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

		dprintk(100, "demod stat = %d\n", stat);
		switch (stat)
		{
			case 0: /* searching */
			case 1: /* first PLH detected */
			default:
			{
				dprintk(150, "Demodulator searching ..\n");
				lock = 0;
				break;
			}
			case 2: /* DVB-S2 mode */
			case 3: /* DVB-S1/legacy mode */
			{
				reg = STV090x_READ_DEMOD(state, DSTATUS);
				lock = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
				break;
			}
		}
		if (!lock)
		{
			msleep(10);
		}
		else
		{
			dprintk(100, "Demodulator acquired LOCK\n");
		}
		timer += 10;
	}
	if (lock)
	{
		dprintk(50, "%s lock %d<\n", __func__, lock);
	}
	return lock;
}

static int stv090x_blind_search(struct stv090x_state *state)
{
	u32 agc2, reg, srate_coarse;
	s32 cpt_fail, agc2_ovflw, i;
	u8 k_ref, k_max, k_min;
	int coarse_fail = 0;
	int lock;

	k_max = 110;
	k_min = 10;

	agc2 = stv090x_get_agc2_min_level(state);

	if (agc2 > STV090x_SEARCH_AGC2_TH(state->dev_ver))
	{
		lock = 0;
	}
	else
	{
		if (state->dev_ver <= 0x20)
		{
			if (STV090x_WRITE_DEMOD(state, CARCFG, 0xc4) < 0)
			{
				goto err;
			}
		}
		else
		{
			/* > Cut 3 */
			if (STV090x_WRITE_DEMOD(state, CARCFG, 0x06) < 0)
			{
				goto err;
			}
		}
		if (STV090x_WRITE_DEMOD(state, RTCS2, 0x44) < 0)
		{
			goto err;
		}
		if (state->dev_ver >= 0x20)
		{
			if (STV090x_WRITE_DEMOD(state, EQUALCFG, 0x41) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, FFECFG, 0x41) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x82) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x00) < 0) /* set viterbi hysteresis */
			{
				goto err;
			}
		}
		k_ref = k_max;
		do
		{
			if (STV090x_WRITE_DEMOD(state, KREFTMG, k_ref) < 0)
			{
				goto err;
			}
			if (stv090x_srate_srch_coarse(state) != 0)
			{
				srate_coarse = stv090x_srate_srch_fine(state);
				if (srate_coarse != 0)
				{
					stv090x_get_lock_tmg(state);
					lock = stv090x_get_dmdlock(state, state->DemodTimeout);
				}
				else
				{
					lock = 0;
				}
			}
			else
			{
				cpt_fail = 0;
				agc2_ovflw = 0;
				for (i = 0; i < 10; i++)
				{
					agc2 += (STV090x_READ_DEMOD(state, AGC2I1) << 8)
					      |  STV090x_READ_DEMOD(state, AGC2I0);
					if (agc2 >= 0xff00)
					{
						agc2_ovflw++;
					}
					reg = STV090x_READ_DEMOD(state, DSTATUS2);
					if ((STV090x_GETFIELD_Px(reg, CFR_OVERFLOW_FIELD) == 0x01)
					&&  (STV090x_GETFIELD_Px(reg, DEMOD_DELOCK_FIELD) == 0x01))
					{
						cpt_fail++;
					}
				}
				if ((cpt_fail > 7) || (agc2_ovflw > 7))
				{
					coarse_fail = 1;
				}
				lock = 0;
			}
			k_ref -= 20;
		}
		while ((k_ref >= k_min) && (!lock) && (!coarse_fail));
	}
	return lock;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_chk_tmg(struct stv090x_state *state)
{
	u32 reg;
	s32 tmg_cpt = 0, i;
	u8 freq, tmg_thh, tmg_thl;
	int tmg_lock = 0;

	dprintk(100, "%s >\n", __func__);

	freq = STV090x_READ_DEMOD(state, CARFREQ);
	tmg_thh = STV090x_READ_DEMOD(state, TMGTHRISE);
	tmg_thl = STV090x_READ_DEMOD(state, TMGTHFALL);
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, 0x20) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, 0x00) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DMDCFGMD);
	STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00); /* stop carrier offset search */
	if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, RTC, 0x80) < 0)
	{
		goto err;
	}

	if (STV090x_WRITE_DEMOD(state, RTCS2, 0x40) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x00) < 0)
	{
		goto err;
	}

	if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0x00) < 0) /* set car ofset to 0 */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0x00) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x65) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* trigger acquisition */
	{
		goto err;
	}
	msleep(10);

	for (i = 0; i < 10; i++)
	{
		reg = STV090x_READ_DEMOD(state, DSTATUS);
		if (STV090x_GETFIELD_Px(reg, TMGLOCK_QUALITY_FIELD) >= 2)
		{
			tmg_cpt++;
		}
		msleep(1);
	}
	if (tmg_cpt >= 3)
	{
		tmg_lock = 1;
	}
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, RTC, 0x88) < 0) /* DVB-S1 timing */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, RTCS2, 0x68) < 0) /* DVB-S2 timing */
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, CARFREQ, freq) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGTHRISE, tmg_thh) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, TMGTHFALL, tmg_thl) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return	tmg_lock;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_get_coldlock(struct stv090x_state *state, s32 timeout_dmd)
{
	struct dvb_frontend *fe = &state->frontend;

	u32 reg;
	s32 car_step, steps, cur_step, dir, freq, timeout_lock;
	int lock = 0;

	dprintk(100, "%s >\n", __func__);

	if (state->srate >= 10000000)
	{
		timeout_lock = timeout_dmd / 3;
	}
	else
	{
		timeout_lock = timeout_dmd / 2;
	}

	lock = stv090x_get_dmdlock(state, timeout_lock); /* cold start wait */
	if (!lock)
	{
		if (state->srate >= 10000000)
		{
			if (stv090x_chk_tmg(state))
			{
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
				{
					goto err;
				}
				lock = stv090x_get_dmdlock(state, timeout_dmd);
			}
			else
			{
				lock = 0;
			}
		}
		else
		{
			//note: state->srate < 10000000
			if (state->srate <= 4000000)
			{
				car_step = 1000;
			}
			else if (state->srate <= 7000000)
			{
				car_step = 2000;
			}
			else if (state->srate <= 10000000)
			{
				car_step = 3000;
			}
			else
			{
				car_step = 5000; //never reached??
			}
			steps  = (state->search_range / 1000) / car_step;
			steps /= 2;
			steps  = 2 * (steps + 1);
			if (steps < 0)
			{
				steps = 2;
			}
			else if (steps > 12)
			{
				steps = 12;
			}
			cur_step = 1;
			dir = 1;

			if (!lock)
			{
				freq = state->frequency;
				state->tuner_bw = stv090x_car_width(state->srate, state->rolloff) + state->srate;
				while ((cur_step <= steps) && (!lock))
				{
					if (dir > 0)
					{
						freq += cur_step * car_step;
					}
					else
					{
						freq -= cur_step * car_step;
					}
					/* Setup tuner */
					if (state->config->tuner_set_frequency)
					{
						if (state->config->tuner_set_frequency(fe, freq) < 0)
						{
							goto err;
						}
					}

					if (state->config->tuner_set_bandwidth)
					{
						if (state->config->tuner_set_bandwidth(fe, state->tuner_bw) < 0)
						{
							goto err;
						}
					}
					msleep(1);

					if (state->config->tuner_get_status)
					{
						if (state->config->tuner_get_status(fe, &reg) < 0)
						{
							goto err;
						}
					}
					if (reg)
					{
						dprintk(50, "Tuner phase locked\n");
					}
					else
					{
						dprintk(50, "Tuner unlocked\n");
					}
					STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c);

					/* FIXME: not sure here! is it also for dvbs1? */
//					if (state->delsys == STV090x_DVBS2)
					if (state->search_mode != STV090x_SEARCH_DSS)
					{
						reg = STV090x_READ_DEMOD(state, DMDCFGMD);
						STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
						STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);

						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
						{
							goto err;
						}
						reg = STV090x_READ_DEMOD(state, DMDCFGMD);
						STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
						STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);

						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
						{
							goto err;
						}
					}

					if (STV090x_WRITE_DEMOD(state, CFRINIT1, 0x00) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, CFRINIT0, 0x00) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x15) < 0)
					{
						goto err;
					}
					lock = stv090x_get_dmdlock(state, (timeout_dmd / 3));

					dir *= -1;
					cur_step++;
				}
			}
		}
	}
	dprintk(100, "%s <\n", __func__);
	return lock;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_get_loop_params(struct stv090x_state *state, s32 *freq_inc, s32 *timeout_sw, s32 *steps)
{
	s32 timeout, inc, steps_max, srate, car_max;

	dprintk(100, "%s >\n", __func__);
	srate = state->srate;
	car_max = state->search_range / 1000;
	car_max += car_max / 10;
	car_max  = 65536 * (car_max / 2);
	car_max /= (state->mclk / 1000);

	if (car_max > 0x4000)
	{
		car_max = 0x4000 ; /* maxcarrier should be<= +-1/4 Mclk */
	}
	inc  = srate;
	inc /= state->mclk / 1000;
	inc *= 256;
	inc *= 256;
	inc /= 1000;

	switch (state->search_mode)
	{
		case STV090x_SEARCH_DVBS1:
		case STV090x_SEARCH_DSS:
		{
			inc *= 3; /* freq step = 3% of srate */
			timeout = 20;
			break;
		}
		case STV090x_SEARCH_DVBS2:
		{
			inc *= 4;
			timeout = 25;
			break;
		}
		case STV090x_SEARCH_AUTO:
		default:
		{
			inc *= 3;
			timeout = 25;
			break;
		}
	}
	inc /= 100;
	if ((inc > car_max) || (inc < 0))
	{
		inc = car_max / 2; /* increment <= 1/8 Mclk */
	}
	timeout *= 27500; /* 27.5 Msps reference */
	if (srate > 0)
	{
		timeout /= (srate / 1000);
	}
	if ((timeout > 100) || (timeout < 0))
	{
		timeout = 100;
	}
	steps_max = (car_max / inc) + 1; /* min steps = 3 */
	if ((steps_max > 100) || (steps_max < 0))
	{
		steps_max = 100; /* max steps <= 100 */
		inc = car_max / steps_max;
	}
	*freq_inc = inc;
	*timeout_sw = timeout;
	*steps = steps_max;
	dprintk(100, "%s <\n", __func__);
	return 0;
}

static int stv090x_chk_signal(struct stv090x_state *state)
{
	s32 offst_car, agc2, car_max;
	int no_signal;

	dprintk(100, "%s >\n", __func__);
	offst_car  = STV090x_READ_DEMOD(state, CFR2) << 8;
	offst_car |= STV090x_READ_DEMOD(state, CFR1);
	offst_car = comp2(offst_car, 16);

	agc2  = STV090x_READ_DEMOD(state, AGC2I1) << 8;
	agc2 |= STV090x_READ_DEMOD(state, AGC2I0);
	car_max = state->search_range / 1000;

	car_max += (car_max / 10); /* 10% margin */
	car_max  = (65536 * car_max / 2);
	car_max /= state->mclk / 1000;

	if (car_max > 0x4000)
	{
		car_max = 0x4000;
	}

	if ((agc2 > 0x2000) || (offst_car > 2 * car_max) || (offst_car < -2 * car_max))
	{
		no_signal = 1;
		dprintk(10, "No Signal\n");
	}
	else
	{
		no_signal = 0;
		dprintk(10, "Found Signal\n");
	}
	dprintk(100, "%s no_signal %d>\n", __func__, no_signal);
	return no_signal;
}

static int stv090x_search_car_loop(struct stv090x_state *state, s32 inc, s32 timeout, int zigzag, s32 steps_max)
{
	int no_signal, lock = 0;
	s32 cpt_step = 0, offst_freq, car_max;
	u32 reg;

	dprintk(100, "%s timeout inc %d, %d, zigzag %d, setps_max %d>\n", __func__, inc, timeout, zigzag, steps_max);

	car_max  = state->search_range / 1000;
	car_max += (car_max / 10);
	car_max  = (65536 * car_max / 2);
	car_max /= (state->mclk / 1000);
	if (car_max > 0x4000)
	{
		car_max = 0x4000;
	}
	if (zigzag)
	{
		offst_freq = 0;
	}
	else
	{
		offst_freq = -car_max + inc;
	}

	do
	{
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, ((offst_freq / 256) & 0xff)) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, offst_freq & 0xff) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
		{
			goto err;
		}
		reg = STV090x_READ_DEMOD(state, PDELCTRL1);
		STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x1); /* stop DVB-S2 packet delin */
		if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
		{
			goto err;
		}
		if (zigzag)
		{
			if (offst_freq >= 0)
			{
				offst_freq = -offst_freq - 2 * inc;
			}
			else
			{
				offst_freq = -offst_freq;
			}
		}
		else
		{
			offst_freq += 2 * inc;
		}
		cpt_step++;

		lock = stv090x_get_dmdlock(state, timeout);
		no_signal = stv090x_chk_signal(state);

		dprintk(100, "%s: no_signal  = %d\n", __func__, no_signal);
		dprintk(100, "%s: lock       = %d\n", __func__, lock);
		dprintk(100, "%s: offst_freq = %d\n", __func__, offst_freq);
		dprintk(100, "%s: inc        = %d\n", __func__, inc);
		dprintk(100, "%s: car_max    = %d\n", __func__, car_max);
		dprintk(100, "%s: cpt_step   = %d\n", __func__, cpt_step);
		dprintk(100, "%s: steps_max  = %d\n", __func__, steps_max);
	}
	while ((!lock)
	&&     (!no_signal)
	&&     ((offst_freq - inc) < car_max)
	&&     ((offst_freq + inc) > -car_max)
	&&     (cpt_step < steps_max));

	reg = STV090x_READ_DEMOD(state, PDELCTRL1);
	STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0);
	if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
	{
		goto err;
	}
	dprintk(100, "%s <\n", __func__);
	return lock;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static int stv090x_sw_algo(struct stv090x_state *state)
{
	int no_signal, zigzag, lock = 0;
	u32 reg;

	s32 dvbs2_fly_wheel;
	s32 inc, timeout_step, trials, steps_max;

	dprintk(100, "%s >\n", __func__);

	/* get params */
	stv090x_get_loop_params(state, &inc, &timeout_step, &steps_max);

	switch (state->search_mode)
	{
		case STV090x_SEARCH_DVBS1:
		case STV090x_SEARCH_DSS:
		{
			/* accelerate the frequency detector */
			if (state->dev_ver >= 0x20)
			{
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x3B) < 0)
				{
					goto err;
				}
			}
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x49) < 0)
			{
				goto err;
			}
			zigzag = 0;
			break;
		}
		case STV090x_SEARCH_DVBS2:
		{
			if (state->dev_ver >= 0x20)
			{
				if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
				{
					goto err;
				}
			}
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x89) < 0)
			{
				goto err;
			}
			zigzag = 1;
			break;
		}
		case STV090x_SEARCH_AUTO:
		default:
		{
			/* accelerate the frequency detector */
			if (state->dev_ver >= 0x20)
			{
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x3b) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
				{
					goto err;
				}
			}
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0xc9) < 0)
			{
				goto err;
			}
			zigzag = 0;
			break;
		}
	}
	trials = 0;
	do
	{
		lock = stv090x_search_car_loop(state, inc, timeout_step, zigzag, steps_max);
		no_signal = stv090x_chk_signal(state);
		trials++;

		/* Run the SW search 2 times maximum */
		if (lock || no_signal || (trials == 2))
		{
			/* Check if the demod is not losing lock in DVBS2 */
			if (state->dev_ver >= 0x20)
			{
				if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x9e) < 0)
				{
					goto err;
				}
			}
			reg = STV090x_READ_DEMOD(state, DMDSTATE);
			if ((lock) && (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == STV090x_DVBS2))
			{
				/* Check if the demod is not losing lock in DVBS2 */
				msleep(timeout_step);
				reg = STV090x_READ_DEMOD(state, DMDFLYW);
				dvbs2_fly_wheel = STV090x_GETFIELD_Px(reg, FLYWHEEL_CPT_FIELD);
				if (dvbs2_fly_wheel < 0xd)
				{
					/* if correct frames is decrementing */
					msleep(timeout_step);
					reg = STV090x_READ_DEMOD(state, DMDFLYW);
					dvbs2_fly_wheel = STV090x_GETFIELD_Px(reg, FLYWHEEL_CPT_FIELD);
				}
				if (dvbs2_fly_wheel < 0xd)
				{
					/* FALSE lock, The demod is loosing lock */
					lock = 0;
					if (trials < 2)
					{
						if (state->dev_ver >= 0x20)
						{
							if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x79) < 0)
							{
								goto err;
							}
						}
						if (STV090x_WRITE_DEMOD(state, DMDCFGMD, 0x89) < 0)
						{
							goto err;
						}
					}
				}
			}
		}
		dprintk(100, "%s: no_signal  = %d\n", __func__, no_signal);
		dprintk(100, "%s: lock       = %d\n", __func__, lock);
		dprintk(100, "%s: trials     = %d\n", __func__, trials);
	}
	while ((!lock) && (trials < 2) && (!no_signal));

	dprintk(100, "%s lock %d<\n", __func__, lock);
	return lock;

err:
	dprintk(1, "%s I/O error\n", __func__);
	return -1;
}

static enum stv090x_delsys stv090x_get_std(struct stv090x_state *state)
{
	u32 reg;
	enum stv090x_delsys delsys;

	dprintk(10, "%s >\n", __func__);

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 2)
	{
		delsys = STV090x_DVBS2;
	}
	else if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 3)
	{
		reg = STV090x_READ_DEMOD(state, FECM);
		if (STV090x_GETFIELD_Px(reg, DSS_DVB_FIELD) == 1)
		{
			delsys = STV090x_DSS;
		}
		else
		{
			delsys = STV090x_DVBS1;
		}
	}
	else
	{
		delsys = STV090x_ERROR;
	}
	dprintk(10, "%s delsys %d <\n", __func__, delsys);
	return delsys;
}

/* in Hz */
static s32 stv090x_get_car_freq(struct stv090x_state *state, u32 mclk)
{
	s32 derot, int_1, int_2, tmp_1, tmp_2;

	dprintk(10, "%s >\n", __func__);

	derot  = STV090x_READ_DEMOD(state, CFR2) << 16;
	derot |= STV090x_READ_DEMOD(state, CFR1) <<  8;
	derot |= STV090x_READ_DEMOD(state, CFR0);

	derot = comp2(derot, 24);
	int_1 = mclk >> 12;
	int_2 = derot >> 12;

	/* carrier_frequency = MasterClock * Reg / 2^24 */
	tmp_1 = mclk % 0x1000;
	tmp_2 = derot % 0x1000;

	derot = (int_1 * int_2) + ((int_1 * tmp_2) >> 12) + ((int_2 * tmp_1) >> 12);
	dprintk(10, "%s derot %d <\n", __func__, derot);
	return derot;
}

static int stv090x_get_viterbi(struct stv090x_state *state)
{
	u32 reg, rate;

	dprintk(10, "%s >\n", __func__);

	reg = STV090x_READ_DEMOD(state, VITCURPUN);
	rate = STV090x_GETFIELD_Px(reg, VIT_CURPUN_FIELD);

	switch (rate)
	{
		case 13:
		{
			state->fec = STV090x_PR12;
			break;
		}
		case 18:
		{
			state->fec = STV090x_PR23;
			break;
		}
		case 21:
		{
			state->fec = STV090x_PR34;
			break;
		}
		case 24:
		{
			state->fec = STV090x_PR56;
			break;
		}
		case 25:
		{
			state->fec = STV090x_PR67;
			break;
		}
		case 26:
		{
			state->fec = STV090x_PR78;
			break;
		}
		default:
		{
			state->fec = STV090x_PRERR;
			break;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;
}

static enum stv090x_signal_state stv090x_get_sig_params(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	u8 tmg;
	u32 reg;
	s32 i = 0, offst_freq;

	dprintk(10, "%s: >\n", __func__);

	msleep(1);

	if (state->algo == STV090x_BLIND_SEARCH)
	{
		tmg = STV090x_READ_DEMOD(state, TMGREG2);
		STV090x_WRITE_DEMOD(state, SFRSTEP, 0x5c);
		while ((i <= 50) && (tmg != 0) && (tmg != 0xff))
		{
			tmg = STV090x_READ_DEMOD(state, TMGREG2);
			msleep(1);
			i += 5;
		}
	}
	state->delsys = stv090x_get_std(state);

	dprintk(50, "delsys from hw = %d\n", state->delsys);

	if (state->config->tuner_get_frequency)
	{
		if (state->config->tuner_get_frequency(fe, &state->frequency) < 0)
		{
			goto err;
		}
	}

	offst_freq = stv090x_get_car_freq(state, state->mclk) / 1000;
	state->frequency += offst_freq;

	if (stv090x_get_viterbi(state) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DMDMODCOD);
	state->modcod = STV090x_GETFIELD_Px(reg, DEMOD_MODCOD_FIELD);

	dprintk(50, "%s: modcod %d\n", __func__, state->modcod);

	state->pilots = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) & 0x01;

	dprintk(50, "%s: pilots %d\n", __func__, state->pilots);

	state->frame_len = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) >> 1;

	dprintk(50, "%s: frame_len %d\n", __func__, state->frame_len);

	reg = STV090x_READ_DEMOD(state, TMGOBS);
	state->rolloff = STV090x_GETFIELD_Px(reg, ROLLOFF_STATUS_FIELD);

	dprintk(50, "%s: rolloff %d\n", __func__, state->rolloff);

	reg = STV090x_READ_DEMOD(state, FECM);
	state->inversion = STV090x_GETFIELD_Px(reg, IQINV_FIELD);

	dprintk(50, "%s: inversion %d\n", __func__, state->inversion);

	if ((state->algo == STV090x_BLIND_SEARCH) || (state->srate < 10000000))
	{
		int car_width;
		dprintk(100, "%s: 1.\n", __func__);

		if (state->config->tuner_get_frequency)
		{
			if (state->config->tuner_get_frequency(fe, &state->frequency) < 0)
			{
				goto err;
			}
		}
		if (abs(offst_freq) <= ((state->search_range / 2000) + 500))
		{
			dprintk(100, "%s: rangeok1\n", __func__);
			return STV090x_RANGEOK;
		}
		else if (abs(offst_freq) <= (car_width = stv090x_car_width(state->srate, state->rolloff) / 2000))
		{
			dprintk(100, "%s: rangeok2\n", __func__);
			return STV090x_RANGEOK;
		}
		else
		{
			dprintk(10, "%s: out of range %ld > %d\n", __func__, abs(offst_freq), car_width);
			return STV090x_OUTOFRANGE; /* Out of Range */
		}
	}
	else
	{
		dprintk(100, "%s: 2.\n", __func__);
		if (abs(offst_freq) <= ((state->search_range / 2000) + 500))
		{
			dprintk(100, "%s: rangeok\n", __func__);
			return STV090x_RANGEOK;
		}
		else
		{
			dprintk(100, "%s: out of range %ld > %d\n", __func__, abs(offst_freq), (state->search_range / 2000) + 500);
			return STV090x_OUTOFRANGE;
		}
	}
	dprintk(10, "%s: out of range <\n", __func__);
	return STV090x_OUTOFRANGE;

err:
	dprintk(1, "stv090x_signal_state stv090x_get_sig_params: I/O error\n");
	return -1;
}

static u32 stv090x_get_tmgoffst(struct stv090x_state *state, u32 srate)
{
	s32 offst_tmg;

	dprintk(10, "%s >\n", __func__);
	offst_tmg  = STV090x_READ_DEMOD(state, TMGREG2) << 16;
	offst_tmg |= STV090x_READ_DEMOD(state, TMGREG1) <<  8;
	offst_tmg |= STV090x_READ_DEMOD(state, TMGREG0);

	offst_tmg = comp2(offst_tmg, 24); /* 2's complement */
	if (!offst_tmg)
	{
		offst_tmg = 1;
	}
	offst_tmg  = ((s32) srate * 10) / ((s32) 0x1000000 / offst_tmg);
	offst_tmg /= 320;

	dprintk(10, "%s <\n", __func__);
	return offst_tmg;
}

static u8 stv090x_optimize_carloop(struct stv090x_state *state, enum stv090x_modcod modcod, s32 pilots)
{
	u8 aclc = 0x29;
	s32 i;
	struct stv090x_long_frame_crloop *car_loop, *car_loop_qpsk_low, *car_loop_apsk_low;

	dprintk(10, "%s >\n", __func__);

	/* FIXME: we should warn in other cut cases if device is stx7111 because values
	 * are unknown
	 */
	if ((state->dev_ver == 0x20) && (state->device == STX7111))
	{
		printk("%s STX7111 cut 0x20 handling (modcod %d, pilots %d)\n", __func__, modcod, pilots);
		car_loop          = stx7111_s2_crl_cut20;
#warning stv090x: fixme do not know lowqpsk crl
		car_loop_qpsk_low = stv090x_s2_lowqpsk_crl_cut20;
		/* FIXME: do not know apsk values */
		car_loop_apsk_low = stv090x_s2_apsk_crl_cut20;
	}
	else if (state->dev_ver == 0x20)
	{
		car_loop          = stv090x_s2_crl_cut20;
		car_loop_qpsk_low = stv090x_s2_lowqpsk_crl_cut20;
		car_loop_apsk_low = stv090x_s2_apsk_crl_cut20;
	}
	else
	{
		/* >= Cut 3 */
		car_loop          = stv090x_s2_crl_cut30;
		car_loop_qpsk_low = stv090x_s2_lowqpsk_crl_cut30;
		car_loop_apsk_low = stv090x_s2_apsk_crl_cut30;
	}

	if (modcod < STV090x_QPSK_12)
	{
		i = 0;
		while ((i < 3) && (modcod != car_loop_qpsk_low[i].modcod))
		{
			i++;
		}
		if (i >= 3)
		{
			i = 2;
		}
	}
	else
	{
		i = 0;
		while ((i < 14) && (modcod != car_loop[i].modcod))
		{
			i++;
		}
		if (i >= 14)
		{
			i = 0;
			while ((i < 11) && (modcod != car_loop_apsk_low[i].modcod))
			{
				i++;
			}
			if (i >= 11)
			{
				i = 10;
			}
		}
	}

	if (modcod <= STV090x_QPSK_25)
	{
		if (pilots)
		{
			if (state->srate <= 3000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_on_2;
			}
			else if (state->srate <= 7000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_on_5;
			}
			else if (state->srate <= 15000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_on_10;
			}
			else if (state->srate <= 25000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_on_20;
			}
			else
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_on_30;
			}
		}
		else
		{
			if (state->srate <= 3000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_off_2;
			}
			else if (state->srate <= 7000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_off_5;
			}
			else if (state->srate <= 15000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_off_10;
			}
			else if (state->srate <= 25000000)
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_off_20;
			}
			else
			{
				aclc = car_loop_qpsk_low[i].crl_pilots_off_30;
			}
		}
	}
	else if (modcod <= STV090x_8PSK_910)
	{
		if (pilots)
		{
			if (state->srate <= 3000000)
			{
				aclc = car_loop[i].crl_pilots_on_2;
			}
			else if (state->srate <= 7000000)
			{
				aclc = car_loop[i].crl_pilots_on_5;
			}
			else if (state->srate <= 15000000)
			{
				aclc = car_loop[i].crl_pilots_on_10;
			}
			else if (state->srate <= 25000000)
			{
				aclc = car_loop[i].crl_pilots_on_20;
			}
			else
			{
				aclc = car_loop[i].crl_pilots_on_30;
			}
		}
		else
		{
			if (state->srate <= 3000000)
			{
				aclc = car_loop[i].crl_pilots_off_2;
			}
			else if (state->srate <= 7000000)
			{
				aclc = car_loop[i].crl_pilots_off_5;
			}
			else if (state->srate <= 15000000)
			{
				aclc = car_loop[i].crl_pilots_off_10;
			}
			else if (state->srate <= 25000000)
			{
				aclc = car_loop[i].crl_pilots_off_20;
			}
			else
			{
				aclc = car_loop[i].crl_pilots_off_30;
			}
		}
	}
	else
	{  /* 16APSK and 32APSK */
		if (state->srate <= 3000000)
		{
			aclc = car_loop_apsk_low[i].crl_pilots_on_2;
		}
		else if (state->srate <= 7000000)
		{
			aclc = car_loop_apsk_low[i].crl_pilots_on_5;
		}
		else if (state->srate <= 15000000)
		{
			aclc = car_loop_apsk_low[i].crl_pilots_on_10;
		}
		else if (state->srate <= 25000000)
		{
			aclc = car_loop_apsk_low[i].crl_pilots_on_20;
		}
		else
		{
			aclc = car_loop_apsk_low[i].crl_pilots_on_30;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return aclc;
}

static u8 stv090x_optimize_carloop_short(struct stv090x_state *state)
{
	struct stv090x_short_frame_crloop *short_crl = NULL;
	s32 index = 0;
	u8 aclc = 0x0b;

	dprintk(10, "%s >\n", __func__);
	switch (state->modulation)
	{
		case STV090x_QPSK:
		default:
		{
			index = 0;
			break;
		}
		case STV090x_8PSK:
		{
			index = 1;
			break;
		}
		case STV090x_16APSK:
		{
			index = 2;
			break;
		}
		case STV090x_32APSK:
		{
			index = 3;
			break;
		}
	}
	if (state->dev_ver >= 0x30)
	{
		/* Cut 3.0 and up */
		short_crl = stv090x_s2_short_crl_cut30;
	}
	else
	{
		/* Cut 2.0 and up: we don't support cuts older than 2.0 */
		short_crl = stv090x_s2_short_crl_cut20;
	}

	if (state->srate <= 3000000)
	{
		aclc = short_crl[index].crl_2;
	}
	else if (state->srate <= 7000000)
	{
		aclc = short_crl[index].crl_5;
	}
	else if (state->srate <= 15000000)
	{
		aclc = short_crl[index].crl_10;
	}
	else if (state->srate <= 25000000)
	{
		aclc = short_crl[index].crl_20;
	}
	else
	{
		aclc = short_crl[index].crl_30;
	}
	dprintk(10, "%s <\n", __func__);
	return aclc;
}

static int stv090x_optimize_track(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;

	enum stv090x_rolloff rolloff;
	enum stv090x_modcod modcod;

	s32 srate, pilots, aclc, f_1, f_0, i = 0, blind_tune = 0;
	u32 reg;

	dprintk(10, "%s >\n", __func__);

	srate  = stv090x_get_srate(state, state->mclk);
	srate += stv090x_get_tmgoffst(state, srate);

	switch (state->delsys)
	{
		case STV090x_DVBS1:
		case STV090x_DSS:
		{
			dprintk(50, "STV090x_DVBS1\n");
			if (state->search_mode == STV090x_SEARCH_AUTO)
			{
				reg = STV090x_READ_DEMOD(state, DMDCFGMD);
				STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
				STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 0);
				if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
				{
					goto err;
				}
			}
			reg = STV090x_READ_DEMOD(state, DEMOD);
			STV090x_SETFIELD_Px(reg, ROLLOFF_CONTROL_FIELD, state->rolloff);
			STV090x_SETFIELD_Px(reg, MANUAL_SXROLLOFF_FIELD, 0x01);
			if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			{
				goto err;
			}
#ifndef FS9000
			if (state->device == STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x73) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, U1, 0x73) < 0)
				{
					goto err;
				}
			}
			if ((state->dev_ver >= 0x30) && (state->device != STX7111))
			{
				if (stv090x_get_viterbi(state) < 0)
				{
					goto err;
				}
				if (state->fec == STV090x_PR12)
				{
					if (STV090x_WRITE_DEMOD(state, GAUSSR0, 0x98) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, CCIR0, 0x18) < 0)
					{
						goto err;
					}
				}
				else
				{
					if (STV090x_WRITE_DEMOD(state, GAUSSR0, 0x18) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, CCIR0, 0x18) < 0)
					{
						goto err;
					}
				}
			}
			else if ((state->dev_ver >= 0x20) && (state->device == STX7111))
			{
				if (srate >= 15000000)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC, 0x2b) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, BCLC, 0x2b) < 0)
					{
						goto err;
					}
				}
				else if ((srate >= 7000000) && (15000000 > srate))
				{
					if (STV090x_WRITE_DEMOD(state, ACLC, 0x0c) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, BCLC, 0x1b) < 0)
					{
						goto err;
					}
				}
				else if (srate < 7000000)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC, 0x2c) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, BCLC, 0x1c) < 0)
					{
						goto err;
					}
				}
				stv090x_get_viterbi(state); 
				if (state->fec == STV090x_PR12)
				{
					STV090x_WRITE_DEMOD(state, GAUSSR0, 0x98);
					STV090x_WRITE_DEMOD(state, CCIR0, 0x18);
				}
				else
				{
					STV090x_WRITE_DEMOD(state, GAUSSR0, 0x18);
					STV090x_WRITE_DEMOD(state, CCIR0, 0x18);
				}
			}
#endif
			if (state->device != STX7111)
			{
				if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x75) < 0)
				{
					goto err;
				}
			}
			break;
		}
		case STV090x_DVBS2:
		{
			dprintk(50, "STV090x_DVBS2\n");

			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 0);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, ACLC, 0) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, BCLC, 0) < 0)
			{
				goto err;
			}
#if 1  // #ifndef FS9000
			if (state->frame_len == STV090x_LONG_FRAME)
			{
				reg = STV090x_READ_DEMOD(state, DMDMODCOD);
				modcod = STV090x_GETFIELD_Px(reg, DEMOD_MODCOD_FIELD);
				pilots = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) & 0x01;
				aclc = stv090x_optimize_carloop(state, modcod, pilots);
				if (modcod <= STV090x_QPSK_910)
				{
					STV090x_WRITE_DEMOD(state, ACLC2S2Q, aclc);
				}
				else if (modcod <= STV090x_8PSK_910)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, ACLC2S28, aclc) < 0)
					{
						goto err;
					}
				}
				if ((state->demod_mode == STV090x_SINGLE) && (modcod > STV090x_8PSK_910))
				{
					if (modcod <= STV090x_16APSK_910)
					{
						if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
						{
							goto err;
						}
						if (STV090x_WRITE_DEMOD(state, ACLC2S216A, aclc) < 0)
						{
							goto err;
						}
					}
					else
					{
						if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
						{
							goto err;
						}
						if (STV090x_WRITE_DEMOD(state, ACLC2S232A, aclc) < 0)
						{
							goto err;
						}
					}
				}
			}
			else
			{
				/* Carrier loop setting for short frame */
				aclc = stv090x_optimize_carloop_short(state);
				if (state->modulation == STV090x_QPSK)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, aclc) < 0)
					{
						goto err;
					}
				}
				else if (state->modulation == STV090x_8PSK)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, ACLC2S28, aclc) < 0)
					{
						goto err;
					}
				}
				else if (state->modulation == STV090x_16APSK)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, ACLC2S216A, aclc) < 0)
					{
						goto err;
					}
				}
				else if (state->modulation == STV090x_32APSK)
				{
					if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, ACLC2S232A, aclc) < 0)
					{
						goto err;
					}
				}
			}
			if (state->device == STX7111)
			{
				if (state->search_mode == STV090x_SEARCH_AUTO)
				{
					if (stv090x_write_reg(state, STV090x_GENCFG, 0x16 /* 0x14 ??? */) < 0)
					{
						goto err;
					}
					reg = stv090x_read_reg(state, STV090x_TSTRES0);
					STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x01); /* ldpc reset */
					if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
					{
						goto err;
					}
					STV090x_SETFIELD(reg, FRESFEC_FIELD, 0);
					if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
					{
						goto err;
					}
				}
				STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x63); /* PER */
				reg = STV090x_READ_DEMOD(state, DEMOD);
				STV090x_SETFIELD_Px(reg, MANUAL_SXROLLOFF_FIELD, 0); /* auto rolloff */
				if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
				{
					goto err;
				}
				STV090x_WRITE_DEMOD(state, GAUSSR0, 0xac);
				STV090x_WRITE_DEMOD(state, CCIR0, 0x2c);
			}
			else
			{
				STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x67); /* PER */
			}
#else  // FS9000
			reg = STV090x_READ_DEMOD(state, DMDMODCOD);
			modcod = STV090x_GETFIELD_Px(reg, DEMOD_MODCOD_FIELD);
			pilots = STV090x_GETFIELD_Px(reg, DEMOD_TYPE_FIELD) & 0x01;
			aclc = stv090x_optimize_carloop(state, modcod, pilots);
			if (modcod <= STV090x_QPSK_910)
			{
				STV090x_WRITE_DEMOD(state, ACLC2S2Q, aclc);
			}
			else if (modcod <= STV090x_8PSK_910)
			{
				if (STV090x_WRITE_DEMOD(state, ACLC2S2Q, 0x2a) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, ACLC2S28, aclc) < 0)
				{
					goto err;
				}
			}
			STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x67); /* PER */
#endif
			break;
		}
		default:
		{
			dprintk(50, "STV090x_UNKNOWN\n");
			reg = STV090x_READ_DEMOD(state, DMDCFGMD);
			STV090x_SETFIELD_Px(reg, DVBS1_ENABLE_FIELD, 1);
			STV090x_SETFIELD_Px(reg, DVBS2_ENABLE_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
			{
				goto err;
			}
			break;
		}
	} //switch end

	f_1 = STV090x_READ_DEMOD(state, CFR2);
	f_0 = STV090x_READ_DEMOD(state, CFR1);
	reg = STV090x_READ_DEMOD(state, TMGOBS);
	rolloff = STV090x_GETFIELD_Px(reg, ROLLOFF_STATUS_FIELD);

	if (state->algo == STV090x_BLIND_SEARCH)
	{
		STV090x_WRITE_DEMOD(state, SFRSTEP, 0x00);
		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, SCAN_ENABLE_FIELD, 0x00);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0xc1) < 0)
		{
			goto err;
		}
		if (stv090x_set_srate(state, srate) < 0)
		{
			goto err;
		}
#if 0
		if (stv090x_set_max_srate(state, state->mclk, srate) < 0)
		{
			goto err;
		}
		if (stv090x_set_min_srate(state, state->mclk, srate) < 0)
		{
			goto err;
		}
#endif
		blind_tune = 1;

		if (stv090x_dvbs_track_crl(state) < 0)
		{
			goto err;
		}
	}

	if (state->dev_ver >= 0x20)
	{
		if ((state->search_mode == STV090x_SEARCH_DVBS1)
		||  (state->search_mode == STV090x_SEARCH_DSS)
		||  (state->search_mode == STV090x_SEARCH_AUTO))
		{
			if (STV090x_WRITE_DEMOD(state, VAVSRVIT, 0x0a) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, VITSCALE, 0x00) < 0)
			{
				goto err;
			}
		}
	}
	if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
	{
		goto err;
	}
// #ifndef FS9000
	/* AUTO tracking MODE */
	if (STV090x_WRITE_DEMOD(state, SFRUP1, 0x80) < 0)
	{
		goto err;
	}
	/* AUTO tracking MODE */
	if (STV090x_WRITE_DEMOD(state, SFRLOW1, 0x80) < 0)
	{
		goto err;
	}
//#endif
	if ((state->dev_ver >= 0x20) || (blind_tune == 1) || (state->srate < 10000000))
	{
		/* update initial carrier freq with the found freq offset */
		dprintk(1, "f_1 0x%x\n", f_1);
		dprintk(1, "f_0 0x%x\n", f_0);
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
		{
			goto err;
		}
		state->tuner_bw = stv090x_car_width(srate, state->rolloff) + 10000000;

		if ((state->dev_ver >= 0x20) || (blind_tune == 1))
		{
			if (state->algo != STV090x_WARM_SEARCH)
			{
				if (state->config->tuner_set_bandwidth)
				{
					if (state->config->tuner_set_bandwidth(fe, state->tuner_bw) < 0)
					{
						goto err;
					}
				}
			}
		}
		if ((state->algo == STV090x_BLIND_SEARCH) || (state->srate < 10000000))
		{
			msleep(2); /* blind search: wait 50(?)ms for SR stabilization */
		}
		else
		{
			msleep(1);
		}
		stv090x_get_lock_tmg(state);

		if (!(stv090x_get_dmdlock(state, (state->DemodTimeout / 2))))
		{
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
			{
				goto err;
			}
			i = 0;
			while ((!(stv090x_get_dmdlock(state, (state->DemodTimeout / 2)))) && (i <= 2))
			{
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1f) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_0) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0)
				{
					goto err;
				}
				i++;
			}
		}
	}
	if (state->dev_ver >= 0x20)
	{
		if (STV090x_WRITE_DEMOD(state, CARFREQ, 0x49) < 0)
		{
			goto err;
		}
	}
	if ((state->delsys == STV090x_DVBS1)
	||  (state->delsys == STV090x_DSS)
	||  state->device == STX7111)
	{
		stv090x_set_vit_thtracq(state);
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_optimize_track: I/O error\n");
	return -1;
}

static int stv090x_get_feclock(struct stv090x_state *state, s32 timeout)
{
	s32 timer = 0, lock = 0, stat;
	u32 reg;

	dprintk(10, "%s >\n", __func__);
	while ((timer < timeout) && (!lock))
	{
		reg = STV090x_READ_DEMOD(state, DMDSTATE);
		stat = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

		dprintk(10, "reg = 0x%x, stat = %d\n", reg, stat);

		switch (stat)
		{
			case 0: /* searching */
			case 1: /* first PLH detected */
			default:
			{
				lock = 0;
				dprintk(20, "%s searching, plh detected or default\n", __func__);
				break;
			}
			case 2: /* DVB-S2 mode */
			{
				reg = STV090x_READ_DEMOD(state, PDELSTATUS1);
				lock = STV090x_GETFIELD_Px(reg, PKTDELIN_LOCK_FIELD);
				dprintk(20, "%s dvb-s2 mode: reg = 0x%x, lock = %d\n", __func__, reg, lock);
				break;
			}
			case 3: /* DVB-S1/legacy mode */
			{
				reg = STV090x_READ_DEMOD(state, VSTATUSVIT);
				lock = STV090x_GETFIELD_Px(reg, LOCKEDVIT_FIELD);
				dprintk(20, "%s dvb-s1 mode: reg = 0x%x, lock = %d\n", __func__, reg, lock);
				break;
			}
		}
		if (!lock)
		{
			msleep(2);
			timer += 2;
		}
	}
	dprintk(10, "%s lock %d<\n", __func__, lock);
	return lock;
}

static int stv090x_get_lock(struct stv090x_state *state, s32 timeout_dmd, s32 timeout_fec)
{
	u32 reg;
	s32 timer = 0;
	int lock;

	dprintk(10, "%s >\n", __func__);

	lock = stv090x_get_dmdlock(state, timeout_dmd);
	if (lock)
	{
		lock = stv090x_get_feclock(state, timeout_fec);
	}

	if (lock)
	{
		lock = 0;

		while ((timer < timeout_fec) && (!lock))
		{
			reg = STV090x_READ_DEMOD(state, TSSTATUS);
			lock = STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD);
			msleep(1);
			timer++;
		}
	}
	dprintk(10, "%s lock %d<\n", __func__, lock);
	return lock;
}

static int stv090x_set_s2rolloff(struct stv090x_state *state)
{
	u32 reg;

	dprintk(10, "%s >\n", __func__);
	if (state->dev_ver <= 0x20)
	{
		/* rolloff to auto mode if DVBS2 */
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, MANUAL_SXROLLOFF_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		{
			goto err;
		}
	}
	else
	{
		/* DVB-S2 rolloff to auto mode if DVBS2 */
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, MANUAL_S2ROLLOFF_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		{
			goto err;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	printk("stv090x_set_s2rolloff: I/O error\n");
	return -1;
}

#if 0
static enum stv090x_signal_state stv090x_acq_fixs1(struct stv090x_state *state)
{
	s32 srate, f_1, f_2;
	enum stv090x_signal_state signal_state = STV090x_NODATA;
	u32 reg;
	int lock;

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	if (STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD) == 3)
	{
		/* DVB-S mode */
		srate  = stv090x_get_srate(state, state->mclk);
		srate += stv090x_get_tmgoffst(state, state->srate);

		if (state->algo == STV090x_BLIND_SEARCH)
		{
			if (stv090x_set_srate(state, state->srate) < 0)
			{
				goto err;
			}
		}
		stv090x_get_lock_tmg(state);

		f_1 = STV090x_READ_DEMOD(state, CFR2);
		f_2 = STV090x_READ_DEMOD(state, CFR1);

		reg = STV090x_READ_DEMOD(state, DMDCFGMD);
		STV090x_SETFIELD_Px(reg, CFR_AUTOSCAN_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DMDCFGMD, reg) < 0)
		{
			goto err;
		}
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, STV090x_IQ_SWAP);
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0) /* stop demod */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_2) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* warm start trigger */
		{
			goto err;
		}
		if (stv090x_get_lock(state, state->DemodTimeout, state->FecTimeout))
		{
			lock = 1;
			stv090x_get_sig_params(state);
			stv090x_optimize_track(state);
		}
		else
		{
			reg = STV090x_READ_DEMOD(state, DEMOD);
			STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, STV090x_IQ_NORMAL);
			if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x1c) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRINIT1, f_1) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, CFRINIT0, f_2) < 0)
			{
				goto err;
			}
			if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x18) < 0) /* warm start trigger */
			{
				goto err;
			}
			if (stv090x_get_lock(state, state->DemodTimeout, state->FecTimeout))
			{
				lock = 1;
				signal_state = stv090x_get_sig_params(state);
				stv090x_optimize_track(state);
			}
		}
	}
	else
	{
		lock = 0;
	}
	return signal_state;

err:
	dprintk(1, "%s: I/O error\n", __func__);
	return -1;
}
#endif

static enum stv090x_signal_state stv090x_algo(struct stv090x_state *state)
{
	struct dvb_frontend *fe = &state->frontend;
	enum stv090x_signal_state signal_state = STV090x_NOCARRIER;
	u32 reg;
	s32 agc1_power, power_iq = 0, i;
	int lock = 0, low_sr = 0, no_signal = 0;

	dprintk(10, "%s >\n", __func__);

	if (state->device != STX7111)
	{
		reg = STV090x_READ_DEMOD(state, TSCFGH);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1);  /* Stop path 1 stream merger */
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		{
			goto err;
		}
	}
	if (STV090x_WRITE_DEMOD(state, DMDISTATE, 0x5c) < 0) /* Demod stop */
	{
		goto err;
	}
	if (state->device == STX7111)
	{
		reg = STV090x_READ_DEMOD(state, PDELCTRL1);
		STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
		{
			goto err;
		}
	}
	if (state->dev_ver >= 0x20)
	{
		if (state->srate > 5000000)
		{
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x9e) < 0)
			{
				goto err;
			}
		}
		else
		{
			if (STV090x_WRITE_DEMOD(state, CORRELABS, 0x82) < 0)
			{
				goto err;
			}
		}
	}
	if (state->device == STX7111)
	{
		reg = STV090x_READ_DEMOD(state, TSCFGH);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1); /* Stop path 1 stream merger */
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		{
			goto err;
		}
	}
	stv090x_get_lock_tmg(state);

	if (state->algo == STV090x_BLIND_SEARCH)
	{
		state->tuner_bw = 2 * 36000000; /* wide bw for unknown srate */
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0xc0) < 0) /* wider srate scan */
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, CORRELMANT, 0x70) < 0)
		{
			goto err;
		}
		if (stv090x_set_srate(state, 1000000) < 0) /* inital srate = 1Msps */
		{
			goto err;
		}
	}
	else
	{
		/* known srate */
		if (STV090x_WRITE_DEMOD(state, DMDTOM, 0x20) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, TMGCFG, 0xd2) < 0)
		{
			goto err;
		}
// #ifndef FS9000
		if (state->device != STX7111)
		{
			if (state->srate < 2000000)
			{
				/* SR < 2MSPS */
				if (STV090x_WRITE_DEMOD(state, CORRELMANT, 0x63) < 0)
				{
					goto err;
				}
			}
			else
			{
				/* SR >= 2Msps */
				if (STV090x_WRITE_DEMOD(state, CORRELMANT, 0x70) < 0)
				{
					goto err;
				}
			}
		}
// #endif
		if (STV090x_WRITE_DEMOD(state, AGC2REF, 0x38) < 0)
		{
			goto err;
		}
		if (state->dev_ver >= 0x20)
		{
			if (STV090x_WRITE_DEMOD(state, KREFTMG, 0x5a) < 0)
			{
				goto err;
			}
			if (state->algo == STV090x_COLD_SEARCH)
			{
				if (state->device != STX7111)
				{
					state->tuner_bw = (15 * (stv090x_car_width(state->srate, state->rolloff) + 10000000)) / 10;
				}
				else
				{
					state->tuner_bw = (15 * (stv090x_car_width(state->srate, state->rolloff))) / 10;
				}
			}
			else if ((state->algo == STV090x_WARM_SEARCH) && (state->device != STX7111))
			{
				state->tuner_bw = stv090x_car_width(state->srate, state->rolloff) + 10000000;
			}
			else if (state->algo == STV090x_WARM_SEARCH)
			{
				state->tuner_bw = stv090x_car_width(state->srate, state->rolloff);
			}
		}
		/* if cold start or warm  (Symbolrate is known)
		 * use a Narrow symbol rate scan range
		 */
		if (STV090x_WRITE_DEMOD(state, TMGCFG2, 0x01) < 0) /* narrow srate scan */
		{
			goto err;
		}
		if (stv090x_set_srate(state, state->srate) < 0)
		{
			goto err;
		}
		if (stv090x_set_max_srate(state, state->mclk, state->srate) < 0)
		{
			goto err;
		}
		if (stv090x_set_min_srate(state, state->mclk, state->srate) < 0)
		{
			goto err;
		}
		if (state->srate >= 10000000)
		{
			low_sr = 0;
		}
		else
		{
			low_sr = 1;
		}
	}
// #ifndef FS9000
	if (state->config->tuner_set_bbgain)
	{
		reg = state->config->tuner_bbgain;
		if (reg == 0)
		{
			reg = 10; /* default: 10dB */
		}
		if (bbgain != -1) /* module param set by user ? */
		{
			reg = bbgain;
		}
		if (state->config->tuner_set_bbgain(fe, reg) < 0)
		{
			goto err;
		}
	}
// #endif
	if (state->config->tuner_set_frequency)
	{
		if (state->config->tuner_set_frequency(fe, state->frequency) < 0)
		{
			goto err;
		}
	}
	if (state->config->tuner_set_bandwidth)
	{
		if (state->config->tuner_set_bandwidth(fe, state->tuner_bw) < 0)
		{
			goto err;
		}
	}
	if (state->config->tuner_get_status)
	{
		if (state->config->tuner_get_status(fe, &reg) < 0)
		{
			goto err;
		}
	}
	if (reg)
	{
		dprintk(10, "1. Tuner phase locked\n");
	}
	else
	{
		dprintk(10, "1. Tuner unlocked\n");
	}
	agc1_power = MAKEWORD16(STV090x_READ_DEMOD(state, AGCIQIN1), STV090x_READ_DEMOD(state, AGCIQIN0));
	dprintk(50, "agc1_power = %d\n", agc1_power);

	if (agc1_power == 0)
	{
		/* If AGC1 integrator value is 0
		 * then read POWERI, POWERQ
		 */
		for (i = 0; i < 5; i++)
		{
			power_iq += (STV090x_READ_DEMOD(state, POWERI) + STV090x_READ_DEMOD(state, POWERQ)) >> 1;
		}
		power_iq /= 5;
	}
	if ((agc1_power == 0) && (power_iq < STV090x_IQPOWER_THRESHOLD))
	{
		dprintk(50, "No Signal: POWER_IQ=0x%02x\n", power_iq);
		lock = 0;
		signal_state = STV090x_NOAGC1;
	}
	else
	{
		reg = STV090x_READ_DEMOD(state, DEMOD);
		STV090x_SETFIELD_Px(reg, SPECINV_CONTROL_FIELD, state->inversion);

		if (state->dev_ver <= 0x20)
		{
			/* rolloff to auto mode if DVBS2 */
			STV090x_SETFIELD_Px(reg, MANUAL_SXROLLOFF_FIELD, 1);
		}
		else
		{
			/* DVB-S2 rolloff to auto mode if DVBS2 */
			STV090x_SETFIELD_Px(reg, MANUAL_S2ROLLOFF_FIELD, 1);
		}
		if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
		{
			goto err;
		}
		if (stv090x_delivery_search(state) < 0)
		{
			goto err;
		}
		if (state->algo != STV090x_BLIND_SEARCH)
		{
			if (stv090x_start_search(state) < 0)
			{
				goto err;
			}
		}
		if (state->device == STX7111)
		{
			reg = STV090x_READ_DEMOD(state, PDELCTRL1);
			STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			{
				goto err;
			}
#warning fixme 0xf3d0 ??? !!!
			if (stv090x_write_reg(state, 0xf5d0, 0x8) < 0)
			{
				goto err;
			}
			if (stv090x_write_reg(state, 0xf5d0, 0x0) < 0)
			{
				goto err;
			}
		}
	}
	if (signal_state == STV090x_NOAGC1)
	{
		return signal_state;
	}

	if (state->device == STX7111)
	{
		/* release merger reset */
		reg = STV090x_READ_DEMOD(state, TSCFGH);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		{
			goto err;
		}
		msleep(1);
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x01);
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		{
			goto err;
		}
		STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
		if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
		{
			goto err;
		}
	}
	/* need to check for AGC1 state */
	if (state->algo == STV090x_BLIND_SEARCH)
	{
		lock = stv090x_blind_search(state);
	}
	else if (state->algo == STV090x_COLD_SEARCH)
	{
		lock = stv090x_get_coldlock(state, state->DemodTimeout);
		dprintk(10, "cold_search ->lock = %d\n", lock);
	}
	else if (state->algo == STV090x_WARM_SEARCH)
	{
		lock = stv090x_get_dmdlock(state, state->DemodTimeout);
	}
	if ((!lock) && (state->algo == STV090x_COLD_SEARCH))
	{
		if (!low_sr)
		{
			if (stv090x_chk_tmg(state))
			{
				lock = stv090x_sw_algo(state);
			}
			dprintk(10, "->lock = %d\n", lock);
		}
	}
	if (lock)
	{
		signal_state = stv090x_get_sig_params(state);
	}
	if ((lock) && (signal_state == STV090x_RANGEOK))
	{
		/* signal within Range */
		dprintk(10, "lock && rangeok\n");

		stv090x_optimize_track(state);

		if (state->dev_ver >= 0x20)
		{
#if 0  // #ifdef FS9000
			reg = stv090x_read_reg(state, STV090x_TSTRES0);
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x1);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			{
				goto err;
			}
			reg = STV090x_READ_DEMOD(state, PDELCTRL1);
			STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x01);
			if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			{
				goto err;
			}
#endif
			/* >= Cut 2.0 :release TS reset after
			 * demod lock and optimized Tracking
			 */
			reg = STV090x_READ_DEMOD(state, TSCFGH);
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0);  /* release merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			{
				goto err;
			}
			msleep(1);
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 1);  /* merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			{
				goto err;
			}
#if 0  // #ifdef FS9000
			reg = stv090x_read_reg(state, STV090x_TSTRES0);
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			{
				goto err;
			}
			reg = STV090x_READ_DEMOD(state, PDELCTRL1);
			STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x00);
			if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			{
				goto err;
			}
			reg = STV090x_READ_DEMOD(state, TSCFGH);
#endif
			STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0); /* release merger reset */
			if (STV090x_WRITE_DEMOD(state, TSCFGH, reg) < 0)
			{
				goto err;
			}
		}
		lock = stv090x_get_lock(state, state->FecTimeout, state->FecTimeout);
		dprintk(10, "get_lock ->lock\n");
		if (lock)
		{
			if (state->delsys == STV090x_DVBS2)
			{
				if (state->device != STX7111)
				{
					stv090x_set_s2rolloff(state);
				}
				reg = STV090x_READ_DEMOD(state, PDELCTRL2);
				STV090x_SETFIELD_Px(reg, RESET_UPKO_COUNT, 1);
				if (STV090x_WRITE_DEMOD(state, PDELCTRL2, reg) < 0)
				{
					goto err;
				}
				/* Reset DVBS2 packet delinator error counter */
				reg = STV090x_READ_DEMOD(state, PDELCTRL2);
				STV090x_SETFIELD_Px(reg, RESET_UPKO_COUNT, 0);
				if (STV090x_WRITE_DEMOD(state, PDELCTRL2, reg) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x67) < 0) /* PER */
				{
					goto err;
				}
			}
			else
			{
				if (state->device != STX7111)
				{
					if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x75) < 0)
					{
						goto err;
					}
				}
				else
				{
					if (STV090x_WRITE_DEMOD(state, ERRCTRL1, 0x73) < 0)
					{
						goto err;
					}
					if (STV090x_WRITE_DEMOD(state, U1, 0x73) < 0)
					{
						goto err;
					}
				}
			}
			/* Reset the Total packet counter */
			if (STV090x_WRITE_DEMOD(state, FBERCPT4, 0x00) < 0)
			{
				goto err;
			}
			/* Reset the packet Error counter2 */
			if (STV090x_WRITE_DEMOD(state, ERRCTRL2, 0xc1) < 0)  // bylo 0xc1 jest 0x12
			{
				goto err;
			}
		}
		else
		{
			dprintk(10, "get_lock ->no lock\n");
			lock = 0;
			signal_state = STV090x_NODATA;
			no_signal = stv090x_chk_signal(state);
			dprintk(10, "no_signal = %d\n", no_signal);
		}
	}
	dprintk(10, "%s signal_state %d<\n", __func__, signal_state);
	return signal_state;

err:
	printk("stv090x_algo: I/O error\n");
	return -1;
}

#if DVB_API_VERSION < 5
static enum dvbfe_search stv090x_search(struct dvb_frontend *fe, struct dvbfe_params *p)
{
	struct stv090x_state *state = fe->demodulator_priv;
	enum stv090x_signal_state algo_state;

	dprintk(10, "%s: freq %d, symbol %d, inversion %d, rolloff %d, modulation %d, fec %d, delsys %d\n", __func__, 
		p->frequency, p->delsys.dvbs.symbol_rate, p->inversion, p->delsys.dvbs.rolloff,
		p->delsys.dvbs.modulation, p->delsys.dvbs.fec,
		p->delivery);

	if ((p->frequency == 0)
	&&  (p->delsys.dvbs.symbol_rate == 0)
	&&  (p->inversion == 0)
	&&  (p->delsys.dvbs.rolloff == 0)
	&&  (p->delsys.dvbs.modulation == 0)
	&&  (p->delsys.dvbs.fec == 0)
	&&  (p->delivery == 0))
	{
		dprintk(1, "%s exit: -EINVAL\n", __func__);
		return DVBFE_ALGO_SEARCH_FAILED;
	}
	if (p->delivery == DVBFE_DELSYS_DVBS2)
	{
		state->delsys = STV090x_DVBS2;
	}
	else if (p->delivery == DVBFE_DELSYS_DVBS)
	{
		state->delsys = STV090x_DVBS1;
	}
	else if (p->delivery == DVBFE_DELSYS_DSS)
	{
		state->delsys = STV090x_DSS;
	}
	else
	{
		state->delsys = STV090x_ERROR;
	}
	state->frequency = p->frequency;
	state->srate = p->delsys.dvbs.symbol_rate;
	state->algo = STV090x_COLD_SEARCH;
	state->search_mode = STV090x_SEARCH_AUTO;
	state->fec = STV090x_PRERR;

	if (state->srate > 10000000)
	{
		dprintk(1, "Search range: 10 MHz\n");
		state->search_range = 10000000;
	}
	else
	{
		dprintk(1, "Search range: 5 MHz\n");
		state->search_range = 5000000;
	}
	algo_state = stv090x_algo(state);

	if (algo_state == STV090x_RANGEOK)
	{
		dprintk(1, "Search success!\n");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	}
	else
	{
		dprintk(1, "Search failed! %d\n", algo_state);
		return DVBFE_ALGO_SEARCH_FAILED;
	}
	return DVBFE_ALGO_SEARCH_ERROR;
}
#else // DVB_API_VERSION >= 5
static enum dvbfe_search stv090x_search(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct stv090x_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *props = &fe->dtv_property_cache;

	if (p->frequency == 0)
	{
		return DVBFE_ALGO_SEARCH_INVALID;
	}
	state->delsys = props->delivery_system;
	state->frequency = p->frequency;
	state->srate = p->u.qpsk.symbol_rate;
	state->search_mode = STV090x_SEARCH_AUTO;
	state->algo = STV090x_COLD_SEARCH;
	state->fec = STV090x_PRERR;
	if (state->srate > 10000000)
	{
		dprintk(10, "Search range: 10 MHz\n");
		state->search_range = 10000000;
	}
	else
	{
		dprintk(10, "Search range: 5 MHz\n");
		state->search_range = 5000000;
	}

	if (stv090x_algo(state) == STV090x_RANGEOK)
	{
		dprintk(1, "Search success!\n");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	}
	else
	{
		dprintk(1, "Search failed!\n");
		return DVBFE_ALGO_SEARCH_FAILED;
	}
	return DVBFE_ALGO_SEARCH_ERROR;
}
#endif

static int stv090x_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;
	u8 search_state;

	dprintk(10, "%s >\n", __func__);

	reg = STV090x_READ_DEMOD(state, DMDSTATE);
	search_state = STV090x_GETFIELD_Px(reg, HEADER_MODE_FIELD);

	switch (search_state)
	{
		case 0: /* searching */
		case 1: /* first PLH detected */
		default:
		{
			dprintk(50, "Status: Unlocked (Searching...)\n");
			*status = 0;
			break;
		}
		case 2: /* DVB-S2 mode */
		{
			dprintk(50, "Delivery system: DVB-S2\n");
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			if (STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD))
			{
				reg = STV090x_READ_DEMOD(state, PDELSTATUS1);
				if (STV090x_GETFIELD_Px(reg, PKTDELIN_LOCK_FIELD))
				{
					reg = STV090x_READ_DEMOD(state, TSSTATUS);
					if (STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD))
					{
						*status = FE_HAS_SIGNAL
						        | FE_HAS_CARRIER
						        | FE_HAS_VITERBI
						        | FE_HAS_SYNC
						        | FE_HAS_LOCK;
					}
				}
			}
			break;
		}
		case 3: /* DVB-S1/legacy mode */
		{
			dprintk(50, "Delivery system: DVB-S\n");
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			if (STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD))
			{
				reg = STV090x_READ_DEMOD(state, VSTATUSVIT);
				if (STV090x_GETFIELD_Px(reg, LOCKEDVIT_FIELD))
				{
					reg = STV090x_READ_DEMOD(state, TSSTATUS);
					if (STV090x_GETFIELD_Px(reg, TSFIFO_LINEOK_FIELD))
					{
						*status = FE_HAS_SIGNAL
						        | FE_HAS_CARRIER
						        | FE_HAS_VITERBI
						        | FE_HAS_SYNC
						        | FE_HAS_LOCK;
					}
				}
			}
			break;
		}
	}
	dprintk(10, "%s status = %d<\n", __func__, *status);
	return 0;
}

static int stv090x_read_per(struct dvb_frontend *fe, u32 *per)
{
	struct stv090x_state *state = fe->demodulator_priv;

	s32 count_4, count_3, count_2, count_1, count_0, count;
	u32 reg, h, m, l;
	enum fe_status status;

	dprintk(10, "%s >\n", __func__);

	stv090x_read_status(fe, &status);
	if (!(status & FE_HAS_LOCK))
	{
		*per = 1 << 23; /* Max PER */
	}
	else
	{
		/* Counter 2 */
		reg = STV090x_READ_DEMOD(state, ERRCNT22);
		h = STV090x_GETFIELD_Px(reg, ERR_CNT2_FIELD);

		reg = STV090x_READ_DEMOD(state, ERRCNT21);
		m = STV090x_GETFIELD_Px(reg, ERR_CNT21_FIELD);

		reg = STV090x_READ_DEMOD(state, ERRCNT20);
		l = STV090x_GETFIELD_Px(reg, ERR_CNT20_FIELD);

		*per = ((h << 16) | (m << 8) | l);

		printk("h:%d m:%d l:%d per:%d\n", h, m, l, *per);

		count_4 = STV090x_READ_DEMOD(state, FBERCPT4);
		count_3 = STV090x_READ_DEMOD(state, FBERCPT3);
		count_2 = STV090x_READ_DEMOD(state, FBERCPT2);
		count_1 = STV090x_READ_DEMOD(state, FBERCPT1);
		count_0 = STV090x_READ_DEMOD(state, FBERCPT0);

		if ((!count_4) && (!count_3))
		{
			count  = (count_2 & 0xff) << 16;
			count |= (count_1 & 0xff) <<  8;
			count |=  count_0 & 0xff;
		}
		else
		{
			count = 1 << 24;
		}
		if (count == 0)
		{
			*per = 1;
		}
	}
	if (STV090x_WRITE_DEMOD(state, FBERCPT4, 0) < 0)
	{
		goto err;
	}
	if (STV090x_WRITE_DEMOD(state, ERRCTRL2, 0xc1) < 0)
	{
		goto err;
	}
	dprintk(10, "%s per = %d<\n", __func__, *per);
	return 0;

err:
	dprintk(1, "stv090x_read_per: I/O error\n");
	return -1;
}

/* powarman's vdr version, because orig is buggy */
static int stv090x_table_lookup(const struct stv090x_tab *tab, int max, int val)
{
	int res = 0;
	int min = 0, med;

	if ((val >= tab[min].read && val < tab[max].read)
	||  (val >= tab[max].read && val < tab[min].read))
	{
		while ((max - min) > 1)
		{
			med = (max + min) / 2;
			if ((val >= tab[min].read && val < tab[med].read)
			||  (val >= tab[med].read && val < tab[min].read))
			{
				max = med;
			}
			else
			{
				min = med;
			}
		}
		res = ((val - tab[min].read) * (tab[max].real - tab[min].real) / (tab[max].read - tab[min].read)) + tab[min].real;
	}
	else
	{
		if (tab[min].read < tab[max].read)
		{
			if (val < tab[min].read)
			{
				res = tab[min].real;
			}
			else if (val >= tab[max].read)
			{
				res = tab[max].real;
			}
		}
		else if (val >= tab[min].read)
		{
			res = tab[min].real;
		}
		else if (val < tab[max].read)
		{
			res = tab[max].real;
		}
	}
	return res;
}

static int stv090x_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;
	s32 agc_0, agc_1, agc;
	s32 str;

	dprintk(10, "%s >\n", __func__);

	reg = STV090x_READ_DEMOD(state, AGCIQIN1);
	agc_1 = STV090x_GETFIELD_Px(reg, AGCIQ_VALUE_FIELD);
	reg = STV090x_READ_DEMOD(state, AGCIQIN0);
	agc_0 = STV090x_GETFIELD_Px(reg, AGCIQ_VALUE_FIELD);
	agc = MAKEWORD16(agc_1, agc_0);

	dprintk(50, "agc = 0x%04x\n", agc);

	str = stv090x_table_lookup(stv090x_rf_tab, ARRAY_SIZE(stv090x_rf_tab) - 1, agc);
	if (agc > stv090x_rf_tab[0].read)
	{
		str = 0;
	}
	else if (agc < stv090x_rf_tab[ARRAY_SIZE(stv090x_rf_tab) - 1].read)
	{
		str = -100;
	}
	*strength = (str + 100) * 0xFFFF / 100;
	dprintk(10, "%s strength %d <\n", __func__, *strength);
	return 0;
}

static int stv090x_read_cnr(struct dvb_frontend *fe, u16 *cnr)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg_0, reg_1, reg, i;
	s32 val_0, val_1, val = 0;
	u8 lock_f;
	s32 div;
	u32 last;

	*cnr = 0;

	switch (state->delsys)
	{
		case STV090x_DVBS2:
		{
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			lock_f = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
			if (lock_f)
			{
				msleep(1);
				for (i = 0; i < 6; i++)
				{
					reg_1 = STV090x_READ_DEMOD(state, NNOSPLHT1);
					val_1 = STV090x_GETFIELD_Px(reg_1, NOSPLHT_NORMED_FIELD);
					reg_0 = STV090x_READ_DEMOD(state, NNOSPLHT0);
					val_0 = STV090x_GETFIELD_Px(reg_0, NOSPLHT_NORMED_FIELD);
					val  += MAKEWORD16(val_1, val_0);
					msleep(1);
				}
				val /= 16;
				last = ARRAY_SIZE(stv090x_s2cn_tab) - 1;
				div = stv090x_s2cn_tab[0].read - stv090x_s2cn_tab[last].read;
				*cnr = 0xFFFF - ((val * 0xFFFF) / div);
			}
			break;
		}
		case STV090x_DVBS1:
		case STV090x_DSS:
		{
			reg = STV090x_READ_DEMOD(state, DSTATUS);
			lock_f = STV090x_GETFIELD_Px(reg, LOCK_DEFINITIF_FIELD);
			if (lock_f)
			{
				msleep(1);
				for (i = 0; i < 6; i++)
				{
					reg_1 = STV090x_READ_DEMOD(state, NOSDATAT1);
					val_1 = STV090x_GETFIELD_Px(reg_1, NOSDATAT_UNNORMED_FIELD);
					reg_0 = STV090x_READ_DEMOD(state, NOSDATAT0);
					val_0 = STV090x_GETFIELD_Px(reg_0, NOSDATAT_UNNORMED_FIELD);
					val  += MAKEWORD16(val_1, val_0);
					msleep(1);
				}
				val /= 16;
				last = ARRAY_SIZE(stv090x_s1cn_tab) - 1;
				div = stv090x_s1cn_tab[0].read - stv090x_s1cn_tab[last].read;
				*cnr = 0xFFFF - ((val * 0xFFFF) / div);
			}
			break;
		}
		default:
		{
			break;
		}
	}
	dprintk(10, "%s cnr %d <\n", __func__, *cnr);
	return 0;
}

static int stv090x_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	dprintk(10, "%s >\n", __func__);

	reg = STV090x_READ_DEMOD(state, DISTXCTL);
	switch (tone)
	{
		case SEC_TONE_ON:
		{
#if defined(ADB_2850)
			if (mp8125_extm != NULL)
			{
				stpio_set_pin (mp8125_extm, 1);
			}
#endif
			STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, 0);
			STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			{
				goto err;
			}
			STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 0);
			if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case SEC_TONE_OFF:
		{
#if defined(ADB_2850)
			if (mp8125_extm != NULL)
			{
				stpio_set_pin (mp8125_extm, 0);
			}
#endif
			STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, 0);
			STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
			if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
			{
				goto err;
			}
			break;
		}
		default:
		{
			return -EINVAL;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_set_tone: I/O error\n");
	return -1;
}

static enum dvbfe_algo stv090x_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static int stv090x_send_diseqc_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg, idle = 0, fifo_full = 1;
	int i;

	dprintk(10, "%s >\n", __func__);

	if (box_type == MODEL_2849)
	{
		pwm_diseqc1_send_msg (fe,cmd);
	}
	else
	{
		reg = STV090x_READ_DEMOD(state, DISTXCTL);
	
		STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, (state->config->diseqc_envelope_mode) ? 4 : 2);
	
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		for (i = 0; i < cmd->msg_len; i++)
		{
			while (fifo_full)
			{
				reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
				fifo_full = STV090x_GETFIELD_Px(reg, FIFO_FULL_FIELD);
			}
			if (STV090x_WRITE_DEMOD(state, DISTXDATA, cmd->msg[i]) < 0)
			{
				goto err;
			}
		}
		reg = STV090x_READ_DEMOD(state, DISTXCTL);
		STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		i = 0;
	
		while ((!idle) && (i < 4))
		{
			reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
			idle = STV090x_GETFIELD_Px(reg, TX_IDLE_FIELD);
			msleep(2);
			i++;
		}
	}  // else
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_send_diseqc_msg: I/O error\n");
	return -1;
}

static int stv090x_send_diseqc_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t burst)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg, idle = 0, fifo_full = 1;
	u8 mode, value;
	int i;

	dprintk(10, "%s >\n", __func__);

	if (box_type == MODEL_2849)
	{
		pwm_send_diseqc1_burst(fe,burst);
	}
	else
	{
		reg = STV090x_READ_DEMOD(state, DISTXCTL);

		if (burst == SEC_MINI_A)
		{
			mode = (state->config->diseqc_envelope_mode) ? 5 : 3;
			value = 0x00;
		}
		else
		{
			mode = (state->config->diseqc_envelope_mode) ? 4 : 2;
			value = 0xFF;
		}

		STV090x_SETFIELD_Px(reg, DISTX_MODE_FIELD, mode);
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		STV090x_SETFIELD_Px(reg, DISEQC_RESET_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 1);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		while (fifo_full)
		{
		reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
		fifo_full = STV090x_GETFIELD_Px(reg, FIFO_FULL_FIELD);
		}

		if (STV090x_WRITE_DEMOD(state, DISTXDATA, value) < 0)
		{
			goto err;
		}
		reg = STV090x_READ_DEMOD(state, DISTXCTL);
		STV090x_SETFIELD_Px(reg, DIS_PRECHARGE_FIELD, 0);
		if (STV090x_WRITE_DEMOD(state, DISTXCTL, reg) < 0)
		{
			goto err;
		}
		i = 0;

		while ((!idle) && (i < 4))
		{
			reg = STV090x_READ_DEMOD(state, DISTXSTATUS);
			idle = STV090x_GETFIELD_Px(reg, TX_IDLE_FIELD);
			msleep(2);
			i++;
		}
	}  // else
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_send_diseqc_burst: I/O error\n");
	return -1;
}

static int stv090x_recv_slave_reply(struct dvb_frontend *fe, struct dvb_diseqc_slave_reply *reply)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg = 0, i = 0, rx_end = 0;

	dprintk(10, "%s >\n", __func__);

	while ((rx_end != 1) && (i < 4))
	{
		msleep(2);
		i++;
		reg = STV090x_READ_DEMOD(state, DISRX_ST0);
		rx_end = STV090x_GETFIELD_Px(reg, RX_END_FIELD);
	}
	if (rx_end)
	{
		reply->msg_len = STV090x_GETFIELD_Px(reg, FIFO_BYTENBR_FIELD);
		for (i = 0; i < reply->msg_len; i++)
		{
			reply->msg[i] = STV090x_READ_DEMOD(state, DISRXDATA);
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;
}

static int stv090x_sleep(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	if (state->config->tuner_sleep)
	{
		if (state->config->tuner_sleep(fe) < 0)
		{
			goto err;
		}
	}
	dprintk(10, "Set %s to sleep\n", state->device == STV0900 ? "STV0900" : "STV0903");

	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	STV090x_SETFIELD(reg, STANDBY_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, reg) < 0)
	{
		goto err;
	}
	reg = stv090x_read_reg(state, STV090x_TSTTNR1);
	STV090x_SETFIELD(reg, ADC1_PON_FIELD, 0);
	if (stv090x_write_reg(state, STV090x_TSTTNR1, reg) < 0)
	{
		goto err;
	}
	return 0;

err:
	dprintk(1, "stv090x_sleep: I/O error\n");
	return -1;
}

static int stv090x_wakeup(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	u32 reg;

	dprintk(10, "Wake %s from standby\n", state->device == STV0900 ? "STV0900" : "STV0903");

	reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
	STV090x_SETFIELD(reg, STANDBY_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, reg) < 0)
	{
		goto err;
	}
	reg = stv090x_read_reg(state, STV090x_TSTTNR1);
	STV090x_SETFIELD(reg, ADC1_PON_FIELD, 1);
	if (stv090x_write_reg(state, STV090x_TSTTNR1, reg) < 0)
	{
		goto err;
	}
	return 0;

err:
	dprintk(1, "stv090x_wakeup: I/O error\n");
	return -1;
}

static void stv090x_release(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;

	kfree(state);
}

static int stv090x_ldpc_mode(struct stv090x_state *state, enum stv090x_mode ldpc_mode)
{
	u32 reg = 0;

	dprintk(10, "%s >\n", __func__);

	reg = stv090x_read_reg(state, STV090x_GENCFG);

	switch (ldpc_mode)
	{
		case STV090x_DUAL:
		default:
		{
			if ((state->demod_mode != STV090x_DUAL) || (STV090x_GETFIELD(reg, DDEMOD_FIELD) != 1))
			{
				/* set LDPC to dual mode */
				if (stv090x_write_reg(state, STV090x_GENCFG, 0x1d) < 0)
				{
					goto err;
				}
				dprintk(10, "setting to dual mode\n");

				state->demod_mode = STV090x_DUAL;

				reg = stv090x_read_reg(state, STV090x_TSTRES0);
				STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x1);
				if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				{
					goto err;
				}
				STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
				if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST0, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST1, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST2, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST3, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST4, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST5, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST6, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST7, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST8, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLST9, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTA, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTB, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTC, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTD, 0xcc) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTE, 0xff) < 0)
				{
					goto err;
				}
				if (STV090x_WRITE_DEMOD(state, MODCODLSTF, 0xcf) < 0)
				{
					goto err;
				}
			}
			break;
		}
		case STV090x_SINGLE:
		{
			dprintk(10, "setting to single mode\n");
			if (stv090x_stop_modcod(state) < 0)
			{
				goto err;
			}
			if (stv090x_activate_modcod_single(state) < 0)
			{
				goto err;
			}

			if (state->demod == STV090x_DEMODULATOR_1)
			{
				if (stv090x_write_reg(state, STV090x_GENCFG, 0x06) < 0) /* path 2 */
				{
					goto err;
				}
			}
			else
			{
				if (stv090x_write_reg(state, STV090x_GENCFG, 0x04) < 0) /* path 1 */
				{
					goto err;
				}
			}
			reg = stv090x_read_reg(state, STV090x_TSTRES0);
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x1);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			{
				goto err;
			}
			STV090x_SETFIELD(reg, FRESFEC_FIELD, 0x0);
			if (stv090x_write_reg(state, STV090x_TSTRES0, reg) < 0)
			{
				goto err;
			}

			reg = STV090x_READ_DEMOD(state, PDELCTRL1);
			STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x01);
			if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			{
				goto err;
			}
			STV090x_SETFIELD_Px(reg, ALGOSWRST_FIELD, 0x00);
			if (STV090x_WRITE_DEMOD(state, PDELCTRL1, reg) < 0)
			{
				goto err;
			}
			break;
		}
	}  // end switch
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_ldpc_mode: I/O error\n");
	return -1;
}

/* return (Hz), clk in Hz*/
static u32 stv090x_get_mclk(struct stv090x_state *state)
{
	const struct stv090x_config *config = state->config;

	if (state->device == STX7111)
	{
		u32 n_div = 0;
		u32 m_div = 0;
		u32 mclk = 0;
		u32 reg;

		reg = stv090x_read_reg(state, STV090x_NCOARSE);
		m_div = STV090x_GETFIELD(reg, M_DIV_FIELD);

		reg = stv090x_read_reg(state, STV090x_NCOARSE1);
		n_div = STV090x_GETFIELD(reg, N_DIV_FIELD);

		dprintk(10, "n_div = %d, m_div =%d\n", n_div, m_div);

		if (m_div == 0)
		{
			m_div = 1;
		}
		if (n_div == 0)
		{
			n_div = 1;
		}
		mclk = n_div * (config->xtal / 100);
		mclk /= (m_div * 2);
		mclk = mclk * 100;
		return mclk;
	}
	else
	{
		u32 div, reg;
		u8 ratio;

		dprintk(10, "%s >\n", __func__);

		div = stv090x_read_reg(state, STV090x_NCOARSE);
		reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
		ratio = STV090x_GETFIELD(reg, SELX1RATIO_FIELD) ? 4 : 6;

		dprintk(10, "%s <(div = %d, ratio %d, xtal %d)\n", __func__, div, ratio, config->xtal);
		return (div + 1) * (config->xtal / ratio); /* kHz */
	}
}

static int stv090x_set_mclk(struct stv090x_state *state, u32 mclk, u32 clk)
{
	const struct stv090x_config *config = state->config;
	u32 reg, div, clk_sel;

	dprintk(10, "%s >\n", __func__);

	if (state->device == STX7111)
	{
		reg = stv090x_read_reg(state, STV090x_NCOARSE);
		STV090x_SETFIELD(reg, M_DIV_FIELD, 0x06);
		if (stv090x_write_reg(state, STV090x_NCOARSE, reg) < 0)
		{
			goto err;
		}
		msleep(2);

		reg = stv090x_read_reg(state, STV090x_NCOARSE1);
		STV090x_SETFIELD(reg, N_DIV_FIELD, 0x37);
		if (stv090x_write_reg(state, STV090x_NCOARSE1, reg) < 0)
		{
			goto err;
		}
		state->mclk = stv090x_get_mclk(state);

		dprintk(10, "%s: reading the masterclock = %d\n", __func__, state->mclk);
	}
	else
	{
		reg = stv090x_read_reg(state, STV090x_SYNTCTRL);
		clk_sel = ((STV090x_GETFIELD(reg, SELX1RATIO_FIELD) == 1) ? 4 : 6);

		div = ((clk_sel * mclk) / config->xtal) - 1;

		reg = stv090x_read_reg(state, STV090x_NCOARSE);
		STV090x_SETFIELD(reg, M_DIV_FIELD, div);
		if (stv090x_write_reg(state, STV090x_NCOARSE, reg) < 0)
		{
			goto err;
		}
		state->mclk = stv090x_get_mclk(state);

		dprintk(10, "%s: reading the masterclock = %d\n", __func__, state->mclk);

		/* Set the DiSEqC frequency to 22KHz */

		div = state->mclk / 704000;

		dprintk(10, "%d 0x%02x\n", div, div);

		if (STV090x_WRITE_DEMOD(state, F22TX, div) < 0)
		{
			goto err;
		}
		if (STV090x_WRITE_DEMOD(state, F22RX, div) < 0)
		{
			goto err;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_set_mclk: I/O error\n");
	return -1;
}

static int stv090x_set_tspath(struct stv090x_state *state)
{
	u32 reg;

	dprintk(10, "%s >\n", __func__);

	dprintk(20, "\tts path1 %d\n", state->config->ts1_mode);
	dprintk(20, "\tts path2 %d\n", state->config->ts2_mode);

	if (state->dev_ver >= 0x20)
	{
		switch (state->config->ts1_mode)
		{
			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
			{
				switch (state->config->ts2_mode)
				{
					case STV090x_TSMODE_SERIAL_PUNCTURED:
					case STV090x_TSMODE_SERIAL_CONTINUOUS:
					default:
					{
						stv090x_write_reg(state, STV090x_TSGENERAL, 0x00);
						break;
					}
					case STV090x_TSMODE_PARALLEL_PUNCTURED:
					case STV090x_TSMODE_DVBCI:
					{
						if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x06) < 0) /* Mux'd stream mode */
						{
							goto err;
						}
						reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
						STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
						if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
						{
							goto err;
						}
						reg = stv090x_read_reg(state, STV090x_P2_TSCFGM);
						STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
						if (stv090x_write_reg(state, STV090x_P2_TSCFGM, reg) < 0)
						{
							goto err;
						}
						if (stv090x_write_reg(state, STV090x_P1_TSSPEED, 0x14) < 0)
						{
							goto err;
						}
						if (stv090x_write_reg(state, STV090x_P2_TSSPEED, 0x28) < 0)
						{
							goto err;
						}
						break;
					}
				}  // end of switch
				break;
			}
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
			{
				switch (state->config->ts2_mode)
				{
					case STV090x_TSMODE_SERIAL_PUNCTURED:
					case STV090x_TSMODE_SERIAL_CONTINUOUS:
					default:
					{
						if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0c) < 0)
						{
							goto err;
						}
						break;
					}
					case STV090x_TSMODE_PARALLEL_PUNCTURED:
					case STV090x_TSMODE_DVBCI:
					{
						if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0a) < 0)
						{
							goto err;
						}
						break;
					}
				}
				break;
			}
		}
	}
	else
	{
		switch (state->config->ts1_mode)
		{
			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
			{
				switch (state->config->ts2_mode)
				{
					case STV090x_TSMODE_SERIAL_PUNCTURED:
					case STV090x_TSMODE_SERIAL_CONTINUOUS:
					default:
					{
						stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x10);
						break;
					}
					case STV090x_TSMODE_PARALLEL_PUNCTURED:
					case STV090x_TSMODE_DVBCI:
					{
						stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x16);
						reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
						STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
						if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
						{
							goto err;
						}
						reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
						STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 0);
						if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
						{
							goto err;
						}
						if (stv090x_write_reg(state, STV090x_P1_TSSPEED, 0x14) < 0)
						{
							goto err;
						}
						if (stv090x_write_reg(state, STV090x_P2_TSSPEED, 0x28) < 0)
						{
							goto err;
						}
						break;
					}
				}
				break;
			}
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			default:
			{
				switch (state->config->ts2_mode)
				{
					case STV090x_TSMODE_SERIAL_PUNCTURED:
					case STV090x_TSMODE_SERIAL_CONTINUOUS:
					default:
					{
						stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x14);
						break;
					}
					case STV090x_TSMODE_PARALLEL_PUNCTURED:
					case STV090x_TSMODE_DVBCI:
					{
						stv090x_write_reg(state, STV090x_TSGENERAL1X, 0x12);
						break;
					}
				}
				break;
			}
		}
	}
	switch (state->config->ts1_mode)
	{
		case STV090x_TSMODE_PARALLEL_PUNCTURED:
		{
			reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
			if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_DVBCI:
		{
			reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
			if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_SERIAL_PUNCTURED:
		{
			reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
			if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_SERIAL_CONTINUOUS:
		{
			reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
			if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		default:
		{
			break;
		}
	}
	switch (state->config->ts2_mode)
	{
		case STV090x_TSMODE_PARALLEL_PUNCTURED:
		{
			reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
			if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_DVBCI:
		{
			reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x00);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
			if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_SERIAL_PUNCTURED:
		{
			reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x00);
			if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		case STV090x_TSMODE_SERIAL_CONTINUOUS:
		{
			reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
			STV090x_SETFIELD_Px(reg, TSFIFO_SERIAL_FIELD, 0x01);
			STV090x_SETFIELD_Px(reg, TSFIFO_DVBCI_FIELD, 0x01);
			if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
			{
				goto err;
			}
			break;
		}
		default:
		{
			break;
		}
	}  // end switch
	if (state->config->ts1_clk > 0)
	{
		u32 speed;

		switch (state->config->ts1_mode)
		{
			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
			default:
			{
				speed = state->mclk / (state->config->ts1_clk / 4);
				if (speed < 0x08)
				{
					speed = 0x08;
				}
				if (speed > 0xFF)
				{
					speed = 0xFF;
				}
				break;
			}
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			{
				speed = state->mclk / (state->config->ts1_clk / 32);
				if (speed < 0x20)
				{
					speed = 0x20;
				}
				if (speed > 0xFF)
				{
					speed = 0xFF;
				}
				break;
			}
		}
		reg = stv090x_read_reg(state, STV090x_P1_TSCFGM);
		STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
		if (stv090x_write_reg(state, STV090x_P1_TSCFGM, reg) < 0)
		{
			goto err;
		}
		if (stv090x_write_reg(state, STV090x_P1_TSSPEED, speed) < 0)
		{
			goto err;
		}
	}
	if (state->config->ts2_clk > 0)
	{
		u32 speed;

		switch (state->config->ts2_mode)
		{
			case STV090x_TSMODE_PARALLEL_PUNCTURED:
			case STV090x_TSMODE_DVBCI:
			default:
			{
				speed = state->mclk / (state->config->ts2_clk / 4);
				if (speed < 0x08)
				{
					speed = 0x08;
				}
				if (speed > 0xFF)
				{
					speed = 0xFF;
				}
				break;
			}
			case STV090x_TSMODE_SERIAL_PUNCTURED:
			case STV090x_TSMODE_SERIAL_CONTINUOUS:
			{
				speed = state->mclk / (state->config->ts2_clk / 32);
				if (speed < 0x20)
				{
					speed = 0x20;
				}
				if (speed > 0xFF)
				{
					speed = 0xFF;
				}
				break;
			}
		}
		reg = stv090x_read_reg(state, STV090x_P2_TSCFGM);
		STV090x_SETFIELD_Px(reg, TSFIFO_MANSPEED_FIELD, 3);
		if (stv090x_write_reg(state, STV090x_P2_TSCFGM, reg) < 0)
		{
			goto err;
		}
		if (stv090x_write_reg(state, STV090x_P2_TSSPEED, speed) < 0)
		{
			goto err;
		}
	}
	reg = stv090x_read_reg(state, STV090x_P2_TSCFGH);
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
	{
		goto err;
	}
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_P2_TSCFGH, reg) < 0)
	{
		goto err;
	}
	reg = stv090x_read_reg(state, STV090x_P1_TSCFGH);
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x01);
	if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
	{
		goto err;
	}
	STV090x_SETFIELD_Px(reg, RST_HWARE_FIELD, 0x00);
	if (stv090x_write_reg(state, STV090x_P1_TSCFGH, reg) < 0)
	{
		goto err;
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_set_tspath: I/O error\n");
	return -1;
}

static int stv090x_init(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	const struct stv090x_config *config = state->config;
	u32 reg;

	dprintk(10, "%s >\n", __func__);

	if (mp8125_init == 0)
	{
		mp8125_init = 1;
		if (box_type == MODEL_2849)
		{
			mp8125_en = stpio_request_pin(6, 0, "mp8125_en", STPIO_OUT);
			if (mp8125_en == NULL)
			{
				printk("!!!!!! FAIL : request pin 6 0\n");
			}
			else
			{
				stpio_set_pin (mp8125_en, 0);
			}
			mp8125_13_18 = stpio_request_pin(6, 5, "mp8125_13_18", STPIO_OUT);
			if (mp8125_13_18 == NULL)
			{
				printk("!!!!!! FAIL : request pin 6 5\n");
			}
			else
			{
				stpio_set_pin (mp8125_13_18, 0);
			}
		}
		// adb2850 pio7/3 only tone
		// adb2849 pio7/3 tone + DiSEqCc
		mp8125_extm = stpio_request_pin(7, 3, "mp8125_extm", STPIO_OUT);
		if (mp8125_extm == NULL)
		{
			printk("!!!!!! FAIL : request pin 7 3\n");
		}
		else
		{
			stpio_set_pin (mp8125_extm, 0);
			if (box_type == MODEL_2849)
			{
				pwm_diseqc_init();
			}
		}
	}
	if (state->mclk == 0)
	{
		/* call tuner init to configure the tuner's clock output
		   divider directly before setting up the master clock of
		   the stv090x. */
		if (config->tuner_init)
		{
			if (config->tuner_init(fe) < 0)
			{
				goto err;
			}
		}
		stv090x_set_mclk(state, 135000000, config->xtal); /* 135 Mhz */

		msleep(1);
		if (stv090x_write_reg(state, STV090x_SYNTCTRL, 0x20 | config->clk_mode) < 0)
		{
			goto err;
		}
		stv090x_get_mclk(state);
	}
	if (stv090x_wakeup(fe) < 0)
	{
		dprintk(1, "Error waking device\n");
		goto err;
	}

	if (stv090x_ldpc_mode(state, state->demod_mode) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, TNRCFG2);
	STV090x_SETFIELD_Px(reg, TUN_IQSWAP_FIELD, state->inversion);
	if (STV090x_WRITE_DEMOD(state, TNRCFG2, reg) < 0)
	{
		goto err;
	}
	reg = STV090x_READ_DEMOD(state, DEMOD);
	STV090x_SETFIELD_Px(reg, ROLLOFF_CONTROL_FIELD, state->rolloff);
	if (STV090x_WRITE_DEMOD(state, DEMOD, reg) < 0)
	{
		goto err;
	}
	if (config->tuner_set_mode)
	{
		if (config->tuner_set_mode(fe, TUNER_WAKE) < 0)
		{
			goto err;
		}
	}
	if (config->tuner_init)
	{
		if (config->tuner_init(fe) < 0)
		{
			goto err;
		}
	}
#if defined(ADB_2850)
	writereg_lnb_supply(state, 0xc8);
	msleep(1);
	writereg_lnb_supply(state, 0xe8);
#endif

	if (stv090x_set_tspath(state) < 0)
	{
		goto err;
	}
	dprintk(10, "%s <\n", __func__);
	return 0;

err:
	dprintk(1, "stv090x_init: I/O error\n");
	return -1;
}

static int stv090x_setup(struct dvb_frontend *fe)
{
	struct stv090x_state *state = fe->demodulator_priv;
	const struct stv090x_config *config = state->config;
	const struct stv090x_reg *stv090x_initval = NULL;
	const struct stv090x_reg *stv090x_cut20_val = NULL;
	unsigned long t1_size = 0, t2_size = 0;
	u32 reg = 0;
	int i;

	if (state->device == STV0900)
	{
		dprintk(10, "Initializing STV0900\n");
		stv090x_initval = stv0900_initval;
		t1_size = ARRAY_SIZE(stv0900_initval);
		stv090x_cut20_val = stv0900_cut20_val;
		t2_size = ARRAY_SIZE(stv0900_cut20_val);
	}
	else if (state->device == STV0903)
	{
		dprintk(10, "Initializing STV0903\n");
		stv090x_initval = stv0903_initval;
		t1_size = ARRAY_SIZE(stv0903_initval);
		stv090x_cut20_val = stv0903_cut20_val;
		t2_size = ARRAY_SIZE(stv0903_cut20_val);
	}
	else if (state->device == STX7111)
	{
		dprintk(10, "Initializing STX7111\n");
		stv090x_initval = stx7111_initval;
		t1_size = ARRAY_SIZE(stx7111_initval);
		t2_size = 0;
	}

	/* STV090x init */
	/* Stop Demod */
	if (stv090x_write_reg(state, STV090x_P1_DMDISTATE, 0x5c) < 0)
	{
		goto err;
	}
	if (stv090x_write_reg(state, STV090x_P2_DMDISTATE, 0x5c) < 0)
	{
		goto err;
	}
	msleep(1);

	/* Set No Tuner Mode */
	if (stv090x_write_reg(state, STV090x_P1_TNRCFG, 0x6c) < 0)
	{
		goto err;
	}
	if (stv090x_write_reg(state, STV090x_P2_TNRCFG, 0x6c) < 0)
	{
		goto err;
	}

#if defined(ADB_2850)
	STV090x_SETFIELD_Px(reg, STOP_ENABLE_FIELD, 1);
#endif
	/* I2C repeater OFF */
	STV090x_SETFIELD_Px(reg, ENARPT_LEVEL_FIELD, config->repeater_level);
	if (stv090x_write_reg(state, STV090x_P1_I2CRPT, reg) < 0)
	{
		goto err;
	}
	if (stv090x_write_reg(state, STV090x_P2_I2CRPT, reg) < 0)
	{
		goto err;
	}
	if (stv090x_write_reg(state, STV090x_NCOARSE, 0x13) < 0) /* set PLL divider */
	{
		goto err;
	}
	msleep(1);
	if (stv090x_write_reg(state, STV090x_I2CCFG, 0x08) < 0) /* 1/41 oversampling */
	{
		goto err;
	}
#if defined(ADB_2850)
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, 0x10 | config->clk_mode) < 0) /* enable PLL */
	{
		goto err;
	}
#else
	if (stv090x_write_reg(state, STV090x_SYNTCTRL, 0x20 | config->clk_mode) < 0) /* enable PLL */
	{
		goto err;
	}
#endif
	msleep(1);

	/* write initval */
	dprintk(10, "Setting up initial values\n");
	for (i = 0; i < t1_size; i++)
	{
		if (stv090x_write_reg(state, stv090x_initval[i].addr, stv090x_initval[i].data) < 0)
		{
			goto err;
		}
	}
	state->dev_ver = stv090x_read_reg(state, STV090x_MID);
	if (state->dev_ver >= 0x20)
	{
		if (stv090x_write_reg(state, STV090x_TSGENERAL, 0x0c) < 0)
		{
			goto err;
		}
		/* write cut20_val*/
		dprintk(10, "Setting up Cut 2.0 initial values\n");
		for (i = 0; i < t2_size; i++)
		{
			if (stv090x_write_reg(state, stv090x_cut20_val[i].addr, stv090x_cut20_val[i].data) < 0)
			{
				goto err;
			}
		}
	}
	else if (state->dev_ver < 0x20)
	{
		dprintk(1, "ERROR: Unsupported Cut: 0x%02x!\n", state->dev_ver);
		goto err;
	}
	else if (state->dev_ver > 0x30)
	{
		/* we shouldn't bail out from here */
		printk("INFO: Cut: 0x%02x probably incomplete support!\n", state->dev_ver);
	}
// ifndef FS9000
	/* ADC1 range */
	reg = stv090x_read_reg(state, STV090x_TSTTNR1);
	STV090x_SETFIELD(reg, ADC1_INMODE_FIELD, (config->adc1_range == STV090x_ADC_1Vpp) ? 0 : 1);
	if (stv090x_write_reg(state, STV090x_TSTTNR1, reg) < 0)
	{
		goto err;
	}
	/* ADC2 range */
	reg = stv090x_read_reg(state, STV090x_TSTTNR3);
	STV090x_SETFIELD(reg, ADC2_INMODE_FIELD, (config->adc2_range == STV090x_ADC_1Vpp) ? 0 : 1);
	if (stv090x_write_reg(state, STV090x_TSTTNR3, reg) < 0)
	{
		goto err;
	}
// #endif
	if (stv090x_write_reg(state, STV090x_TSTRES0, 0x80) < 0)
	{
		goto err;
	}
	if (stv090x_write_reg(state, STV090x_TSTRES0, 0x00) < 0)
	{
		goto err;
	}
	return 0;

err:
	dprintk(1, "stv090x_setup: I/O error\n");
	return -1;
}

#if DVB_API_VERSION < 5
static struct dvbfe_info dvbs_info =
{
	.name                     = "STV090x Multistandard",
	.delivery                 = DVBFE_DELSYS_DVBS,
	.delsys                   =
	{
		.dvbs.modulation  = DVBFE_MOD_QPSK,
		.dvbs.fec         = DVBFE_FEC_1_2
		                  | DVBFE_FEC_2_3
		                  | DVBFE_FEC_3_4
		                  | DVBFE_FEC_4_5
		                  | DVBFE_FEC_5_6
		                  | DVBFE_FEC_6_7
		                  | DVBFE_FEC_7_8
		                  | DVBFE_FEC_AUTO,
	},
	.frequency_min            = 950000,
	.frequency_max            = 2150000,
	.frequency_step           = 0,
	.frequency_tolerance      = 0,
	.symbol_rate_min          = 1000000,
	.symbol_rate_max          = 45000000
};

static const struct dvbfe_info dvbs2_info =
{
	.name                     = "STV090x Multistandard",
	.delivery                 = DVBFE_DELSYS_DVBS2,
	.delsys =
	{
		.dvbs2.modulation = DVBFE_MOD_QPSK
		                  | DVBFE_MOD_8PSK,
		/* TODO: Review these */
		.dvbs2.fec        = DVBFE_FEC_1_4
		                  | DVBFE_FEC_1_3
		                  | DVBFE_FEC_2_5
		                  | DVBFE_FEC_1_2
		                  | DVBFE_FEC_3_5
		                  | DVBFE_FEC_2_3
		                  | DVBFE_FEC_3_4
		                  | DVBFE_FEC_4_5
		                  | DVBFE_FEC_5_6
		                  | DVBFE_FEC_8_9
		                  | DVBFE_FEC_9_10,
	},
	.frequency_min            = 950000,
	.frequency_max            = 2150000,
	.frequency_step           = 0,
	.symbol_rate_min          = 1000000,
	.symbol_rate_max          = 45000000,
	.symbol_rate_tolerance    = 0
};

static int stv090x_get_info(struct dvb_frontend *fe, struct dvbfe_info *fe_info)
{
	switch (fe_info->delivery)
	{
		case DVBFE_DELSYS_DVBS:
		{
			memcpy(fe_info, &dvbs_info, sizeof(dvbs_info));
			break;
		}
		case DVBFE_DELSYS_DVBS2:
		{
			memcpy(fe_info, &dvbs2_info, sizeof(dvbs2_info));
			break;
		}
		default:
		{
			printk("%s() invalid arg\n", __func__);
			return -EINVAL;
		}
	}
	return 0;
}
#else  // DVB_API_VERSION >= 5

static int stv090x_get_property(struct dvb_frontend *fe, struct dtv_property *tvp)
{
	/* get delivery system info */
	if (tvp->cmd == DTV_DELIVERY_SYSTEM)
	{
		switch (tvp->u.data)
		{
			case SYS_DVBS2:
			case SYS_DVBS:
			case SYS_DSS:
			{
				break;
			}
			default:
			{
				return -EINVAL;
			}
		}
	}
	dprintk(20, "%s()\n", __func__);
	return 0;
}
#endif

// TODO move to lnb driver
/* Dagi: maybe we should make a directory for lnb supplies;
 * we have three different ones until now ... and lnbh23
 * is also used for newer ufs922
 */
int writereg_lnb_supply(struct stv090x_state *state, char data)
{
	int ret = -EREMOTEIO;
	struct i2c_msg msg;
	u8 buf;
	static struct i2c_adapter *adapter = NULL;

	buf = data;
//#warning fixme: make this adjustable in configuration
	if (adapter == NULL)
	{
		adapter = i2c_get_adapter(1);
	}

	msg.addr = 0x0a;
	msg.flags = 0;
	msg.buf = &buf;
	msg.len = 1;

	dprintk(100, "write LNB: %s:  write 0x%02x to 0x0a\n", __func__, data);
//	printk ("!!!!!!!! LNB write=0x%02x >> adr = 0x%02x\n", data,msg.addr);

	if ((ret = i2c_transfer(adapter, &msg, 1)) != 1)
	{
		printk("%s: writereg error(err == %i)\n", __func__, ret);
		ret = -EREMOTEIO;
	}
	return ret;
}

static int lnbh23_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage)
{
	struct stv090x_state *state = fe->demodulator_priv;

	dprintk(10, "%s > Tuner:%d\n", __func__, state->tuner);

	//lnbh23_t = LNBH23_PCL + LNBH23_TEN + LNBH23_TTX;  // always 22khz
	lnbh23_t = LNBH23_PCL + LNBH23_TTX;
	
	switch (voltage)
	{
		case SEC_VOLTAGE_OFF:
		{
			dprintk(10, "set_voltage_off\n");
//			if (_12v_isON == 0)
//			{
				if (box_type == MODEL_2849)
				{
					stpio_set_pin (mp8125_en, 0);
				}
				else
				{
					lnbh23_v = 0;
					writereg_lnb_supply(state, (lnbh23_v + lnbh23_t));
				}
//			}
			break;
		}
		case SEC_VOLTAGE_13: /* vertical */
		{
			dprintk(20, "Set_LNB voltage vertical\n");
			if (box_type == MODEL_2849)
			{
				stpio_set_pin (mp8125_en, 1);
				stpio_set_pin (mp8125_13_18, 0);
			}
			else
			{
				lnbh23_v = LNBH23_EN + LNBH23_LLC;  // 94
				writereg_lnb_supply(state, (lnbh23_v + lnbh23_t));
			}
			break;
		}
		case SEC_VOLTAGE_18: /* horizontal */
		{
			dprintk(20, "Set LNB voltage horizontal\n");
			if (box_type == MODEL_2849)
			{
				stpio_set_pin (mp8125_en, 1);
				stpio_set_pin (mp8125_13_18, 1);
			}
			else
	 		{
				lnbh23_v = LNBH23_EN + LNBH23_LLC + LNBH23_VSEL;  // 9c
				writereg_lnb_supply(state, (lnbh23_v + lnbh23_t));
			}
			break;
		}
		default:
		{
			break;
		}
	}
	dprintk(10, "%s <\n", __func__);
	return 0;
}

static struct dvb_frontend_ops stv090x_ops =
{
	.info =
	{
		.name                = "STV090x Multistandard",
		.type                = FE_QPSK,
		.frequency_min       = 950000,
		.frequency_max       = 2150000,
		.frequency_stepsize  = 0,
		.frequency_tolerance = 0,
		.symbol_rate_min     = 1000000,
		.symbol_rate_max     = 70000000,
		.caps                = FE_CAN_INVERSION_AUTO
		                     | FE_CAN_FEC_AUTO
		                     | FE_CAN_QPSK
	},
	.release                     = stv090x_release,
	.init                        = stv090x_init,

// workaround for tuner failed, a frontend open does not always wakeup the tuner
#ifndef FS9000
	.sleep                       = stv090x_sleep,
#endif
	.get_frontend_algo           = stv090x_frontend_algo,
	.i2c_gate_ctrl               = stv090x_i2c_gate_ctrl,

	.diseqc_send_master_cmd      = stv090x_send_diseqc_msg,
	.diseqc_send_burst           = stv090x_send_diseqc_burst,
	.diseqc_recv_slave_reply     = stv090x_recv_slave_reply,
	.set_tone                    = stv090x_set_tone,

	.search                      = stv090x_search,
	.read_status                 = stv090x_read_status,
	.read_ber                    = stv090x_read_per,
	.read_signal_strength        = stv090x_read_signal_strength,
	.read_snr                    = stv090x_read_cnr,
#if DVB_API_VERSION < 5
	.get_info                    = stv090x_get_info,
#else
	.get_property                = stv090x_get_property,
#endif

	.set_voltage             = lnbh23_set_voltage,  // ADB ITI-28XX
};

struct dvb_frontend *stv090x_attach(const struct stv090x_config *config, struct i2c_adapter *i2c, enum stv090x_demodulator demod, enum stv090x_tuner tuner)
{
	struct stv090x_state *state = NULL;

	state = kzalloc(sizeof(struct stv090x_state), GFP_KERNEL);
	if (state == NULL)
	{
		goto error;
	}
	state->verbose                   = &verbose;
	state->config                    = config;
	state->i2c                       = i2c;
	state->demod                     = demod;
	state->demod_mode                = config->demod_mode;  /* Single or Dual mode */
	state->device                    = config->device;
	state->rolloff                   = STV090x_RO_35;  /* default */
	state->tuner                     = tuner;
	state->frontend.ops              = stv090x_ops;
	state->frontend.demodulator_priv = state;

	state->mclk = 0;

	dprintk(10, "i2c adapter = %p\n", state->i2c);

#if defined(ADB_2850)
	mutex_init(&demod_lock);
#else
	if (state->demod == STV090x_DEMODULATOR_0)
	{
		mutex_init(&demod_lock);
	}
#endif
	if (stv090x_sleep(&state->frontend) < 0)
	{
		printk("Error putting device to sleep\n");
		goto error;
	}
	if (stv090x_setup(&state->frontend) < 0)
	{
		printk("Error setting up device\n");
		goto error;
	}
	if (stv090x_wakeup(&state->frontend) < 0)
	{
		printk("Error waking device\n");
		goto error;
	}
	dprintk(10, "Attaching %s demodulator(%d) Cut=0x%02x\n", state->device == STV0900 ? "STV0900" : "STV0903", demod, state->dev_ver);
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(stv090x_attach);
// vim:ts=4
