/*
 * e2_proc_info.c
 */

#include <linux/proc_fs.h> /* proc fs */
#include <asm/uaccess.h> /* copy_from_user */

int proc_info_model_read(char *page, char **start, off_t off, int count,
			 int *eof, void *data_unused)
{
	int len = 0;
	printk("%s\n", __FUNCTION__);
#if defined(CUBEREVO)
	len = sprintf(page, "cuberevo\n");
#elif defined(CUBEREVO_MINI)
	len = sprintf(page, "cuberevo-mini\n");
#elif defined(CUBEREVO_MINI2)
	len = sprintf(page, "cuberevo-mini2\n");
#elif defined(CUBEREVO_250HD)
	len = sprintf(page, "cuberevo-250hd\n");
#elif defined(CUBEREVO_MINI_FTA)
	len = sprintf(page, "cuberevo-mini-fta\n");
#elif defined(CUBEREVO_2000HD)
	len = sprintf(page, "cuberevo-2000hd\n");
#elif defined(CUBEREVO_9500HD)
	len = sprintf(page, "cuberevo-9500hd\n");
#elif defined(CUBEREVO_3000HD)
	len = sprintf(page, "cuberevo-3000hd\n");
#elif defined(TF7700)
	len = sprintf(page, "tf7700\n");
#elif defined(HL101)
	len = sprintf(page, "hl101\n");
#elif defined(VIP1_V1)
	len = sprintf(page, "vip1-v1\n");
#elif defined(VIP1_V2)
	len = sprintf(page, "vip1-v2\n");
#elif defined(VIP2)
	len = sprintf(page, "vip2\n");
#elif defined(UFS922)
	len = sprintf(page, "ufs922\n");
#elif defined(UFC960)
	len = sprintf(page, "ufc960\n");
#elif defined(FS9000)
	len = sprintf(page, "fs9000\n");
#elif defined(IPBOX9900)
	len = sprintf(page, "ipbox9900\n");
#elif defined(IPBOX99)
	len = sprintf(page, "ipbox99\n");
#elif defined(IPBOX55)
	len = sprintf(page, "ipbox55\n");
#elif defined(ADB_BOX)
	len = sprintf(page, "adb_box\n");
#elif defined(VITAMIN_HD5000)
	len = sprintf(page, "vitamin_hd5000\n");
#elif defined(SAGEMCOM88)
	len = sprintf(page, "sagemcom88\n");
#elif defined(ARIVALINK200)
	len = sprintf(page, "ariva@link200\n");
#elif defined(UFS910)
	len = sprintf(page, "ufs910\n");
#elif defined(OPT9600)
	len = sprintf(page, "opt9600\n");
#elif defined(OPT9600MINI)
	len = sprintf(page, "opt9600mini\n");
#elif defined(OPT9600PRIMA)
	len = sprintf(page, "opt9600prima\n");
#else
	len = sprintf(page, "unknown\n");
#endif
	return len;
}

