/**
File Description: This is a notify light chip aw2013 i2c driver
Author: yaoyc
Create Date:20140926
Change List:
  Time:        Author:        Description:
20141028     yaoyc         1. let it work normal. charge : red led light; miss: green led blink; charge full: green led light; charge mid:blue led light;
20141103     yaoyc         1. code reconsitution
20141111     yaoyc         1. fix the issue that when green led blink cause of miss call or miss message, after check miss call or message,aways blink green led.
*/


#include "gn_aw2013.h"
//#include <mach/mt_gpio.h>
//#include <mach/mt_gpt.h>
//#include <mach/mt_pm_ldo.h>
//#include <mach/mt_typedefs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/workqueue.h> 



#define MISS_MSG_FLAG 		 0
#define MISS_CALL_FLAG 		 1
#define POWER_LOW_FLAG 		 2
#define CHARGING_FLAG  		 3
#define CHARGING_FULL_FLAG   4
#define CHARGING_MID_FLAG	 5
#define BLINK_ALL_LED		 6
#define AW2013_I2C_MAX_LOOP 		10   

#define I2C_delay 		2    //可根据平台调整,保证I2C速度不高于400k

#define AW2013_DBG
#ifdef AW2013_DBG
#define DBG_PRINT(x...)	printk(KERN_ERR x)
#else
#define DBG_PRINT(x...)
#endif

static unsigned char LED_ON_FLAG = 0x0;

#define TST_BIT(flag,bit)	(flag & (0x1 << bit))
#define CLR_BIT(flag,bit)	(flag &= (~(0x1 << bit)))
#define SET_BIT(flag,bit)	(flag |= (0x1 << bit))

//以下为调节呼吸效果的参数
#define Imax          0x02   //LED最大电流配置,0x00=omA,0x01=5mA,0x02=10mA,0x03=15mA,
#define Rise_time   0x02   //LED呼吸上升时间,0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Hold_time   0x01   //LED呼吸到最亮时的保持时间0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s
#define Fall_time     0x02   //LED呼吸下降时间,0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Off_time      0x01   //LED呼吸到灭时的保持时间0x00=0.13s,0x01=0.26s,0x02=0.52s,0x03=1.04s,0x04=2.08s,0x05=4.16s,0x06=8.32s,0x07=16.64s
#define Delay_time   0x00   //LED呼吸启动后的延迟时间0x00=0s,0x01=0.13s,0x02=0.26s,0x03=0.52s,0x04=1.04s,0x05=2.08s,0x06=4.16s,0x07=8.32s,0x08=16.64s
#define Period_Num  0x00   //LED呼吸次数0x00=无限次,0x01=1次,0x02=2次.....0x0f=15次


