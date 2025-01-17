/* drivers/input/touchscreen/gt1x.c
 *
 * 2010 - 2017 Goodix Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * Version: 1.6
 */
 
#include "gt1x_generic.h"
#ifdef CONFIG_GTP_TYPE_B_PROTOCOL
#include <linux/input/mt.h>
#endif

static struct input_dev *input_dev;
static spinlock_t irq_lock;
static int irq_disabled;

struct goodix_pinctrl *gt_pinctrl;

#ifdef CONFIG_OF
static struct regulator *vdd_ana =NULL;
static struct regulator *vcc_i2c =NULL;
int gt1x_rst_gpio;
int gt1x_pwr_gpio;
int gt1x_int_gpio;
int gt1x_pwrio_gpio;
#endif

#if defined(CONFIG_DRM_PANEL)
static struct drm_panel *active_panel;
#endif

static int gt1x_register_powermanger(void);
static int gt1x_unregister_powermanger(void);

/**
 * gt1x_i2c_write - i2c write.
 * @addr: register address.
 * @buffer: data buffer.
 * @len: the bytes of data to write.
 *Return: 0: success, otherwise: failed
 */
s32 gt1x_i2c_write(u16 addr, u8 * buffer, s32 len)
{
	struct i2c_msg msg = {
		.flags = 0,
		.addr = gt1x_i2c_client->addr,
	};
	return _do_i2c_write(&msg, addr, buffer, len);
}

/**
 * gt1x_i2c_read - i2c read.
 * @addr: register address.
 * @buffer: data buffer.
 * @len: the bytes of data to write.
 *Return: 0: success, otherwise: failed
 */
s32 gt1x_i2c_read(u16 addr, u8 * buffer, s32 len)
{
	u8 addr_buf[GTP_ADDR_LENGTH] = { (addr >> 8) & 0xFF, addr & 0xFF };
	struct i2c_msg msgs[2] = {
		{
		 .addr = gt1x_i2c_client->addr,
		 .flags = 0,
		 .buf = addr_buf,
		 .len = GTP_ADDR_LENGTH},
		{
		 .addr = gt1x_i2c_client->addr,
		 .flags = I2C_M_RD}
	};
	return _do_i2c_read(msgs, addr, buffer, len);
}

/**
 * gt1x_irq_enable - enable irq function.
 *
 */
void gt1x_irq_enable(void)
{
	unsigned long irqflags = 0;
    //GTP_ERROR("gt1x_irq_enable Enter\n");
	spin_lock_irqsave(&irq_lock, irqflags);
	if (irq_disabled) {
		irq_disabled = 0;
		spin_unlock_irqrestore(&irq_lock, irqflags);
		enable_irq(gt1x_i2c_client->irq);
	} else {
		spin_unlock_irqrestore(&irq_lock, irqflags);
	}
    //GTP_ERROR("gt1x_irq_enable Leave\n");
}

/**
 * gt1x_irq_enable - disable irq function.
 *  disable irq and wait bottom half
 *  thread(gt1x_ts_work_thread)
 */
void gt1x_irq_disable(void)
{
	unsigned long irqflags;

	/* because there is an irq enable action in
	 * the bottom half thread, we need to wait until
	 * bottom half thread finished.
	 */
    //GTP_ERROR("gt1x_irq_disable Enter\n");

	synchronize_irq(gt1x_i2c_client->irq);
	spin_lock_irqsave(&irq_lock, irqflags);
	if (!irq_disabled) {
		irq_disabled = 1;
		spin_unlock_irqrestore(&irq_lock, irqflags);
		disable_irq(gt1x_i2c_client->irq);
	} else {
		spin_unlock_irqrestore(&irq_lock, irqflags);
	}
    //GTP_ERROR("gt1x_irq_disable Leave\n");
}

#ifndef CONFIG_OF
int gt1x_power_switch(s32 state)
{
    return 0;
}
#endif

int gt1x_debug_proc(u8 * buf, int count)
{
	return -1;
}

#ifdef CONFIG_GTP_CHARGER_SWITCH
u32 gt1x_get_charger_status(void)
{
	/* 
	 * Need to get charger status of
	 * your platform.
	 */
	 return 0;
}
#endif

