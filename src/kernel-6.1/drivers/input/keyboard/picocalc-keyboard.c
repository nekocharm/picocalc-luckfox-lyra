// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Picocalc Keyboard
 *
 * Copyright 2025 nekocharm <jumba.jookiba@outlook.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/of.h>

#define DRV_NAME    	"picocalc-keyboard"
#define PCKB_REG		0x09
#define KEYCODE_SIZE	256
#define INTERVAL    	20 //ms

#define KEY_STATE_IDLE			0
#define KEY_STATE_PRESSED		1
#define KEY_STATE_HOLD			2
#define KEY_STATE_RELEASED		3

struct picocalc_keyboard {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
	int page_key;
	int interval;
};

static unsigned short xlate[KEYCODE_SIZE] = {
	['a'] = KEY_A, ['b'] = KEY_B, ['c'] = KEY_C, ['d'] = KEY_D,
	['e'] = KEY_E, ['f'] = KEY_F, ['g'] = KEY_G, ['h'] = KEY_H,
	['i'] = KEY_I, ['j'] = KEY_J, ['k'] = KEY_K, ['l'] = KEY_L,
	['m'] = KEY_M, ['n'] = KEY_N, ['o'] = KEY_O, ['p'] = KEY_P,
	['q'] = KEY_Q, ['r'] = KEY_R, ['s'] = KEY_S, ['t'] = KEY_T,
	['u'] = KEY_U, ['v'] = KEY_V, ['w'] = KEY_W, ['x'] = KEY_X,
	['y'] = KEY_Y, ['z'] = KEY_Z,

	['A'] = KEY_A, ['B'] = KEY_B, ['C'] = KEY_C, ['D'] = KEY_D,
	['E'] = KEY_E, ['F'] = KEY_F, ['G'] = KEY_G, ['H'] = KEY_H,
	['I'] = KEY_I, ['J'] = KEY_J, ['K'] = KEY_K, ['L'] = KEY_L,
	['M'] = KEY_M, ['N'] = KEY_N, ['O'] = KEY_O, ['P'] = KEY_P,
	['Q'] = KEY_Q, ['R'] = KEY_R, ['S'] = KEY_S, ['T'] = KEY_T,
	['U'] = KEY_U, ['V'] = KEY_V, ['W'] = KEY_W, ['X'] = KEY_X,
	['Y'] = KEY_Y, ['Z'] = KEY_Z,

	['1'] = KEY_1, ['2'] = KEY_2, ['3'] = KEY_3, ['4'] = KEY_4,
	['5'] = KEY_5, ['6'] = KEY_6, ['7'] = KEY_7, ['8'] = KEY_8,
	['9'] = KEY_9, ['0'] = KEY_0,

	['!'] = KEY_1, ['@'] = KEY_2, ['#'] = KEY_3, ['$'] = KEY_4,
	['%'] = KEY_5, ['^'] = KEY_6, ['&'] = KEY_7, ['*'] = KEY_8,
	['('] = KEY_9, [')'] = KEY_0,

	[0x81] = KEY_F1, [0x82] = KEY_F2, [0x83] = KEY_F3, [0x84] = KEY_F4,
	[0x85] = KEY_F5, [0x86] = KEY_F6, [0x87] = KEY_F7, [0x88] = KEY_F8,
	[0x89] = KEY_F9, [0x90] = KEY_F10,

	[0xA1] = KEY_LEFTALT, [0xA2] = KEY_LEFTSHIFT,
	[0xA3] = KEY_RIGHTSHIFT, [0xA5] = KEY_LEFTCTRL,
	[0xB5] = KEY_UP, [0xB6] = KEY_DOWN,
	[0xB4] = KEY_LEFT, [0xB7] = KEY_RIGHT,
	[0xD6] = KEY_PAGEUP, [0xD7] = KEY_PAGEDOWN,

	[0xB1] = KEY_ESC, [0xD0] = KEY_BREAK,
	[0x09] = KEY_TAB, [0xD2] = KEY_HOME,
	[0xD4] = KEY_DELETE, [0xD5] = KEY_END,
	[0xC1] = KEY_CAPSLOCK, [0x08] = KEY_BACKSPACE, 
	[0xD1] = KEY_INSERT,

	['~'] = KEY_GRAVE, ['`'] = KEY_GRAVE,
	['?'] = KEY_SLASH, ['/'] = KEY_SLASH,
	['\\'] = KEY_BACKSLASH, ['|'] = KEY_BACKSLASH,
	['-'] = KEY_MINUS, ['_'] = KEY_MINUS,
	['='] = KEY_EQUAL, ['+'] = KEY_EQUAL,
	['['] = KEY_LEFTBRACE, ['{'] = KEY_LEFTBRACE,
	[']'] = KEY_RIGHTBRACE, ['}'] = KEY_RIGHTBRACE,
	[';'] = KEY_SEMICOLON, [':'] = KEY_SEMICOLON,
	[','] = KEY_COMMA, ['<'] = KEY_COMMA,
	['.'] = KEY_DOT, ['>'] = KEY_DOT,
	['\''] = KEY_APOSTROPHE, ['"'] = KEY_APOSTROPHE,
	[' '] = KEY_SPACE, ['\n'] = KEY_ENTER,
};

