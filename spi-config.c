/*
 *  linux/arch/arm/mach-meson8b/spi_config.c
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DRV_NAME	"spi_config"
#define DEFAULT_SPEED	2000000
#define MAX_DEVICES	4

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#include <linux/string.h>
#include <linux/spi/spi.h>

#include <linux/amlogic/aml_gpio_consumer.h>
#include <asm/irq.h>

/* the module parameters */
static char* devices="";
module_param(devices, charp, S_IRUGO);
MODULE_PARM_DESC(devices, "SPI device configs");

struct device_config {
      struct spi_board_info	*brd;
      u32			pd_len;
      int			force_release;
      char			irq_gpioname[20];
      int			irq_source;
};

/* the devices that we have registered */
static struct spi_device *spi_devices[MAX_DEVICES];
u16 spi_devices_bus[MAX_DEVICES];
u16 spi_devices_cs[MAX_DEVICES];
static int spi_devices_count = 0;

static void register_device(char *devdesc);
static void release_device(u16 bus, u16 cs,struct spi_device * dev);

static int __init spi_config_init(void)
{
	char *head = devices;
	/* clean up the spi_devices array */
	memset(spi_devices, 0, sizeof(spi_devices));
	/* parse the devices parameter */
	while (*head) {
		/* find delimiter and create a separator */
		char *idx = strchr(head, ',');
		if (idx) { 
			*idx = 0; 
		}
		
		/* now parse the argument - if it is not "empty" */
		if (*head) { 
			register_device(head);
		}
		
		/* and skip to next section and contine - exiting if there was no more ","*/
		if (idx) { 
			head = idx + 1;
		} else { 
			break;
		}
	}
	
	/* and return OK */
        return 0;
}
module_init(spi_config_init);

static void __exit spi_config_exit(void)
{
	int i;
	/* unregister devices */
	for (i=0; i<MAX_DEVICES; i++) {
		if (spi_devices[i]) {
			release_device(spi_devices_bus[i], spi_devices_cs[i], spi_devices[i]);
			spi_devices[i] = NULL;
		}
	}
	
	/* and return */
	return;
}
module_exit(spi_config_exit);

static int spi_config_match_cs(struct device * dev, void *data) {
	/* convert pointers to something we need */
	struct spi_device *sdev=(struct spi_device *)dev;
	u8 cs=(int)data;
	/* convert to SPI device */
	printk(KERN_INFO " spi_config_match_cs: SPI%i: check CS=%i to be %i\n", sdev->master->bus_num, sdev->chip_select, cs);

	if (sdev->chip_select == cs) {
		if (dev->driver) {
			printk(KERN_INFO " spi_config_match_cs: SPI%i.%i: Found a device with modinfo %s\n", sdev->master->bus_num, sdev->chip_select, dev->driver->name);
		} else {
			printk(KERN_INFO " spi_config_match_cs: SPI%i.%i: Found a device, but no driver...\n", sdev->master->bus_num, sdev->chip_select);
		}
		
		return 1;
	}
	
	/* by default return 0 - no match */
	return 0;
}

static struct device_config* parse_device_config(char *devdesc)
{
	struct device_config* result;
	char *tmp;
	
	result = kmalloc(sizeof(struct device_config), GFP_KERNEL);
	if (!result)
	{
		printk(KERN_ERR " spi_config_register: could not allocate %i bytes\n", sizeof(struct device_config));
		return NULL;
	}
	
	memset(result, 0, sizeof(struct device_config));
	result->irq_source = -1;
	
	result->brd = kmalloc(sizeof(struct spi_board_info), GFP_KERNEL);
	if (!result->brd)
	{
		printk(KERN_ERR " spi_config_register: could not allocate %i bytes\n", sizeof(struct spi_board_info));
		kfree(result);
		return NULL;
	}
	