/**
 * gt1x_ts_irq_handler - External interrupt service routine 
 *		for interrupt mode.
 * @irq:  interrupt number.
 * @dev_id: private data pointer.
 * Return: Handle Result.
 *  		IRQ_WAKE_THREAD: top half work finished,
 *  		wake up bottom half thread to continue the rest work.
 */
static irqreturn_t gt1x_ts_irq_handler(int irq, void *dev_id)
{
	unsigned long irqflags;

	/* irq top half, use nosync irq api to 
	 * disable irq line, if irq is enabled,
	 * then wake up bottom half thread */
	spin_lock_irqsave(&irq_lock, irqflags);
	if (!irq_disabled) {
		irq_disabled = 1;
		spin_unlock_irqrestore(&irq_lock, irqflags);
		disable_irq_nosync(gt1x_i2c_client->irq);
		return IRQ_WAKE_THREAD;
	} else {
		spin_unlock_irqrestore(&irq_lock, irqflags);
		return IRQ_HANDLED;
	}
}

/**
 * gt1x_touch_down - Report touch point event .
 * @id: trackId
 * @x:  input x coordinate
 * @y:  input y coordinate
 * @w:  input pressure
 * Return: none.
 */
void gt1x_touch_down(s32 x, s32 y, s32 size, s32 id)
{
#ifdef CONFIG_GTP_CHANGE_X2Y
	GTP_SWAP(x, y);
#endif

	input_report_key(input_dev, BTN_TOUCH, 1);
#ifdef CONFIG_GTP_TYPE_B_PROTOCOL
	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
#else
	input_report_abs(input_dev, ABS_MT_TRACKING_ID, id);
#endif

	input_report_abs(input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(input_dev, ABS_MT_PRESSURE, size);
	input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, size);

#ifndef CONFIG_GTP_TYPE_B_PROTOCOL
	input_mt_sync(input_dev);
#endif
}

/**
 * gt1x_touch_up -  Report touch release event.
 * @id: trackId
 * Return: none.
 */
void gt1x_touch_up(s32 id)
{
#ifdef CONFIG_GTP_TYPE_B_PROTOCOL
	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
#else
	input_mt_sync(input_dev);
#endif
}

/**
 * gt1x_ts_work_thread - Goodix touchscreen work function.
 * @iwork: work struct of gt1x_workqueue.
 * Return: none.
 */
static irqreturn_t gt1x_ts_work_thread(int irq, void *data)
{
	u8 point_data[11] = { 0 };
	u8 end_cmd = 0;
	u8 finger = 0;
	s32 ret = 0;

    if (update_info.status) {
        GTP_DEBUG("Ignore interrupts during fw update.");
        return IRQ_HANDLED;
    }

#ifdef CONFIG_GTP_GESTURE_WAKEUP
	ret = gesture_event_handler(input_dev);
	if (ret >= 0)
		goto exit_work_func;
#endif

	if (gt1x_halt) {
		GTP_DEBUG("Ignore interrupts after suspend");
        return IRQ_HANDLED;
	}

	ret = gt1x_i2c_read(GTP_READ_COOR_ADDR, point_data, sizeof(point_data));
	if (ret < 0) {
		GTP_ERROR("I2C transfer error!");
#ifndef CONFIG_GTP_ESD_PROTECT
		gt1x_power_reset();
#endif
		goto exit_work_func;
	}

	finger = point_data[0];
	if (finger == 0x00)
		gt1x_request_event_handler();

	if ((finger & 0x80) == 0) {
#ifdef CONFIG_HOTKNOT_BLOCK_RW
		if (!hotknot_paired_flag)
#endif
		{
			goto exit_eint;
		}
	}

#ifdef CONFIG_HOTKNOT_BLOCK_RW
	ret = hotknot_event_handler(point_data);
	if (!ret)
		goto exit_work_func;
#endif

#ifdef CONFIG_GTP_PROXIMITY
	ret = gt1x_prox_event_handler(point_data);
	if (ret > 0)
		goto exit_work_func;
#endif

#ifdef CONFIG_GTP_WITH_STYLUS
	ret = gt1x_touch_event_handler(point_data, input_dev, pen_dev);
#else
	ret = gt1x_touch_event_handler(point_data, input_dev, NULL);
#endif

exit_work_func:
	if (!gt1x_rawdiff_mode && (ret >= 0 || ret == ERROR_VALUE)) {
		ret = gt1x_i2c_write(GTP_READ_COOR_ADDR, &end_cmd, 1);
		if (ret < 0)
			GTP_ERROR("I2C write end_cmd  error!");
	}
exit_eint:
    gt1x_irq_enable();
	return IRQ_HANDLED;
}