static void pckb_work_handler(struct work_struct *work)
{
    struct picocalc_keyboard *pckb = container_of(work,
										struct picocalc_keyboard, work.work);
    struct i2c_client *client = pckb->client;
	u8 keystate;
	u8 keycode;
	int ret;

	ret = i2c_smbus_read_word_data(client, PCKB_REG);
	if (ret < 0) {
		dev_err(&client->dev, "%d\n", ret);
        goto reschedule;
	}

	keystate = (u8)(ret & 0xff);
	keycode = (u8)((ret & 0xff00) >> 8);

	if (keystate != KEY_STATE_PRESSED && keystate != KEY_STATE_RELEASED)
		goto reschedule;

	if (xlate[keycode] == 0 || xlate[keycode] == KEY_UNKNOWN)
		goto reschedule;

	if (keycode == 0xA2 || keycode == 0xA3)
	{
		if (keystate == KEY_STATE_PRESSED)
			pckb->page_key = 1;
		else
			pckb->page_key = 0;
	}

	if (pckb->page_key)
	{
		if (keycode == 0xB5)
			keycode = 0xD6;
		else if (keycode == 0xB6)
			keycode = 0xD7;
	}

	input_report_key(pckb->input, xlate[keycode], keystate == KEY_STATE_PRESSED);
    input_sync(pckb->input);

reschedule:
    schedule_delayed_work(&pckb->work, msecs_to_jiffies(pckb->interval));
}

static int pckb_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct picocalc_keyboard *pckb;
	struct device *dev = &client->dev;
	int i;
	int ret;

	pckb = devm_kzalloc(dev, sizeof(*pckb), GFP_KERNEL);
	if (!pckb)
		return -ENOMEM;
    
    pckb->client = client;
    pckb->interval = INTERVAL;
	pckb->page_key = 0;

	i2c_set_clientdata(client, pckb);

	pckb->input = devm_input_allocate_device(dev);
	if (!pckb->input)
    {
        ret = -ENOMEM;
        goto error;
    }

	input_set_drvdata(pckb->input, client);

	pckb->input->name = "Picocalc Keyboard";
	pckb->input->id.bustype = BUS_I2C;
	pckb->input->id.vendor  = 0x0001;
	pckb->input->id.product = 0x0001;
	pckb->input->id.version = 0x0001;

    __set_bit(EV_KEY, pckb->input->evbit); 
	__set_bit(EV_REP, pckb->input->evbit);
    for (i = 0; i < KEYCODE_SIZE; i++) {
        __set_bit(xlate[i], pckb->input->keybit);
    }

	ret = input_register_device(pckb->input);
	if (ret) {
		dev_err(dev, "Failed to register input: %d\n", ret);
		goto error;
	}

    INIT_DELAYED_WORK(&pckb->work, pckb_work_handler);
    schedule_delayed_work(&pckb->work, msecs_to_jiffies(pckb->interval));
	return 0;
error:
    return ret;
}

static void pckb_remove(struct i2c_client *client)
{
    struct picocalc_keyboard *pckb;

    pckb = i2c_get_clientdata(client);
    cancel_delayed_work_sync(&pckb->work);
}

static const struct i2c_device_id pckb_i2c_match[] = {
	{ "picocalc-keyboard", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pckb_i2c_match);

static const struct of_device_id pckb_of_match[] = {
	{ .compatible = "picocalc,picocalc-keyboard" },
	{ }
};
MODULE_DEVICE_TABLE(of, pckb_of_match);

static struct i2c_driver pckb_driver = {
	.probe		= pckb_probe,
    .remove     = pckb_remove,
	.id_table	= pckb_i2c_match,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table = pckb_of_match,
	},
};

static int __init pckb_i2c_init(void)
{
	return i2c_add_driver(&pckb_driver);
}
subsys_initcall(pckb_i2c_init);

static void __exit pckb_i2c_exit(void)
{
	i2c_del_driver(&pckb_driver);
}
module_exit(pckb_i2c_exit);

MODULE_AUTHOR("nekocharm <jumba.jookiba@outlook.com>");
MODULE_DESCRIPTION("Picocalc keyboard driver");
MODULE_LICENSE("GPL");