	memset(result->brd, 0, sizeof(struct spi_board_info));
	result->brd->irq = -1;
	result->brd->max_speed_hz = DEFAULT_SPEED;
        result->brd->bus_num = 0xffff;
        result->brd->chip_select = 0xffff;
        result->brd->mode = SPI_MODE_0;
	
#define HANDLE_SIMPLE_DATA(dataType, prefix, converter)\
	if (strncmp(key, prefix, strlen(prefix))==0) {\
		u32 offset;\
		dataType v;\
		if (kstrtou32(key + strlen(prefix), 0, &offset)) {\
			printk(KERN_ERR " spi_config_register: the " prefix " position can not get parsed in %s - ignoring config\n", key + strlen(prefix));\
			goto parse_device_config_error;\
		}\
		if (offset + sizeof(dataType) > result->pd_len) {\
			printk(KERN_ERR " spi_config_register: the " prefix " position %02x is larger than the length of the structure (%02x) - ignoring config\n", offset, result->pd_len);\
			goto parse_device_config_error;\
		}\
		if (converter(value, 0, &v)) {\
			printk(KERN_ERR " spi_config_register: the " prefix " value can not get parsed in %s - ignoring config\n", value);\
			goto parse_device_config_error;\
		}\
		*((dataType*)(result->brd->platform_data + offset)) = v;\
	}
#define HANDLE_DATA(expectedKey, converter, destination)\
	if (strcmp(key, expectedKey) == 0) {\
		if (converter(value, 10, &destination)) {\
			printk(KERN_ERR " spi_config_register: %s=%s can not get parsed - ignoring config\n", key, value);\
			goto parse_device_config_error;\
		}\
	}
	