/* 
 * Devices Tree support, 
*/
#ifdef CONFIG_OF
/**
 * gt1x_parse_dt - parse platform infomation form devices tree.
 */
static int gt1x_parse_dt(struct device *dev)
{
	struct device_node *np;
    int ret = 0;

    if (!dev)
        return -ENODEV;
    
    np = dev->of_node;
	gt1x_int_gpio = of_get_named_gpio(np, "goodix,irq-gpio", 0);
	gt1x_rst_gpio = of_get_named_gpio(np, "goodix,rst-gpio", 0);
	gt1x_pwr_gpio = of_get_named_gpio(np, "goodix,pwr-gpio", 0);
	gt1x_pwrio_gpio = of_get_named_gpio(np, "goodix,pwr-gpioio", 0);

    if (!gpio_is_valid(gt1x_int_gpio) || !gpio_is_valid(gt1x_rst_gpio))// ||!gpio_is_valid(gt1x_pwr_gpio)) 
    {
        GTP_ERROR("Invalid GPIO, irq-gpio:%d, rst-gpio:%d",
            gt1x_int_gpio, gt1x_rst_gpio);
        return -EINVAL;
    }

    vdd_ana = regulator_get(dev, "vdd_ana");
    if (IS_ERR(vdd_ana)) {
	    GTP_ERROR("regulator get of vdd_ana failed");
	    ret = PTR_ERR(vdd_ana);
	    vdd_ana = NULL;
	    //return ret;
    }
   else
   {//disable first 
	    ret = regulator_disable(vdd_ana);
	    if (ret) {
	        GTP_ERROR("Failed to disable VDD33 vdd_ana\n");
	    }
	    regulator_set_load(vdd_ana,0);
   }
	vcc_i2c = regulator_get(dev, "vcc_i2c");
    if (IS_ERR(vcc_i2c)) {
	    GTP_ERROR("regulator get of vcc_i2c failed");
	    ret = PTR_ERR(vcc_i2c);
	    vcc_i2c = NULL;
	   // return ret;
    }

    return 0;
}

/**
 * goodix_pinctrl_init - pinctrl init
 */
static int goodix_pinctrl_init(struct i2c_client *client)
{
	int ret = 0;
	
	if(gt_pinctrl == NULL){
		gt_pinctrl =(struct goodix_pinctrl*)kmalloc(sizeof(struct goodix_pinctrl),GFP_KERNEL);
		if (!gt_pinctrl)
                return -ENOMEM;
	}

	gt_pinctrl->ts_pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(gt_pinctrl->ts_pinctrl)) {
		GTP_ERROR("Failed to get pinctrl");
		ret = PTR_ERR(gt_pinctrl->ts_pinctrl);
		gt_pinctrl->ts_pinctrl= NULL;
		return ret;
	}

	gt_pinctrl->pinctrl_wakeup = pinctrl_lookup_state(gt_pinctrl->ts_pinctrl, "pmx_ts_wakeup");
	if (IS_ERR_OR_NULL(gt_pinctrl->pinctrl_wakeup)) {
		GTP_ERROR("Pin state[wakeup] not found");
		ret = PTR_ERR(gt_pinctrl->pinctrl_wakeup);
		goto exit_put;
	}

	gt_pinctrl->pinctrl_normal = pinctrl_lookup_state(gt_pinctrl->ts_pinctrl, "pmx_ts_normal");
	if (IS_ERR_OR_NULL(gt_pinctrl->pinctrl_normal)) {
		GTP_ERROR("Pin state[normal] not found");
		ret = PTR_ERR(gt_pinctrl->pinctrl_normal);
	}

	gt_pinctrl->pinctrl_poweroff = pinctrl_lookup_state(gt_pinctrl->ts_pinctrl, "pmx_ts_poweroff");
	if (IS_ERR_OR_NULL(gt_pinctrl->pinctrl_poweroff)) {
		GTP_ERROR("Pin state[poweroff] not found");
		ret = PTR_ERR(gt_pinctrl->pinctrl_poweroff);
		goto exit_put;
	}

	gt_pinctrl->pinctrl_sleep = pinctrl_lookup_state(gt_pinctrl->ts_pinctrl, "pmx_ts_sleep");
	if (IS_ERR_OR_NULL(gt_pinctrl->pinctrl_sleep)) {
		GTP_ERROR("Pin state[sleep] not found");
		ret = PTR_ERR(gt_pinctrl->pinctrl_sleep);
		goto exit_put;
	}
	
	return 0;
