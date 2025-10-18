#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    DRIVER_BUS_NONE = 0,
    DRIVER_BUS_PS2,
    DRIVER_BUS_PCI,
    DRIVER_BUS_PLATFORM,
    DRIVER_BUS_VIRTUAL
} driver_bus_t;
typedef enum {
    DRIVER_CLASS_MISC = 0,
    DRIVER_CLASS_INPUT,
    DRIVER_CLASS_BLOCK,
    DRIVER_CLASS_NET,
    DRIVER_CLASS_DISPLAY,
    DRIVER_CLASS_CHAR
} driver_class_t;

struct device;
struct driver;

typedef struct driver_ops {
	int(*probe)(struct device*);
	int(*init)(struct device*);
	int(*remove)(struct device*);
	int(*read)(struct device*, void*, size_t);
	int(*write)(struct device*, const void*, size_t);
	int(*ioctl)(struct device*, uint32_t, uintptr_t);
	void(*irq_handler)(struct device*, uint8_t);
} driver_ops_t;

typedef struct driver_manifest {
	const char* name;
	uint32_t version;
	driver_bus_t bus;
	driver_class_t class_;
	uint8_t pubkey_id[32];
	uint8_t code_hash[32];
	uint8_t signature[64];
} driver_manifest_t;

typedef struct driver {
	const char*name;
	driver_bus_t bus;
	driver_class_t class_;
	driver_ops_t ops;
	void* priv;
	const driver_manifest_t*manifest;
} driver_t;

typedef struct device {
	const char* name;
	driver_class_t class_;
	driver_bus_t bus;
	struct driver* drv;
	void* driver_data;
	void* os_data;
	uint8_t irq;
	bool initialized;
} device_t;

int driver_register_verified(driver_t* drv);
int driver_unregister(driver_t* drv);
int device_attach(device_t* dev, driver_t* drv);
int device_detach(device_t* dev);
device_t* driver_find_device(const char* name);
driver_t* driver_find(const char* name);

static inline int driver_probe(device_t*d) {
	return d->drv&&d->drv->ops.probe?d->drv->ops.probe(d):0;
}
static inline int driver_init(device_t*d) {
	return d->drv&&d->drv->ops.init?d->drv->ops.init(d):0;
}
static inline int driver_remove(device_t*d) {
	return d->drv&&d->drv->ops.remove?d->drv->ops.remove(d):0;
}
static inline int driver_read(device_t*d,void*b,size_t n) {
	return d->drv&&d->drv->ops.read?d->drv->ops.read(d,b,n):-1;
}
static inline int driver_write(device_t*d,const void*b,size_t n) {
	return d->drv&&d->drv->ops.write?d->drv->ops.write(d,b,n):-1;
}
static inline int driver_ioctl(device_t*d,uint32_t c,uintptr_t a) {
	return d->drv&&d->drv->ops.ioctl?d->drv->ops.ioctl(d,c,a):-1;
}
static inline void driver_irq(device_t*d,uint8_t i) {
	if(d->drv&&d->drv->ops.irq_handler)d->drv->ops.irq_handler(d,i);
}
