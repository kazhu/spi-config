spi-config
==========

spi board configuration without having to recompile the kernel, modified for ODROID C1

Compiling
---------
```
make KDIR=${KERNELSRC} install
```

Usage:
------
loading the module:

```modprobe spi-config devices=<devicedev1>,<devicedev2>,...,<devicedev16>```

and a ```<devicedev>``` is defined as a list of : separated key=value pairs

possible keys are:
* modalias = the driver to assign/use (required)
* irq = the irq to assign (optional, default none)
* speed = the maximum speed of the spi bus (optinal, default 2MHz)
* bus = the bus number (required)
* cs = the chip select (required)
* mode = the SPI mode (optional, default mode 0)
* irqgpio = the GPIO pin of the irq (optional, default none)
* irqsource = the irq source (0-7) (optional, default none)
* irqfilter = the irq filter. 0 = no filtering, 1..7 mean 111ns * 3 * x of filtering (optional, default 7)
* irqtype = the irq type. 0: High, 1: Low, 2: Rising, 3: Falling (optional, default 3)
* pd = platform data length to allocate
* pdx-<offset> = sets the hex values at byte-offset <offset> of the platform data (hex string - 2 chars per byte!!!)
* pdp-<offset> = sets a pointer to platform_data+<value> at byte-offset <offset> of the platform data 
* pds64-<offset> = sets the s64-value at byte-offset <offset> of the platform data 
* pdu64-<offset> = sets the u64-value at byte-offset <offset> of the platform data 
* pds32-<offset> = sets the s32-value at byte-offset <offset> of the platform data 
* pdu32-<offset> = sets the u32-value at byte-offset <offset> of the platform data 
* pds16-<offset> = sets the u16-value at byte-offset <offset> of the platform data 
* pdu16-<offset> = sets the s16-value at byte-offset <offset> of the platform data 
* pds8-<offset> = sets the u8-value at byte-offset <offset> of the platform data 
* pdu8-<offset> = sets the u8-value at byte-offset <offset> of the platform data 
* force_release = forces a release of a spi device if it has NOT been configured by this module 
  this action taints the kernel!!! Also this is defined without a =<value>.

<value> and <offset> can typically get prefixed with 0x for hex and 0 for octal.

So the following:

```
modprobe spi-config devices=\
bus=0:cs=0:modalias=mcp2515:speed=10000000:irqgpio=GPIOX_6:irqsource=0:irqfilter=0:irqtype=1:pd=20:pdu32-0=16000000:pdu32-4=0x20
```

will configure:
* on SPI0.0 a mcp251x device with max_speed of 10MHz with IRQ on GPIOX_6 with irq=96, no filter and interrupt on LOW and platform data that reflects: 16MHz crystal and Interrupt flags with IRQF_DISABLED