exit_put:
	devm_pinctrl_put(gt_pinctrl->ts_pinctrl);
	gt_pinctrl->ts_pinctrl = NULL;
	gt_pinctrl->pinctrl_wakeup = NULL;
	gt_pinctrl->pinctrl_normal = NULL;
	gt_pinctrl->pinctrl_poweroff = NULL;
	gt_pinctrl->pinctrl_sleep = NULL;
	return ret;
}


/**
 * gt1x_power_switch - power switch .
 * @on: 1-switch on, 0-switch off.
 * return: 0-succeed, -1-faileds
 */
int gt1x_power_switch(int on)
{
	//static int vdd_count = 0;
	int ret;
	struct i2c_client *client = gt1x_i2c_client;

    if (!client)// || !vdd_ana)
        return -1;
	GTP_ERROR("gt1x_power_switch on=%d,by eric.wang\n",on);
	if (on) {
		GTP_DEBUG("gt1x_power_switch power on.");
		GTP_GPIO_OUTPUT(gt1x_pwr_gpio,1);
		GTP_GPIO_OUTPUT(gt1x_pwrio_gpio,1);//by eric.wang
		if(vdd_ana!=NULL)
		{
			ret = regulator_enable(vdd_ana);
			//vdd_count++;
		}
	} else {
		GTP_DEBUG("gt1x_power_switch power off.");
		GTP_GPIO_OUTPUT(gt1x_pwr_gpio,0);
		GTP_GPIO_OUTPUT(gt1x_pwrio_gpio,0);
		if(vdd_ana!=NULL)
		{
			ret = regulator_disable(vdd_ana);
			regulator_set_load(vdd_ana,0);
			//vdd_count--;
		}
		
	}

	usleep_range(10000, 10100);
	return ret;

}

int gt1x_vcc_i2c_switch(int on)
{

	int ret;
	struct i2c_client *client = gt1x_i2c_client;

    if (!client )//|| !vcc_i2c)
        return -1;
	GTP_ERROR("gt1x_vcc_i2c_switch on=%d,by eric.wang\n",on);
	if (on) {

		GTP_GPIO_OUTPUT(gt1x_pwrio_gpio,1);
		GTP_DEBUG("gt1x_vcc_i2c_switch power on.");
		if(vcc_i2c!=NULL)
		ret = regulator_enable(vcc_i2c);
	} else {
		GTP_GPIO_OUTPUT(gt1x_pwrio_gpio,0);
		GTP_DEBUG("gt1x_vcc_i2c_switch power off.");
		if(vcc_i2c!=NULL)
		ret = regulator_disable(vcc_i2c);
	}
	
	usleep_range(10000, 10100);
	return ret;
	
}

#endif

static void gt1x_release_resource(void)
{
    if (gpio_is_valid(GTP_INT_PORT)) {
		gpio_direction_input(GTP_INT_PORT);
		gpio_free(GTP_INT_PORT);
	}

    if (gpio_is_valid(GTP_RST_PORT)) {
		gpio_direction_output(GTP_RST_PORT, 0);
		gpio_free(GTP_RST_PORT);
	}

#ifdef CONFIG_OF      
	if (vdd_ana) {
		gt1x_power_switch(SWITCH_OFF);
        	regulator_put(vdd_ana);
		vdd_ana = NULL;
	}
#endif

	if (gt_pinctrl->ts_pinctrl)
		devm_pinctrl_put(gt_pinctrl->ts_pinctrl);
	gt_pinctrl->ts_pinctrl = NULL;
	gt_pinctrl->pinctrl_wakeup = NULL;
	gt_pinctrl->pinctrl_normal = NULL;
	gt_pinctrl->pinctrl_poweroff = NULL;
	gt_pinctrl->pinctrl_sleep = NULL;

	if (input_dev) {
		input_unregister_device(input_dev);
		input_dev = NULL;
	}
}