	/* now parse the device description */
	while ((tmp = strsep(&devdesc, ":"))) {
		char *value = tmp;
		char *key = strsep(&value, "=");
		
		if (!value) {
			/* some keyonly fields */
			if (strcmp(key, "force_release") == 0) {
				result->force_release = 1;
			} else {
				printk(KERN_ERR " spi_config_register: incomplete argument: %s - no value\n", key);
				goto parse_device_config_error;
			}
		}
		else if (strcmp(key, "modalias") == 0) {
			strncpy(result->brd->modalias, value, sizeof(result->brd->modalias));
		} 
		else HANDLE_DATA("irq", kstrtoint, result->brd->irq)
		else HANDLE_DATA("speed", kstrtou32, result->brd->max_speed_hz)
		else HANDLE_DATA("bus", kstrtou16, result->brd->bus_num)
		else HANDLE_DATA("cs", kstrtou16, result->brd->chip_select)
		else HANDLE_DATA("mode", kstrtou8, result->brd->mode)
		else if (strcmp(key, "irqgpio") == 0) {
			strncpy(result->irq_gpioname, value, sizeof(result->irq_gpioname));
		} 
		else HANDLE_DATA("irqsource", kstrtoint, result->irq_source)
		else if (strcmp(key, "pd") == 0) {
			/* we may only allocate once */
			if (result->pd_len) {
				printk(KERN_ERR " spi_config_register: the pd has already been configured - ignoring config\n");
				goto parse_device_config_error;
			}
			/* get the length of platform data */
			if (kstrtou32(value, 0, &result->pd_len)) {
				printk(KERN_ERR " spi_config_register: the pd length can not get parsed in %s - ignoring config\n", value);
				goto parse_device_config_error;
			}
			/* now we allocate it - maybe we should allocate a minimum size to avoid abuse? */
			result->brd->platform_data = kmalloc(result->pd_len, GFP_KERNEL);
			if (result->brd->platform_data) {
				memset((char*)result->brd->platform_data, 0, result->pd_len);
			} else {
				printk(KERN_ERR " spi_config_register: could not allocate %i bytes for platform memory\n", result->pd_len);
				goto parse_device_config_error;
			}
		} 
		else if (strncmp(key,"pdx-", 4) == 0) {
			u32 offset;
			char *src = value;
			if (kstrtou32(key + 4, 0, &offset)) {
				printk(KERN_ERR " spi_config_register: the pdx position can not get parsed in %s - ignoring config\n", key + 4);
				goto parse_device_config_error;
			}
			if (offset >= result->pd_len) {
				printk(KERN_ERR " spi_config_register: the pdx offset %i is outside of allocated length %i- ignoring config\n", offset, result->pd_len);
				goto parse_device_config_error;
			}
			/* and now we fill it in with the data */
			while (offset < result->pd_len) {
				char hex[3];
				char v;
				hex[0] = *(src++);
				if (hex[0] == '\0') { break; }
				hex[1] = *(src++);
				if (hex[1] == '\0') {
					printk(KERN_ERR " spi_config_register: the pdx hex-data is not of expected length in %s (hex number needs to be chars)- ignoring config\n",
						value);
					goto parse_device_config_error;
				}
				hex[2] = 0; /* zero terminate it */
				if (kstrtou8(hex, 16, &v)) {
					printk(KERN_ERR " spi_config_register: the pdx data could not get parsed for %s in %s - ignoring config\n",
						hex, value);
				} else {
					*((char*)(result->brd->platform_data + offset)) = v;
					offset++;
				}
			}
			/* check overflow */
			if (*src) {
				printk(KERN_ERR " spi_config_register: the pdx data exceeds allocated length - rest of data is: %s - ignoring config\n", src);
				goto parse_device_config_error;
			}
		} else if (strncmp(key, "pdp-", 4) == 0) {
			u32 offset;
			u32 v;
			if (kstrtou32(key + 4, 0, &offset)) {
				printk(KERN_ERR " spi_config_register: the pdp position can not get parsed in %s - ignoring config\n", key + 4);
				goto parse_device_config_error;
			}
			if (offset + sizeof(void*) >= result->pd_len) {
				printk(KERN_ERR " spi_config_register: the pdp position %02x is larger than the length of the structure (%02x) - ignoring config\n", offset, result->pd_len);
				goto parse_device_config_error;
			}
			/* now read the value */
			if (kstrtou32(value, 0, &v)) {
				printk(KERN_ERR " spi_config_register: the pdp value can not get parsed in %s - ignoring config\n", value);
				goto parse_device_config_error;
			}
			/* and do some sanity checks */
			if (v >= result->pd_len) {
				printk(KERN_ERR " spi_config_register: the pdp value points outside of platform data - ignoring config\n");
				goto parse_device_config_error;
			}
			/* maybe we also should check that there is at least sizeof(void*) bytes left to point to...*/
			*((char**)(result->brd->platform_data + offset))=(char*)(result->brd->platform_data) + v;
		} 
		else HANDLE_SIMPLE_DATA(s64, "pds64-", kstrtos64)
		else HANDLE_SIMPLE_DATA(u64, "pdu64-", kstrtou64)
		else HANDLE_SIMPLE_DATA(s32, "pds32-", kstrtos32)
		else HANDLE_SIMPLE_DATA(u32, "pdu32-", kstrtou32)
		else HANDLE_SIMPLE_DATA(s16, "pds16-", kstrtos16)
		else HANDLE_SIMPLE_DATA(u16, "pdu16-", kstrtou16)
		else HANDLE_SIMPLE_DATA(s8, "pds8-", kstrtos8)
		else HANDLE_SIMPLE_DATA(u8, "pdu8-", kstrtou8)
		else {
			printk(KERN_ERR " spi_config_register: unsupported argument %s - ignoring config\n",key);
			goto parse_device_config_error;
		}
	}
	
	return result;
	
parse_device_config_error:
	if (result->brd->platform_data) kfree(result->brd->platform_data);
	kfree(result->brd);
	kfree(result);
	return NULL;
}

