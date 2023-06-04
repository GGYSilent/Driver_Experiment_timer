#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
/***************************************************************************************
 *    
 *   AUTHOR: Gao Geyuan
 *      VISION: 1.0
 *      BRIEF:  This module is to drive LED on the board, make the LED illuminates once 
 *              per second. 
 * 
****************************************************************************************/

#define LED_NAME "led_timer_device"
//#define DEBUG


#undef PDEBUG
#ifdef DEBUG
#   define PDEBUG(fmt,args...) printk(fmt,##args)
#else 
#   define PDEBUG(fmt,args...) 
#endif
#undef DEBUG

/*设备结构体*/
static struct led_timer_device {

    int major;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct platform_device *platform_device;
    int led_gpio;
    dev_t devid;
    struct device_node *node;
    int delay;
    char buff[30];
    int buff_len;
    struct timer_list ledtimer;
    
};


static struct led_timer_device led_timer_dev;
/***********************************************************************************
 * 定时器处理函数
 * 
 * 
***********************************************************************************/

static void timefunc(unsigned long data)
{ 
    struct led_timer_device *led_dev = (struct led_timer_device*)data;
    static int len=0;
    PDEBUG("\nlen=%d,buff_len:%d,buff:%s,",len,led_dev->buff_len,led_dev->buff);
    if(len<=led_dev->buff_len)
    {  
        PDEBUG("buff[%d]:%c",len,led_dev->buff[len]);
        if(led_dev->buff[len]=='1')
        {
            PDEBUG("ON");
            gpio_set_value(led_dev->led_gpio,0);
        }
        else{
            PDEBUG("OFF");
            gpio_set_value(led_dev->led_gpio,1);
        }
        len++;
    }
    else
    {
        len = 0;
        if(led_dev->buff[len]=='1')
        {
            PDEBUG("ON");
            gpio_set_value(led_dev->led_gpio,0);
        }
        else{
            PDEBUG("OFF");
            gpio_set_value(led_dev->led_gpio,1);
        }
        len++;      
    }
    mod_timer(&(led_dev->ledtimer), jiffies+msecs_to_jiffies(led_dev->delay));
}

static void time_run(struct led_timer_device *led_dev)
{
    
    init_timer(&(led_dev->ledtimer));
    led_dev->ledtimer.data = (unsigned long)led_dev;
    led_dev->ledtimer.function = timefunc;
    mod_timer(&(led_dev->ledtimer), jiffies+msecs_to_jiffies(led_dev->delay));
}



/**********************************************************************************
 *文件操作函数集
*/

static int led_timer_open(struct inode *inode, struct file *f)
{
    f->private_data = &led_timer_dev;
    led_timer_dev.delay = 500;
    return 0;
}


static int led_timer_write(struct file *f, const char *buff, size_t len, loff_t *off)
{
    int ret;
    struct led_timer_device *led_dev = (struct led_timer_device*)f->private_data;
    led_dev->buff_len = len;
    ret = copy_from_user(led_dev->buff, buff, len); 
    PDEBUG("\nbuff:%s, len:%d\n",led_dev->buff,len);   
    if(ret)
    {
        printk("%d byte waite copy",ret);
    }
    time_run(led_dev);
    return 0;
}



static int led_timer_read(struct file *f, char *buff, size_t len, loff_t *off )
{
        return 0;
}


static int led_timer_close(struct inode *inode, struct file *f)
{
    return 0;
}


static const struct file_operations file_opt =
{
    .owner = THIS_MODULE,
    .read = led_timer_read,
    .write = led_timer_write,
    .open = led_timer_open,
    .release = &led_timer_close,  
};


/*************************************************************************************/

/*platform平台初始化函数*/
static int probe_led_timer(struct platform_device *dev)
{
    led_timer_dev.platform_device = (struct platform_device*)dev;
    
    if(led_timer_dev.major)
    {
        led_timer_dev.devid = MKDEV(led_timer_dev.major,0);
        register_chrdev_region(led_timer_dev.devid, 1, LED_NAME);
    }
    else{
        alloc_chrdev_region(&led_timer_dev.devid, 0, 1, LED_NAME);
        led_timer_dev.major = MAJOR(led_timer_dev.devid);
    }

    led_timer_dev.class = class_create(THIS_MODULE, LED_NAME);
    if(IS_ERR(led_timer_dev.class))
    {
        return PTR_ERR(led_timer_dev.class);
    }

    led_timer_dev.device = device_create(led_timer_dev.class, NULL, led_timer_dev.devid, NULL, LED_NAME);
    if(IS_ERR(led_timer_dev.device)){
        return PTR_ERR(led_timer_dev.device);
    }


    cdev_init(&led_timer_dev.cdev, &file_opt);
    cdev_add(&led_timer_dev.cdev, led_timer_dev.devid, 1);

    led_timer_dev.node = of_find_node_by_path("/board_devices");
    led_timer_dev.led_gpio = of_get_named_gpio(led_timer_dev.node, "led_gpio",0);
    if(gpio_request(led_timer_dev.led_gpio,"led")<0)
    {
        printk("gpio request error!");
        return 0;
    }

    if(gpio_direction_output(led_timer_dev.led_gpio,1)<0)
    {
        printk("gpio_direction_output error!");
        return 0;
    }
    return 0;
}


static int remove_led_timer(struct platform_device *dev)
{
    del_timer_sync(&(led_timer_dev.ledtimer));
    gpio_set_value(led_timer_dev.led_gpio,1);
    cdev_del(&led_timer_dev.cdev);
    unregister_chrdev_region(led_timer_dev.devid,1);
    device_destroy(led_timer_dev.class,led_timer_dev.devid);
    class_destroy(led_timer_dev.class);
    return 0;

}

/*of_match_table设备树匹配表*/
static struct of_device_id led_timer_match_table[] = {
    {.compatible = "alientek,ledgpio" },
    {                                 }
};


/*platform总线结构体*/
static struct platform_driver platform_led_timer = {
    .probe = probe_led_timer,
    .remove = remove_led_timer,
    .driver = {
        .owner = THIS_MODULE,
        .name = "led_timer",
        .of_match_table = led_timer_match_table,
    }
};





/**************************************************************************************
*初始化和卸载函数
*/
static int led_timer_init(void)
{
    platform_driver_register(&platform_led_timer);
    return 0;
}



static void led_timer_release(void)
{
    platform_driver_unregister(&platform_led_timer);
}

/*************************************************************************************/




module_init(led_timer_init);
module_exit(led_timer_release);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gao Geyuan");