/**
 * gt1x_request_gpio - Request gpio(INT & RST) ports.
 */
static s32 gt1x_request_gpio(void)
{
	s32 ret = 0;

	ret = gpio_request(GTP_INT_PORT, "GTP_INT_IRQ");
	if (ret < 0) {
		GTP_ERROR("Failed to request GPIO:%d, ERRNO:%d", (s32) GTP_INT_PORT, ret);
		ret = -ENODEV;
	} else {
		GTP_GPIO_AS_INT(GTP_INT_PORT);
		gt1x_i2c_client->irq = gpio_to_irq(GTP_INT_PORT);
	}

	ret = gpio_request(GTP_RST_PORT, "GTP_RST_PORT");
	if (ret < 0) {
		GTP_ERROR("Failed to request GPIO:%d, ERRNO:%d", (s32) GTP_RST_PORT, ret);
		ret = -ENODEV;
	}

	GTP_GPIO_AS_INPUT(GTP_RST_PORT);

	ret = gpio_request(gt1x_pwr_gpio, "gt1x_pwr_gpio");
	if (ret < 0) {
		GTP_ERROR("Failed to request gt1x_pwr_gpio:%d, ERRNO:%d,by eric.wang\n", (s32) gt1x_pwr_gpio, ret);
		ret = -ENODEV;
	}
	
	ret = gpio_request(gt1x_pwrio_gpio, "gt1x_pwrio_gpio");
	if (ret < 0) {
		GTP_ERROR("Failed to request gt1x_pwrio_gpio:%d, ERRNO:%d,by eric.wang\n", (s32) gt1x_pwrio_gpio, ret);
		ret = -ENODEV;
	}

	return ret;
}

/**
 * gt1x_request_irq - Request interrupt.
 * Return
 *      0: succeed, -1: failed.
 */
static s32 gt1x_request_irq(void)
{
	s32 ret = -1;
	const u8 irq_table[] = GTP_IRQ_TAB;

	GTP_DEBUG("INT trigger type:%x", gt1x_int_type);
	ret = devm_request_threaded_irq(&gt1x_i2c_client->dev,
			gt1x_i2c_client->irq,
			gt1x_ts_irq_handler,
			gt1x_ts_work_thread,
			irq_table[gt1x_int_type],
			gt1x_i2c_client->name,
			gt1x_i2c_client);
	if (ret) {
		GTP_ERROR("Request IRQ failed!ERRNO:%d.", ret);
		return -1;
	} else {
		gt1x_irq_disable();
		return 0;
	}
}

/**
 * gt1x_request_input_dev -  Request input device Function.
 * Return
 *      0: succeed, -1: failed.
 */
static s8 gt1x_request_input_dev(void)
{
	s8 ret = -1;
#ifdef CONFIG_GTP_HAVE_TOUCH_KEY
	u8 index = 0;
#endif

	input_dev = input_allocate_device();
	if (input_dev == NULL) {
		GTP_ERROR("Failed to allocate input device.");
		return -ENOMEM;
	}

	input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
#ifdef CONFIG_GTP_TYPE_B_PROTOCOL
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 0))
    input_mt_init_slots(input_dev, GTP_MAX_TOUCH, INPUT_MT_DIRECT);
#else
    input_mt_init_slots(input_dev, GTP_MAX_TOUCH); 
#endif
#endif
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

#ifdef CONFIG_GTP_HAVE_TOUCH_KEY
	for (index = 0; index < GTP_MAX_KEY_NUM; index++)
		input_set_capability(input_dev, EV_KEY, gt1x_touch_key_array[index]);
#endif