static void register_device(char *devdesc) {
	struct device_config *config;
	struct spi_master *master;
	int i;
	
	/* log the parameter */
	printk(KERN_INFO "spi_config_register: device description: %s\n", devdesc);

	config = parse_device_config(devdesc);
	if (!config) {
		goto register_device_err;
	}

	/* now check if things are set correctly */
	/* first the bus */
	if (config->brd->bus_num == 0xffff) {
		printk(KERN_ERR " spi_config_register: bus not set - ignoring config \n");
		goto register_device_err;
	}
	
	/* getting the master info */
	master = spi_busnum_to_master(config->brd->bus_num);
	if (!master) {
		printk(KERN_ERR " spi_config_register: no spi%i bus found - ignoring config\n", config->brd->bus_num);
		goto register_device_err;
	}
	
	/* now the chip_select */
	if (config->brd->chip_select == 0xffff) {
		printk(KERN_ERR " spi_config_register:spi%i: cs not set - ignoring config\n", config->brd->bus_num);
		goto register_device_err;
	}
	
	if (config->brd->chip_select > master->num_chipselect) {
		printk(KERN_ERR " spi_config_register:spi%i: cs=%i not possible for this bus - max_cs= %i - ignoring config\n",
			config->brd->bus_num, config->brd->chip_select, master->num_chipselect);
		goto register_device_err;
	}
	
	/* check if we are not in the list of registered devices already */
	for(i = 0; i < spi_devices_count; i++) {
		if ((spi_devices[i]) && (config->brd->bus_num == spi_devices[i]->master->bus_num) && (config->brd->chip_select == spi_devices[i]->chip_select)) {
			printk(KERN_ERR " spi_config_register:spi%i.%i: allready assigned - ignoring config\n", config->brd->bus_num, config->brd->chip_select);
			goto register_device_err;
		}
	}
	
	/* check if a device exists already for the requested cs - but is not allocated by us...*/
	if (master) {
		struct device *found = device_find_child(&master->dev, (void*)(int)config->brd->chip_select, spi_config_match_cs);
		if (found) {
			printk(KERN_ERR "spi_config_register:spi%i.%i:%s: found already registered device\n", config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			if (config->force_release) {
				/* write the message */
				printk(KERN_ERR " spi_config_register:spi%i.%i:%s: forcefully-releasing already registered device taints kernel\n", 
					config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
				/* let us taint the kernel */
				add_taint(TAINT_FORCED_MODULE, LOCKDEP_STILL_OK);
				/* the below leaves some unallocated memory wasting kernel memory !!! */
				spi_unregister_device((struct spi_device*)found);
				put_device(found);
			} else {
				printk(KERN_ERR " spi_config_register:spi%i.%i:%s: if you are sure you may add force_release to the arguments\n", 
					config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
				/* release device - needed from device_find_child */
				put_device(found);
				goto register_device_err;
			}
		}
	}
	
	/* now check modalias */
	if (!config->brd->modalias[0]) {
		printk(KERN_ERR " spi_config_register:spi%i.%i: modalias not set - ignoring config\n",
			config->brd->bus_num, config->brd->chip_select);
		goto register_device_err;
	}
	
	/* check speed is "reasonable" */
	if (config->brd->max_speed_hz < 8192) {
		printk(KERN_ERR " spi_config_register:spi%i.%i:%s: speed is set too low at %i - ignoring config\n",
			config->brd->bus_num, config->brd->chip_select, config->brd->modalias, config->brd->max_speed_hz);
		goto register_device_err;
	}

	if (config->irq_gpioname[0] || config->irq_source >= 0) {
		int gpio;
		if (config->brd->irq >= 0) {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: irq is set so irq_source and irq_gpio must be unset - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			goto register_device_err;
		}
		
		if (config->irq_gpioname[0] == '\0') {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: irq_source is set but irq_gpio is unset - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			goto register_device_err;
		}
		
		if (config->irq_source < 0) {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: irq_gpio is set but irq_source is unset - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			goto register_device_err;
		}
		
		config->brd->irq = INT_GPIO_0 + config->irq_source;
		
		gpio = amlogic_gpio_name_map_num(config->irq_gpioname);
		if (gpio < 0) {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: irq_gpio is invalid - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			goto register_device_err;
		}

		if (amlogic_gpio_request_one(gpio, GPIOF_IN, config->brd->modalias)) {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: amlogic_gpio_request_one fail - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias);
			goto register_device_err;
		}

		if (amlogic_gpio_to_irq(gpio, config->brd->modalias, AML_GPIO_IRQ(config->irq_source, FILTER_NUM7, GPIO_IRQ_FALLING))) {
			printk(KERN_ERR " spi_config_register:spi%i.%i:%s: amlogic_gpio_to_irq(%i,,%i) fail - ignoring config\n",
				config->brd->bus_num, config->brd->chip_select, config->brd->modalias, gpio, config->irq_source);
			goto register_device_err;
		}
	}
	
	/* register the device */
	if ((spi_devices[spi_devices_count]=spi_new_device(master, config->brd))) {
		spi_devices_bus[i]=spi_devices[spi_devices_count]->master->bus_num;
		spi_devices_cs[i]=spi_devices[spi_devices_count]->chip_select;

		/* now report the settings */
		if (spi_devices[spi_devices_count]->irq < 0) {
			printk(KERN_INFO "spi_config_register:spi%i.%i: registering modalias=%s with max_speed_hz=%i mode=%i and no interrupt\n",
				spi_devices[spi_devices_count]->master->bus_num,
				spi_devices[spi_devices_count]->chip_select,
				spi_devices[spi_devices_count]->modalias,
				spi_devices[spi_devices_count]->max_speed_hz,
				spi_devices[spi_devices_count]->mode
				);
		} else {
			printk(KERN_INFO "spi_config_register:spi%i.%i: registering modalias=%s with max_speed_hz=%i mode=%i and gpio/irq=%s/%i\n",
				spi_devices[spi_devices_count]->master->bus_num,
				spi_devices[spi_devices_count]->chip_select,
				spi_devices[spi_devices_count]->modalias,
				spi_devices[spi_devices_count]->max_speed_hz,
				spi_devices[spi_devices_count]->mode,
				config->irq_gpioname,
				spi_devices[spi_devices_count]->irq
				);
		}
		if (spi_devices[spi_devices_count]->dev.platform_data) {
			char prefix[64];
			snprintf(prefix,sizeof(prefix),"spi_config_register:spi%i.%i:platform data:",
				spi_devices[spi_devices_count]->master->bus_num,
				spi_devices[spi_devices_count]->chip_select
				);
			print_hex_dump(KERN_INFO,prefix,DUMP_PREFIX_ADDRESS,
				16,1,
				spi_devices[spi_devices_count]->dev.platform_data,config->pd_len,true
				);
		}
		spi_devices_count++;
	} else {
		printk(KERN_ERR "spi_config_register:spi%i.%i:%s: failed to register device\n", config->brd->bus_num,config->brd->chip_select,config->brd->modalias);
		goto register_device_err;
	}

	kfree(config);
	
	/* and return successfull */
	return;
	
	/* error handling code */
register_device_err:
	if (config->brd->platform_data) kfree(config->brd->platform_data);
	kfree(config->brd);
	kfree(config);
	return;
}

static void release_device(u16 bus, u16 cs,struct spi_device *spi) {
	/* checking if the device is still "ours" */
	struct spi_master *master=NULL;
	struct device *found=NULL;
	/* first the bus */
	master=spi_busnum_to_master(bus);
	if (!master) {
		printk(KERN_ERR " spi_config_register: no spi%i bus found - not deallocating\n",bus);
		return;
	}
	/* now check the device for the cs we keep on record */
	found=device_find_child(&master->dev,(void*)(int)cs,spi_config_match_cs);
	if (! found) {
		printk(KERN_ERR " spi_config_register: no spi%i.%i bus found - not deallocating\n",bus,cs);
		return;
	}
	/* and compare if it is still the same as our own record */
	if (found != &spi->dev) {
		printk(KERN_ERR " spi_config_register: the device spi%i.%i is different from the one we allocated - not deallocating\n",bus,cs);
		return;
	}

	printk(KERN_INFO "spi_config_unregister:spi%i.%i: unregister device with modalias %s\n", spi->master->bus_num,spi->chip_select,spi->modalias);
	/* unregister device */
	spi_unregister_device(spi);
	/* seems as if unregistering also means that the structures get freed as well - kernel crashes, so we do not do it */
}

/* the module description */
MODULE_DESCRIPTION("SPI board setup");
MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);

