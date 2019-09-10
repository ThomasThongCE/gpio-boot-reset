#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>       // for fs function like alloc_chrdev_region / operation file
#include <linux/types.h>
#include <linux/device.h>   // for device_create and class_create
#include <linux/uaccess.h>  // for copy to/from user function
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of.h>       // access device tree file
#include <linux/delay.h>
#include <linux/slab.h>     // kmalloc, kcallloc, ....
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/kdev_t.h>

#define DRIVER_NAME "gpio-boot-reset"
#define FIRST_MINOR 0
#define DEFAULT_RESET_TIME 25
#define DEFAULT_BOOT_TIME 10

#define PDEBUG(fmt,args...) printk(KERN_DEBUG"%s: "fmt,DRIVER_NAME, ##args)
#define PERR(fmt,args...) printk(KERN_ERR"%s: "fmt,DRIVER_NAME,##args)
#define PINFO(fmt,args...) printk(KERN_INFO"%s: "fmt,DRIVER_NAME, ##args)

typedef struct gpio_data{
    unsigned gpio;
    bool active_low;
} gpio_data_t;

typedef struct dev_private_data {
	struct mutex lock;
    struct device *dev;
    const char *name;
    gpio_data_t reset;
    gpio_data_t boot;
    int reset_time;
    int boot_time;
    struct task_struct* startup_task;
} dev_private_data_t;

typedef struct platform_private_data{
    struct class * dev_class;
    int num_reset;
    dev_private_data_t devices [];
} platform_private_data_t;

dev_t dev_num ;
int dev_num_major = 0;

int reset_task(void* );
void reset_dev(dev_private_data_t *);
void boot_dev(dev_private_data_t *);
void delay_time (int);

int reset_task(void* param)
{
    dev_private_data_t *data = (dev_private_data_t *)param;

    reset_dev(data);
    PINFO("%s reseted\r\n",data->name);
    return 0;
}

static inline int sizeof_platform_data(int num_reset)
{
	return sizeof(platform_private_data_t) +
		(sizeof(dev_private_data_t) * num_reset);
}

void reset_dev(dev_private_data_t *data)
{
    mutex_lock(&data->lock);
    gpio_set_value(data->reset.gpio, 1 ^ data->reset.active_low); 

    delay_time(data->reset_time);

    gpio_set_value(data->reset.gpio, 0 ^ data->reset.active_low);
    mutex_unlock(&data->lock);
}

void boot_dev(dev_private_data_t *data)
{
    mutex_lock(&data->lock);
    gpio_set_value(data->reset.gpio, 1 ^ data->reset.active_low);
    gpio_set_value(data->boot.gpio, 1 ^ data->boot.active_low);

    delay_time(data->reset_time);

    gpio_set_value(data->reset.gpio, 0 ^ data->reset.active_low);
    delay_time(data->boot_time);
    gpio_set_value(data->boot.gpio, 0 ^ data->boot.active_low);
    mutex_unlock(&data->lock);
}

void delay_time (int time)
{
    if (time <= 10)
        udelay(time);
    else if (time <= 15000)
        usleep_range(time, time + 10);
    else 
        msleep(time/1000);
}

/***********************************/
/***** define device attribute *****/
/***********************************/

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buff, size_t len)
{
    dev_private_data_t *data = dev_get_drvdata(dev);
    if (!data)
        PERR ("Can't get dev private data: %d", data);

    if (strcmp(buff,"prog") == 0)
    {
       boot_dev(data);
    } 
    else if (strcmp(buff,"normal") == 0)
    {
       reset_dev(data);
    }
    else PINFO ("mode input note valid, please enter \"prog\" or \"normal\" (without quote)\n");
    
    return len;
} 

static struct device_attribute dev_class_attr[] = {
    __ATTR(mode,0222,NULL,mode_store),
    __ATTR_NULL,
};