static ssize_t aw2013_store_led(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);
static ssize_t aw2013_get_reg(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t aw2013_set_reg(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);

//static DEVICE_ATTR(led, S_IRUGO | S_IWUSR, NULL, aw2013_store_led);
//static DEVICE_ATTR(reg, S_IRUGO | S_IWUGO,aw2013_get_reg,  aw2013_set_reg);
static DEVICE_ATTR(led, 0220, NULL, aw2013_store_led);
static DEVICE_ATTR(reg, 0660, aw2013_get_reg,  aw2013_set_reg);


struct i2c_client *aw2013_i2c_client;
static struct work_struct   aw2013_work;
static struct cust_aw2013_led cust_aw2013_led_list[AW2013_LED_TYPE_TOTAL];


void AW2013_delay_1us(uint16_t wTime);   //
int AW2013_i2c_write_reg(unsigned char reg,unsigned char data);
//*******************************AW2013呼吸灯程序***********************************///
void aw2013_breath_all(int led0,int led1,int led2)  //led on=0x01   ledoff=0x00
{  

	//write_reg(0x00, 0x55);				// Reset 
	AW2013_i2c_write_reg(0x01, 0x01);		// enable LED 不使用中断		

	AW2013_i2c_write_reg(0x31, Imax|0x70);	//config mode, IMAX = 5mA	
	AW2013_i2c_write_reg(0x32, Imax|0x70);	//config mode, IMAX = 5mA	
	AW2013_i2c_write_reg(0x33, Imax|0x70);	//config mode, IMAX = 5mA	

	AW2013_i2c_write_reg(0x34, 0xff);	// LED0 level,
	AW2013_i2c_write_reg(0x35, 0xff);	// LED1 level,
	AW2013_i2c_write_reg(0x36, 0xff);	// LED2 level,
											
	AW2013_i2c_write_reg(0x37, Rise_time<<4 | Hold_time);	//led0  上升时间，保持时间设定							
	AW2013_i2c_write_reg(0x38, Fall_time<<4 | Off_time);	       //led0 下降时间，关闭时间设定
	AW2013_i2c_write_reg(0x39, Delay_time<<4| Period_Num);   //led0  呼吸延迟时间，呼吸周期设定

	AW2013_i2c_write_reg(0x3a, Rise_time<<4 | Hold_time);	//led1上升时间，保持时间设定								
	AW2013_i2c_write_reg(0x3b, Fall_time<<4 | Off_time);	       //led1 下降时间，关闭时间设定
	AW2013_i2c_write_reg(0x3c, Delay_time<<4| Period_Num);   //led1  呼吸延迟时间，呼吸周期设定

	AW2013_i2c_write_reg(0x3d, Rise_time<<4 | Hold_time);	//led2  上升时间，保持时间设定				
	AW2013_i2c_write_reg(0x3e, Fall_time<<4 | Off_time);	       //led2 下降时间，关闭时间设定
	AW2013_i2c_write_reg(0x3f, Delay_time<<4| Period_Num);    //呼吸延迟时间，呼吸周期设定

	AW2013_i2c_write_reg(0x30, led2<<2|led1<<1|led0);	       //led on=0x01 ledoff=0x00	
	AW2013_delay_1us(8);//需延时5us以上
}





void AW2013_delay_1us(uint16_t wTime)   //
{
	udelay(wTime);
}

static int AW2013_i2c_write_reg_org(unsigned char reg,unsigned char data)
{
	int ack=0;
	unsigned char ret;
	unsigned char wrbuf[2];

	wrbuf[0] = reg;
	wrbuf[1] = data;

	ret = i2c_master_send(aw2013_i2c_client, wrbuf, 2);
	if (ret != 2) {
		dev_err(&aw2013_i2c_client->dev,
		"%s: i2c_master_recv() failed, ret=%d\n",
		__func__, ret);
		ack = 1;
	}

	return ack;
}

int AW2013_i2c_write_reg(unsigned char reg,unsigned char data)
{
	int ack=0;
	unsigned char i;
	for (i=0; i<AW2013_I2C_MAX_LOOP; i++)
	{
		ack = AW2013_i2c_write_reg_org(reg,data);
		if (ack == 0) // ack success
			break;
		}
	return ack;
}

unsigned char AW2013_i2c_read_reg(unsigned char regaddr) 
{
	unsigned char rdbuf[1], wrbuf[1], ret, i;

	wrbuf[0] = regaddr;

	for (i=0; i<AW2013_I2C_MAX_LOOP; i++) 
	{
		ret = i2c_master_send(aw2013_i2c_client, wrbuf, 1);
		if (ret == 1)
			break;
	}
	
	ret = i2c_master_recv(aw2013_i2c_client, rdbuf, 1);
	
	if (ret != 1)
	{
		dev_err(&aw2013_i2c_client->dev,"%s: i2c_master_recv() failed, ret=%d\n",
			__func__, ret);
	}
	
    	return rdbuf[0];
		
}


extern struct i2c_adapter * get_mt_i2c_adaptor(int);



int breathlight_master_send(char * buf ,int count)
{
	unsigned char ret;
	
	ret = i2c_master_send(aw2013_i2c_client, buf, count);
	printk(KERN_ERR "aw2013 i2c write 0x%x 0x%x\n", buf[0], buf[1]);
	if (ret != count) 
	{
		dev_err(&aw2013_i2c_client->dev,"%s: i2c_master_recv() failed, ret=%d\n",
			__func__, ret);
	}
	return ret;
}

void led_off_aw2013(void)
{
	char buf[2];

	buf[0]=0x30;
	buf[1]=0x00;
	breathlight_master_send(buf,2);

}

void led_flash_aw2013_power_low(int enable)/////red led
{
	unsigned int id =2;/////red led
	char buf[2];
	if(enable) {
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x71;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x37+id*3;
		buf[1]=0x00;
		breathlight_master_send(buf,2);

		buf[0]=0x38+id*3;
		buf[1]=0x04;
		breathlight_master_send(buf,2);

		buf[0]=0x39+id*3;
		buf[1]=0x00;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else {
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}

void led_flash_aw2013_charging_full(int enable)
{
	unsigned int id =1;/////green led
	char buf[2];
	if(enable){
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x02;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else{
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}

void led_flash_aw2013_charging(int enable)
{
	unsigned int id =2;/////red led
	char buf[2];
	
	printk(KERN_ERR "led_flash_aw2013_charging enable = %d\n", enable);
	if(enable){
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x02;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else{
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}

void led_flash_aw2013_charging_mid_flag(int enable)
{
	unsigned int id =0;////blue led
	char buf[2];
	if(enable){
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x02;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else{
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}
void led_flash_aw2013_unanswer_message_incall(int enable)/////blue led
{
	unsigned int id =0;////blue led
	char buf[2];
	if(enable) {
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x73;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x37+id*3;
		buf[1]=0x32;
		breathlight_master_send(buf,2);

		buf[0]=0x38+id*3;
		buf[1]=0x35;
		breathlight_master_send(buf,2);

		buf[0]=0x39+id*3;
		buf[1]=0x00;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else {
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}

void led_flash_aw2013_power_on(int enable)/////
{
	char buf[2];
	unsigned int id =0;////0 blue led ,1 green ,2 red,
	if(enable) {
		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x73;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;//0xc8;
		breathlight_master_send(buf,2);

		buf[0]=0x37+id*3;
		buf[1]=0x34;
		breathlight_master_send(buf,2);

		buf[0]=0x38+id*3;
		buf[1]=0x35;
		breathlight_master_send(buf,2);

		buf[0]=0x39+id*3;
		buf[1]=0x03;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		buf[1]=1<<id;
		breathlight_master_send(buf,2);
	}else {
		buf[0]=0x30;
		buf[1]=0<<id;
		breathlight_master_send(buf,2);
	}
}
/*
void Suspend_led(void)
{
	//first if it's charging situation, we skip the other actions
	led_off_aw2013();
}
*/
#if 0//defined(CONFIG_HAS_EARLYSUSPEND)
static void hwdctl_early_suspend(struct early_suspend *h)
{
	Flush_led_data();
}

static void hwdctl_late_resume(struct early_suspend *h)
{
	Suspend_led();
}
#endif

static ssize_t aw2013_store_led(struct device* cd, struct device_attribute *attr, 
										const char *buf, size_t len )
{
	int temp,command,enable;
	sscanf(buf, "%d", &temp);
	enable = temp>>4;
	command = temp & 0x0f;
	switch(command) 
	{
		case MISS_MSG_FLAG:
			led_flash_aw2013_unanswer_message_incall(enable);
			break;
		case MISS_CALL_FLAG:
			led_flash_aw2013_unanswer_message_incall(enable);
			break;
		case POWER_LOW_FLAG:
			led_flash_aw2013_power_low(enable);
			break;
		case CHARGING_FULL_FLAG:
			led_flash_aw2013_charging_full(enable);
			break;
		case CHARGING_FLAG:
			led_flash_aw2013_charging(enable);
			break;
		case CHARGING_MID_FLAG:
			led_flash_aw2013_charging_mid_flag(enable);
			break;
		case BLINK_ALL_LED:
			aw2013_breath_all(1,1,1);
			break;
		default:
			printk(KERN_ERR "invalid command occurred\n");
			break;		
	}
	
	return len;
}
static ssize_t aw2013_get_reg(struct device* cd, struct device_attribute* attr,
										 char *buf)
{
	u8 rbuf[18];
	u8 i;
	rbuf[0] = AW2013_i2c_read_reg(0x00);
	rbuf[1] = AW2013_i2c_read_reg(0x01);
	for(i=0;i<0x10;i++) {
		rbuf[i+2] = AW2013_i2c_read_reg(0x30+i);
	}
	
	printk(KERN_ERR "[0]%x;[1]%x;[30]%x;[31]%x;[32]%x;[33]%x;[34]%x;[35]%x;[36]%x;[37]%x;[38]%x;[39]%x;[3a]%x;[3b]%x;[3c]%x;[3d]%x;[3e]%x;[3f]%x \n ", 
		rbuf[0],rbuf[1],rbuf[2],rbuf[3],rbuf[4],rbuf[5],rbuf[6],rbuf[7],rbuf[8],rbuf[9],rbuf[10],
		rbuf[11],rbuf[12],rbuf[13],rbuf[14],rbuf[15],rbuf[16],rbuf[17]);
	return scnprintf(buf,PAGE_SIZE,"[0]%x;[1]%x;[30]%x;[31]%x;[32]%x;[33]%x;[34]%x;[35]%x;[36]%x;[37]%x;[38]%x;[39]%x;[3a]%x;[3b]%x;[3c]%x;[3d]%d;[3e]%x;[3f]%x \n", 
		rbuf[0],rbuf[1],rbuf[2],rbuf[3],rbuf[4],rbuf[5],rbuf[6],rbuf[7],rbuf[8],rbuf[9],rbuf[10],
		rbuf[11],rbuf[12],rbuf[13],rbuf[14],rbuf[15],rbuf[16],rbuf[17]);
}

static ssize_t aw2013_set_reg(struct device * cd,struct device_attribute * attr,
										const char * buf,size_t len)
{
	return 0;
}
//begin-Bee-20140417
static int aw2013_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	printk(KERN_ERR "%s", __func__);
	
	err = device_create_file(dev, &dev_attr_led);
	err = device_create_file(dev, &dev_attr_reg);
	return err;
}

static void aw2013_led_set(unsigned int lid, int level)
{
	unsigned int id = lid;//// 0:blue  1:green 2:red
	char buf[2];
	
	printk(KERN_ERR "gn_led %s lid = %d ; level = %d \n",__func__, lid, level);
	if(level){
		SET_BIT(LED_ON_FLAG, id);
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x01;  //max current 5mA
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);
		
		buf[0]=0x30;
		//buf[1]=1<<id;
		buf[1]=LED_ON_FLAG;
		breathlight_master_send(buf,2);
	}else{
		CLR_BIT(LED_ON_FLAG, id);
		buf[0]=0x30;
		//buf[1]=0<<id;
		buf[1]=LED_ON_FLAG;
		breathlight_master_send(buf,2);
	}
}
static void aw2013_blink_set(unsigned int lid , unsigned long delay_on, unsigned long delay_off)
{
	unsigned int id = lid;
	char buf[2];

	printk(KERN_ERR "gn_led %s lid=%u; delay_on=%lu; delay_off=%lu\n",__func__,lid,delay_on,delay_off);
	printk(KERN_ERR "gn_led %s blink=%d\n",__func__,cust_aw2013_led_list[lid].data.blink);


	if(cust_aw2013_led_list[lid].data.blink) {
		cust_aw2013_led_list[lid].data.level = 255;
		SET_BIT(LED_ON_FLAG, id);
		buf[0]=0x00;
		buf[1]=0x54;/////reset led module
		breathlight_master_send(buf,2);

		buf[0]=0x01;
		buf[1]=0x01;
		breathlight_master_send(buf,2);

		buf[0]=0x31+id;
		buf[1]=0x71;
		breathlight_master_send(buf,2);

		buf[0]=0x34+id;
		buf[1]=0xff;
		breathlight_master_send(buf,2);

		buf[0]=0x37+id*3;
		buf[1]=0x22;
		breathlight_master_send(buf,2);

		buf[0]=0x38+id*3;
		buf[1]=0x24;
		breathlight_master_send(buf,2);

		buf[0]=0x39+id*3;
		buf[1]=0x00;
		breathlight_master_send(buf,2);

		buf[0]=0x30;
		//buf[1]=1<<id;
		buf[1]=LED_ON_FLAG;
		breathlight_master_send(buf,2);
	}else {
		//cust_aw2013_led_list[lid].data.level = 0;
		CLR_BIT(LED_ON_FLAG, id);
		buf[0]=0x30;
		//buf[1]=0<<id;
		buf[1]=LED_ON_FLAG;
		breathlight_master_send(buf,2);
	}
}

static void aw2013_dispatch_work(struct work_struct *data)
{
	int i;
	printk(KERN_ERR "gn_led %s \n",__func__);
	for(i=0;i<AW2013_LED_TYPE_TOTAL;i++) {
		if(cust_aw2013_led_list[i].data.level_change) {
			cust_aw2013_led_list[i].data.level_change = false;
			aw2013_led_set(i,cust_aw2013_led_list[i].data.level);
		}
		if(cust_aw2013_led_list[i].data.blink_change) {
			cust_aw2013_led_list[i].data.blink_change = false;
			aw2013_blink_set(i, cust_aw2013_led_list[i].data.delay_on, cust_aw2013_led_list[i].data.delay_off);
		}
	}
	return;
	
}

static void aw2013_blue_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{
	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[BLUE_LED_ID].data;
	printk(KERN_ERR "gn_led %s, level=%d; new level=%d \n",__func__,led_data->level, level);
	if(led_data->level != level) {
		led_data->level = level;
		led_data->level_change = true;
	}

	schedule_work(&aw2013_work); 
	//aw2013_led_set(BLUE_LED_ID,level);
	return;
}
static int  aw2013_blue_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[BLUE_LED_ID].data;
	printk(KERN_ERR "gn_led %s delay_on=%lu; delay_off = %lu; new delay_on=%lu; delay_off=%lu \n",__func__,
			led_data->delay_on,led_data->delay_off,*delay_on,*delay_off);

	if((led_data->delay_on != *delay_on) || led_data->delay_off != *delay_off)
		led_data->blink_change = true;
	else
		led_data->blink_change = false;
	
	led_data->delay_off = *delay_off;
	led_data->delay_on = *delay_on;
	if((led_data->delay_on > 0) && (led_data->delay_off > 0))
		led_data->blink = 1;
	else if(!led_data->delay_on && !led_data->delay_off)
		led_data->blink = 0;


	schedule_work(&aw2013_work); 
	//aw2013_blink_set(BLUE_LED_ID, *delay_on, *delay_off );
	return 0;
}

static void aw2013_green_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{

	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[GREEN_LED_ID].data;
	printk(KERN_ERR "gn_led %s level=%d , new level=%d  \n",__func__,led_data->level,level);
	
	if(led_data->level != level) {
		led_data->level = level;
		led_data->level_change = true;
	}
	
	schedule_work(&aw2013_work); 
	//aw2013_led_set(GREEN_LED_ID, level);
	return;
}

static int  aw2013_green_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[GREEN_LED_ID].data;
	printk(KERN_ERR "gn_led %s delay_on=%lu; delay_off = %lu; new delay_on=%lu; delay_off=%lu \n",__func__,
			led_data->delay_on,led_data->delay_off,*delay_on,*delay_off);

	if((led_data->delay_on != *delay_on) || led_data->delay_off != *delay_off)
		led_data->blink_change = true;
	else
		led_data->blink_change = false;
	
	led_data->delay_off = *delay_off;
	led_data->delay_on = *delay_on;
	if((led_data->delay_on > 0) && (led_data->delay_off > 0))
		led_data->blink = 1;
	else if(!led_data->delay_on && !led_data->delay_off)
		led_data->blink = 0;


	schedule_work(&aw2013_work); 
	//aw2013_blink_set(GREEN_LED_ID, *delay_on, *delay_off);
	return 0;
}

static void aw2013_red_led_set(struct led_classdev *led_cdev, enum led_brightness level)
{
	
	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[RED_LED_ID].data;
	printk(KERN_ERR "gn_led %s level=%d , new level=%d  \n",__func__,led_data->level,level);

	if(led_data->level != level) {
		led_data->level = level;
		led_data->level_change = true;
	}

	schedule_work(&aw2013_work); 

	//aw2013_led_set(RED_LED_ID, level);
	return;
}

static int  aw2013_red_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct aw2013_led_data *led_data;
	led_data = &cust_aw2013_led_list[RED_LED_ID].data;
	printk(KERN_ERR "gn_led %s delay_on=%lu; delay_off = %lu; new delay_on=%lu; delay_off=%lu \n",__func__,
			led_data->delay_on,led_data->delay_off,*delay_on,*delay_off);
	
	if((led_data->delay_on != *delay_on) || led_data->delay_off != *delay_off)
		led_data->blink_change = true;
	else
		led_data->blink_change = false;
	
	led_data->delay_off = *delay_off;
	led_data->delay_on = *delay_on;
	if((led_data->delay_on > 0) && (led_data->delay_off > 0))
		led_data->blink = 1;
	else if(!led_data->delay_on && !led_data->delay_off)
		led_data->blink = 0;
	schedule_work(&aw2013_work);  
	//aw2013_blink_set(RED_LED_ID, *delay_on, *delay_off);
	return 0;
}

static struct cust_aw2013_led cust_aw2013_led_list[AW2013_LED_TYPE_TOTAL] = {
{"blue",    aw2013_blue_led_set,    aw2013_blue_blink_set,  {0}},
{"green",   aw2013_green_led_set,	aw2013_green_blink_set, {0}},
{"red",		aw2013_red_led_set,		aw2013_red_blink_set,   {0}}, 
};



static int  aw2013_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	int ret;
	int i;
	
	struct led_classdev *lcdev[AW2013_LED_TYPE_TOTAL];
	
	printk(KERN_ERR "aw2013_i2c_probe:OK");

	aw2013_i2c_client = client;
	DBG_PRINT("******************  aw2013_i2c_probe addr %x   " , aw2013_i2c_client->addr   );

	//Suspend_led();  // case I2C ERROR when bootup
	for(i=0;i<AW2013_LED_TYPE_TOTAL;i++) {
		lcdev[i] = kzalloc(sizeof(struct led_classdev), GFP_KERNEL);
		lcdev[i]->name = cust_aw2013_led_list[i].name;
		lcdev[i]->brightness = LED_OFF;
		lcdev[i]->brightness_set = cust_aw2013_led_list[i].brightness_set;
		lcdev[i]->blink_set = cust_aw2013_led_list[i].blink_set;
		ret = led_classdev_register(&client->dev,lcdev[i]);
	}
	INIT_WORK(&aw2013_work,aw2013_dispatch_work);
	aw2013_create_sysfs(client);
	//Gionee BSP1 yaoyc 20150708 add for CR01514196 begin
	led_off_aw2013();
	//Gionee BSP1 yaoyc 20150708 add for CR01514196 end
	return 0;
}

static int  aw2013_i2c_remove(struct i2c_client *client)
{
	aw2013_i2c_client = NULL;
	return 0;
}

static const struct i2c_device_id AW2013_i2c_id[] = {
	{ "aw2013", 0 },
	{ }
};



//MODULE_DEVICE_TABLE(i2c, AW2013_i2c_id);



//static struct i2c_board_info __initdata aw2013_i2c_hw={ I2C_BOARD_INFO("AW2013", AW2013_I2C_ADDR)};

static struct i2c_driver aw2013_i2c_driver = {
        .driver = 
	   {
            //    .owner  = THIS_MODULE,
                .name   = "AW2013",
        },

        .probe          = aw2013_i2c_probe,
        .remove         = aw2013_i2c_remove,
        .id_table       = AW2013_i2c_id,
};





static int __init aw2013_driver_init(void) 
{
	int ret;


	printk(KERN_ERR "*********** aw2013_driver_init:start");


	//i2c_register_board_info(AW2013_I2C_BUS, &aw2013_i2c_hw, 1);
		 
	ret = i2c_add_driver(&aw2013_i2c_driver);

	
	printk(KERN_ERR "aw2013_driver_init:end \n");

	return 0;
}



/* should never be called */
static void __exit aw2013_driver_exit(void) 
{

		i2c_del_driver(&aw2013_i2c_driver);
}

module_init(aw2013_driver_init);
module_exit(aw2013_driver_exit);
MODULE_DESCRIPTION("Linux HW direct control driver");
MODULE_AUTHOR("David.wang(softnow@live.cn)");
MODULE_LICENSE("GPL");