#ifdef CONFIG_GTP_GESTURE_WAKEUP
	input_set_capability(input_dev, EV_KEY, KEY_GES_REGULAR);
    input_set_capability(input_dev, EV_KEY, KEY_GES_CUSTOM);
#endif

#ifdef CONFIG_GTP_CHANGE_X2Y
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, gt1x_abs_y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, gt1x_abs_x_max, 0, 0);
#else
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, gt1x_abs_x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, gt1x_abs_y_max, 0, 0);
#endif
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

	input_dev->name = "goodix-ts";
	input_dev->phys = "input/ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->id.vendor = 0xDEAD;
	input_dev->id.product = 0xBEEF;
	input_dev->id.version = 10427;

	ret = input_register_device(input_dev);
	if (ret) {
		GTP_ERROR("Register %s input device failed", input_dev->name);
		return -ENODEV;
	}

	return 0;
}

#if defined(CONFIG_DRM_PANEL)
static int gtp_ts_check_dt(struct device_node *np)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			active_panel = panel;
			GT_LOG(" %s:find\n", __func__);
			return 0;
		}
	}

	GT_ERR(" %s: not find\n", __func__);
	return -ENODEV;
}

static int gtp_ts_check_default_tp(struct device_node *dt, const char *prop)
{
	const char **active_tp = NULL;
	int count, tmp, score = 0;
	const char *active;
	int ret, i;

	count = of_property_count_strings(dt->parent, prop);
	if (count <= 0 || count > 3)
		return -ENODEV;

	active_tp = kcalloc(count, sizeof(char *),  GFP_KERNEL);
	if (!active_tp) {
		GT_ERR("FTS alloc failed\n");
		return -ENOMEM;
	}

	ret = of_property_read_string_array(dt->parent, prop,
			active_tp, count);
	if (ret < 0) {
		GT_ERR("fail to read %s %d\n", prop, ret);
		ret = -ENODEV;
		goto out;
	}

	for (i = 0; i < count; i++) {
		active = active_tp[i];
		if (active != NULL) {
			tmp = of_device_is_compatible(dt, active);
			if (tmp > 0)
				score++;
		}
	}

	if (score <= 0) {
		GT_ERR("not match this driver\n");
		ret = -ENODEV;
		goto out;
	}
	ret = 0;
out:
	kfree(active_tp);
	return ret;
}
#endif

/**
 * gt1x_ts_probe -   I2c probe.
 * @client: i2c device struct.
 * @id: device id.
 * Return  0: succeed, <0: failed.
 */
static int gt1x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = -1;
#if defined(CONFIG_DRM)
		struct device_node *dp = client->dev.of_node;
#endif
	
		//GT_LOG("start\n");
	
#if defined(CONFIG_DRM)
		if (gtp_ts_check_dt(dp)) {
			if (!gtp_ts_check_default_tp(dp, "qcom,i2c-touch-active"))
				ret = -EPROBE_DEFER;
			else
				ret = -ENODEV;
	
			return ret;
		}
#endif

	/* do NOT remove these logs */
	GTP_INFO("GTP Driver Version: %s,slave addr:%02xh",
			GTP_DRIVER_VERSION, client->addr);

	gt1x_i2c_client = client;
	spin_lock_init(&irq_lock);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		GTP_ERROR("I2C check functionality failed.");
		return -ENODEV;
	}

#ifdef CONFIG_OF	/* device tree support */
	if (client->dev.of_node) {
		ret = gt1x_parse_dt(&client->dev);
		if (ret < 0)
			return -EINVAL;
	}