/***************************/
/*****module init + exit****/
/***************************/
static int driver_probe (struct platform_device *pdev)
{
    int num_reset, res;
    struct device_node *np = pdev->dev.of_node;
    struct device_node *child ;
    platform_private_data_t *data;

    PINFO ("driver module init\n");
    PINFO ("node name %s\n",pdev->dev.of_node->name );

    num_reset = of_get_child_count(np);
    if (!num_reset)
		return -ENODEV;

    // register device
    res = alloc_chrdev_region(&dev_num, FIRST_MINOR, num_reset, DRIVER_NAME);
     if (res){
        PERR("Can't register device, error code: %d \n", res); 
        return -1;
    }
    dev_num_major = MAJOR(dev_num);

    // create private data
    data = (platform_private_data_t*)kcalloc(1, sizeof_platform_data(num_reset), GFP_KERNEL);
    data->num_reset = 0;

    // create class 
    data->dev_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(data->dev_class))
    {
        PERR("Class create fail, error code: %d\n", (int)data->dev_class);

        goto error_class;
    }
    data->dev_class->dev_attrs = dev_class_attr;
    
    num_reset = 0;
    for_each_child_of_node(np, child) {
        dev_private_data_t *device = &data->devices[num_reset];
        u32 temp;
        mutex_init(&device->lock);
        
		device->name = of_get_property(child, "label", NULL) ? : child->name;

        // get gpio properties and create device
        device->reset.active_low = of_property_read_bool(child,"reset-active-low");
        device->boot.active_low = of_property_read_bool(child,"boot-active-low");

        res = of_property_read_u32(child, "reset-time", &temp);
        if (!res)
            device->reset_time = temp;
        else
            device->reset_time = DEFAULT_RESET_TIME;

        res = of_property_read_u32(child, "boot-time", &temp);
        
        if (!res)
            device->boot_time = temp;
        else
            device->boot_time = DEFAULT_BOOT_TIME;

        device->dev = device_create(data->dev_class, &pdev->dev, 0, device, "%s", device->name);

        if (IS_ERR(device->dev))
        {
            PERR("device for %s create fall, error code: %d\n", device->name, (int)device->dev);

            goto error_device;
        }

        // get gpio number from device tree
        device->reset.gpio = of_get_named_gpio(child, "reset", 0);
        if (!device->reset.gpio)
        {
            PERR ("can't get reset gpio from %s, error code: %d\n", device->name, device->reset.gpio);

            goto error_reset_gpio;
        } 
        device->boot.gpio = of_get_named_gpio(child, "boot", 0);
        if (!device->boot.gpio)
        {
            PERR ("can't get boot gpio from %s, error code: %d\n", device->name, device->boot.gpio);

            goto error_boot_gpio;
        } 

        // request gpio and init
        if (!gpio_is_valid(device->reset.gpio))
        {
            PINFO ("reset gpio number is invalid \n");

            goto error_gpio_init;
        }
        else devm_gpio_request_one(device->dev, device->reset.gpio, GPIOF_OUT_INIT_LOW, "reset");

        if (!gpio_is_valid(device->boot.gpio))
        {
            PINFO ("reset gpio number is invalid \n");

            goto error_gpio_init;
        }
        else devm_gpio_request_one(device->dev, device->boot.gpio, GPIOF_OUT_INIT_LOW, "boot");

	    PINFO("device %s configuration : \n", device->name);
        PINFO("\treset_time: %d\n", device->reset_time);
        PINFO("\tboot_time: %d\n", device->boot_time);
        PINFO("\trset-active-low: %s\n", device->reset.active_low ? "true" : "false");
        PINFO("\tboot-active-low: %s\n", device->boot.active_low ? "true" : "false");
        PINFO("\treset_gpio_number: %d\n", device->reset.gpio);
        PINFO("\tboot_gpio_number: %d\n", device->boot.gpio);
        ++num_reset;

        device->startup_task = (struct task_struct*)kcalloc(1, sizeof(struct task_struct), GFP_KERNEL);
        device->startup_task = kthread_run(reset_task, device, device->name);
        if (!device->startup_task)
            PINFO("%s reset onstaup fail\r\n", device->name);
        continue;

        error_gpio_init:
            gpio_free(device->boot.gpio);
        error_boot_gpio:
            gpio_free(device->reset.gpio);
        error_reset_gpio:
            device_destroy(data->dev_class, dev_num);
        error_device:
            continue;
    }
    if (num_reset == 0)
    {
        class_destroy(data->dev_class);
        kfree(data);

        return -1;
    }
    data->num_reset = num_reset;
    
    platform_set_drvdata(pdev, data);

    return 0;

    //error handle
error_class:
    return -1;

}

static int driver_remove(struct platform_device *pdev)
{
    int i;
    platform_private_data_t *data = platform_get_drvdata(pdev);
    if (!data)
        PERR ("Can't get dev private data: %d", data);
    PINFO("driver module remove from kernel\n");

    for (i = 0 ; i < data->num_reset; ++i)
    {
        gpio_free(data->devices[i].boot.gpio);
        gpio_free(data->devices[i].reset.gpio);
        dev_num = MKDEV(dev_num_major, FIRST_MINOR + i);
        device_destroy(data->dev_class, dev_num);
    }

    class_destroy(data->dev_class);
    kfree(data);
    platform_set_drvdata(pdev, NULL);

    return 0;
}

static const struct of_device_id reset_dst[]={
    { .compatible = "gpio-boot-reset", },
    {}
};

MODULE_DEVICE_TABLE(of, reset_dst);	

static struct platform_driver gpio_isp = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,   
        .of_match_table = of_match_ptr (reset_dst),
    },
    .probe = driver_probe,
    .remove = driver_remove,
};

module_platform_driver(gpio_isp);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("THOMASTHONG");