#else
#error [GOODIX]only support devicetree platform
#endif

	/* init pinctrl states */
	ret  = goodix_pinctrl_init(client);
	if (ret < 0) {
		GTP_ERROR("Init pinctrl states failed.");
		goto exit_clean;
	}

	/* Set pin state as poweroff */
	if ( gt_pinctrl->ts_pinctrl && gt_pinctrl->pinctrl_poweroff)
		ret = pinctrl_select_state(gt_pinctrl->ts_pinctrl, gt_pinctrl->pinctrl_poweroff);
	if (ret < 0) {
		GTP_ERROR("Set pin state as poweroff error: %d", ret);
		goto exit_clean;
	}

	/* gpio resource */
	ret = gt1x_request_gpio();
	if (ret < 0) {
		GTP_ERROR("GTP request IO port failed.");
		goto exit_clean;
	}

    /* power on */
	ret = gt1x_power_switch(SWITCH_ON);
	if (ret < 0) {
		GTP_ERROR("Power on failed");
		goto exit_clean;
	}

    /* reset ic & do i2c test */
	ret = gt1x_reset_guitar();
	if (ret != 0) {
		ret = gt1x_power_switch(SWITCH_OFF);
		if (ret < 0)
			goto exit_clean;
		ret = gt1x_power_switch(SWITCH_ON);
		if (ret < 0)
			goto exit_clean;
		ret = gt1x_reset_guitar(); /* retry */
		if (ret != 0) {
			GTP_ERROR("Reset guitar failed!");
			goto exit_clean;
		}
	}

	/* check firmware, initialize and send
	 * chip configuration data, initialize nodes */
	gt1x_init();

	ret = gt1x_request_input_dev();
	if (ret < 0)
		goto err_input;

	ret = gt1x_request_irq();
	if (ret < 0)
		goto err_irq;

#ifdef CONFIG_GTP_ESD_PROTECT
	/* must before auto update */
	gt1x_init_esd_protect();
	gt1x_esd_switch(SWITCH_ON);
#endif

#ifdef CONFIG_GTP_AUTO_UPDATE
	do {
		struct task_struct *thread = NULL;
		thread = kthread_run(gt1x_auto_update_proc,
				(void *)NULL,
				"gt1x_auto_update");
		if (IS_ERR(thread))
			GTP_ERROR("Failed to create auto-update thread: %d.", ret);
	} while (0);
#endif

	gt1x_register_powermanger();
	gt1x_irq_enable();
	return 0;

err_irq:
err_input:
	gt1x_deinit();
exit_clean:
	gt1x_release_resource();
	GTP_ERROR("GTP probe failed:%d", ret);
	return -ENODEV;
}

/**
 * gt1x_ts_remove -  Goodix touchscreen driver release function.
 * @client: i2c device struct.
 * Return  0: succeed, -1: failed.
 */
static int gt1x_ts_remove(struct i2c_client *client)
{
	GTP_INFO("GTP driver removing...");
	gt1x_unregister_powermanger();

    gt1x_deinit();
    gt1x_release_resource();

    return 0;
}

#if defined(CONFIG_DRM_PANEL)
static struct notifier_block gt1x_drm_notifier;

static int gtp_drm_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank;
	//struct nvt_ts_data *ts =
	//	container_of(self, struct nvt_ts_data, drm_notif);

	if (!evdata || !evdata->data  )
		return 0;

	blank = evdata->data;

	if (event == DRM_PANEL_EARLY_EVENT_BLANK) {
		if (*blank == DRM_PANEL_BLANK_POWERDOWN) {
			GT_LOG("event=%lu, *blank=%d\n", event, *blank);
			gt1x_suspend();			
		}
	} else if (event == DRM_PANEL_EVENT_BLANK) {
		if (*blank == DRM_PANEL_BLANK_UNBLANK) {
			GT_LOG("event=%lu, *blank=%d\n", event, *blank);			
			gt1x_resume();
		}
	}

	return 0;
}

#elif   defined(CONFIG_FB)	
/* frame buffer notifier block control the suspend/resume procedure */
static struct notifier_block gt1x_fb_notifier;

static int gtp_fb_notifier_callback(struct notifier_block *noti, unsigned long event, void *data)
{
	struct fb_event *ev_data = data;
	int *blank;

#ifdef CONFIG_GTP_INCELL_PANEL
#ifndef FB_EARLY_EVENT_BLANK
	#error Need add FB_EARLY_EVENT_BLANK to fbmem.c
#endif
    
	if (ev_data && ev_data->data && event == FB_EARLY_EVENT_BLANK) {
		blank = ev_data->data;
        if (*blank == FB_BLANK_UNBLANK) {
			GTP_DEBUG("Resume by fb notifier.");
			gt1x_resume();
        }
    }
#else
	if (ev_data && ev_data->data && event == FB_EVENT_BLANK) {
		blank = ev_data->data;
        if (*blank == FB_BLANK_UNBLANK) {
			GTP_DEBUG("Resume by fb notifier.");
			gt1x_resume();
        }
    }
#endif

	if (ev_data && ev_data->data && event == FB_EVENT_BLANK) {
		blank = ev_data->data;
        if (*blank == FB_BLANK_POWERDOWN) {
			GTP_DEBUG("Suspend by fb notifier.");
			gt1x_suspend();
		}
	}

	return 0;
}
#elif defined(CONFIG_PM)
/**
 * gt1x_ts_suspend - i2c suspend callback function.
 * @dev: i2c device.
 * Return  0: succeed, -1: failed.
 */
static int gt1x_pm_suspend(struct device *dev)
{
    return gt1x_suspend();
}

/**
 * gt1x_ts_resume - i2c resume callback function.
 * @dev: i2c device.
 * Return  0: succeed, -1: failed.
 */
static int gt1x_pm_resume(struct device *dev)
{
	return gt1x_resume();
}

/* bus control the suspend/resume procedure */
static const struct dev_pm_ops gt1x_ts_pm_ops = {
	.suspend = gt1x_pm_suspend,
	.resume = gt1x_pm_resume,
};

#elif defined(CONFIG_HAS_EARLYSUSPEND)
/* earlysuspend module the suspend/resume procedure */
static void gt1x_ts_early_suspend(struct early_suspend *h)
{
	gt1x_suspend();
}

static void gt1x_ts_late_resume(struct early_suspend *h)
{
	gt1x_resume();
}

static struct early_suspend gt1x_early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = gt1x_ts_early_suspend,
	.resume = gt1x_ts_late_resume,
};
#endif


static int gt1x_register_powermanger(void)
{
#if defined(CONFIG_DRM)		
		gt1x_drm_notifier.notifier_call = gtp_drm_notifier_callback;
		if (active_panel &&
			drm_panel_notifier_register(active_panel,
				&gt1x_drm_notifier) < 0) {
			GT_ERR("register notifier failed!\n");
			//goto err_register_drm_notif_failed;
		}

#elif   defined(CONFIG_FB)
	gt1x_fb_notifier.notifier_call = gtp_fb_notifier_callback;
	fb_register_client(&gt1x_fb_notifier);
	
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	register_early_suspend(&gt1x_early_suspend);
#endif	
	return 0;
}

static int gt1x_unregister_powermanger(void)
{
#if defined(CONFIG_DRM_PANEL)
	if (active_panel)
		drm_panel_notifier_unregister(active_panel, &gt1x_drm_notifier);

#elif   defined(CONFIG_FB)
	fb_unregister_client(&gt1x_fb_notifier);
		
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&gt1x_early_suspend);
#endif
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gt1x_match_table[] = {
		{.compatible = "goodix,gt1x",},
		{ },
};
#endif

static const struct i2c_device_id gt1x_ts_id[] = {
	{GTP_I2C_NAME, 0},
	{}
};

static struct i2c_driver gt1x_ts_driver = {
	.probe = gt1x_ts_probe,
	.remove = gt1x_ts_remove,
	.id_table = gt1x_ts_id,
	.driver = {
		   .name = GTP_I2C_NAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = gt1x_match_table,
#endif
#if defined(CONFIG_FB)
		   .pm = &gt1x_ts_pm_ops,
#endif
		   },
};

/**   
 * gt1x_ts_init - Driver Install function.
 * Return   0---succeed.
 */
static int __init gt1x_ts_init(void)
{
	GTP_INFO("GTP driver installing...");
	return i2c_add_driver(&gt1x_ts_driver);
}

/**   
 * gt1x_ts_exit - Driver uninstall function.
 * Return   0---succeed.
 */
static void __exit gt1x_ts_exit(void)
{
	GTP_DEBUG_FUNC();
	GTP_INFO("GTP driver exited.");
	i2c_del_driver(&gt1x_ts_driver);
}

module_init(gt1x_ts_init);
module_exit(gt1x_ts_exit);

MODULE_DESCRIPTION("GTP Series Driver");
MODULE_LICENSE("GPL v2");
