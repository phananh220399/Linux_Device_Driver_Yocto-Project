

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/spi/spi.h>
#include <linux/cdev.h>
#include <linux/of_gpio.h>
#include <linux/of.h>


#define DEVNUM_NAME         "nokia5110_devnum"
#define CDEV_NAME_DEVICE    "nokia5110"
#define CDEV_NAME_CLASS     "nokia5110_class"


#define MAX_LENGTH          256

#define LOW                 0
#define HIGH                1

#define NOKIA5110_WIDTH    84
#define NOKIA5110_HEIGHT   48
#define NOKIA5110_NUM_BANK  6

/* LCD modes */

#define NOKIA5110_MODE_CMD  0
#define NOKIA5110_MODE_DATA 1
/* LCD commands */

#define LCD_CMD_EXTENDED    0x21    /* LCD Extended Commands */
#define LCD_CMD_CONTRAST    0xB1    /* LCD Extended Commands */
#define LCD_CMD_TEMP_COEF   0x04    /* Set Temperature Coefficient */
#define LCD_CMD_BIAS        0x14    /* LCD bias mode 1:48 */
#define LCD_CMD_NORMAL      0x0C    /* LCD in normal mode */
#define LCD_CMD_INVERSE     0x0D    /* LCD in inverse mode */
#define LCD_CMD_BASIC       0x20    /* LCD Basic Commands */
#define LCD_CMD_DISPLAY_ON  0X0C    /* Display on */
#define LCD_CMD_SET_X       0x80    /* Set X coordinate (OR with position) */
#define LCD_CMD_SET_Y       0x40    /* Set Y coordinate (OR with position) */

/* Union for LCD commands with parameters */
typedef union {
    struct {
        uint8_t cmd;        /* Command byte */
        uint8_t param;      /* Parameter byte (if needed) */
    } bytes;
    uint16_t word;          /* Command and parameter as a word */
} lcd_cmd_t;

/* Device structure */
typedef struct {
    struct spi_device *spi_dev;
    dev_t dev_num;
    struct class *class;
    struct device *device;
    struct cdev cdev;

    /* GPIO pins */
    int rst_pin;
    int dc_pin;

    /* Current position */
    uint8_t x_pos;
    uint8_t y_pos;
} nokia5110_t;

/* Global variables */
static char message [MAX_LENGTH];
static nokia5110_t* module_nokia5110 = NULL;

/* Function prototypes */
static void nokia5110_init(void);
static void nokia5110_clear_screen(void);
static int nokia5110_send_byte (bool is_data, unsigned char data);
static void nokia5110_print_char(char c);
static void nokia5110_print_string(const char* str);
static void nokia5110_set_position(uint8_t x, uint8_t y);
static void nokia5110_cleanup(void);

/* File operations */
static int nokia5110_open(struct inode *inodep, struct file *filep);
static int nokia5110_release(struct inode *inodep, struct file *filep);
static ssize_t nokia5110_write(struct file *filep, const char *buf, size_t Len, loff_t *offset);
static ssize_t nokia5110_read(struct file *filp, char __user *buf, size_t Len, loff_t *off);


/* ASCII character set 5x7 */
static const unsigned short ASCII[] [5] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00}, //20
    { 0x00, 0x00, 0x5f, 0x00, 0x00}, // 21 !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 22  "
    {0x14, 0x7f, 0x14, 0x7f, 0x14}, // 23 #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12}, // 24$
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 25 %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 26 &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 27 '
    {0x00, 0x1c, 0x22, 0x41, 0x00}, // 28(
    {0x00, 0x41, 0x22, 0x1c, 0x00}, // 29 )
    {0x14, 0x08, 0x3e, 0x08, 0x14}, // 2a *
    {0x08, 0x08, 0x3e, 0x08, 0x08}, // 2b +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 2c
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 2d
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 2e.
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 2f /
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, // 30 0
    {0x00, 0x42, 0x7f, 0x40, 0x00}, // 31 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 32 2
    {0x21, 0x41, 0x45, 0x4b, 0x31}, // 33 3
    {0x18, 0x14, 0x12, 0x7f, 0x10}, // 34 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 35 5
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, // 36 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 37 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 38 8
    {0x06, 0x49, 0x49, 0x29, 0x1e}, // 39 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 3a:
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 3b;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 3c <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 3d =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 3e >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 3f ?
    {0x32, 0x49, 0x79, 0x41, 0x3e}, // 40 @
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, // 41 A
    {0x7f, 0x49, 0x49, 0x49, 0x36}, // 42 B
    {0x3e, 0x41, 0x41, 0x41, 0x22}, // 43 C
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, // 44 D
    {0x7f, 0x49, 0x49, 0x49, 0x41}, // 45 E
    {0x7f, 0x09, 0x09, 0x09, 0x01}, // 46 F
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, // 47 G
    {0x7f, 0x08, 0x08, 0x08, 0x7f}, // 48 Η
    {0x00, 0x41, 0x7f, 0x41, 0x00}, // 49 I
    {0x20, 0x40, 0x41, 0x3f, 0x01}, // 4a J
    {0x7f, 0x08, 0x14, 0x22, 0x41}, // 4b K
    {0x7f, 0x40, 0x40, 0x40, 0x40}, // 4c L
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, // 4d M
    {0x7f, 0x04, 0x08, 0x10, 0x7f}, // 4e N
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, // 4f 0
    {0x7f, 0x09, 0x09, 0x09, 0x06}, // 50 P
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, // 51 Q
    {0x7f, 0x09, 0x19, 0x29, 0x46}, // 52 R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 53 5
    {0x01, 0x01, 0x7f, 0x01, 0x01}, // 54 T
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, // 55 U
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, // 56 V
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, // 57 W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 58 X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 59 Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 5a Z
    {0x00, 0x7f, 0x41, 0x41, 0x00}, // 56
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 5c Backslash
    {0x00, 0x41, 0x41, 0x7f, 0x00}, // 5d ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 5e^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 5f _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 60 `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 61 a
    {0x7f, 0x48, 0x44, 0x44, 0x38}, // 62 b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 63 c
    {0x38, 0x44, 0x44, 0x48, 0x7f}, // 64 d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 65 e
    {0x08, 0x7e, 0x09, 0x01, 0x02}, // 66 f
    {0x0c, 0x52, 0x52, 0x52, 0x3e}, // 67 g
    {0x7f, 0x08, 0x04, 0x04, 0x78}, // 68 h
    {0x00, 0x44, 0x7d, 0x40, 0x00}, // 69 i
    {0x20, 0x40, 0x44, 0x3d, 0x00}, // 6a j
    {0x7f, 0x10, 0x28, 0x44, 0x00}, // 6b k
    {0x00, 0x41, 0x7f, 0x40, 0x00}, // 6c L
    {0x7c, 0x04, 0x18, 0x04, 0x78}, // 6d m
    {0x7c, 0x08, 0x04, 0x04, 0x78}, // 6e n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 6f o
    {0x7c, 0x14, 0x14, 0x14, 0x08}, // 70 p
    {0x08, 0x14, 0x14, 0x18, 0x7c}, // 71 q
    {0x7c, 0x08, 0x04, 0x04, 0x08}, // 72 г
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 73 s
    {0x04, 0x3f, 0x44, 0x40, 0x20}, // 74 t
    {0x3c, 0x40, 0x40, 0x20, 0x7c}, // 75 u
    {0x1c, 0x20, 0x40, 0x20, 0x1c}, // 76 v
    {0x3c, 0x40, 0x30, 0x40, 0x3c}, // 77 w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 78 x
    {0x0c, 0x50, 0x50, 0x50, 0x3c}, // 79 y
    {0x44, 0x64, 0x54, 0x4c, 0x44}, // 7a z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 7b {
    {0x00, 0x00, 0x7f, 0x00, 0x00}, // 7c |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 7d }
    {0x10, 0x08, 0x08, 0x10, 0x08}, // 7e ~
    {0x00, 0x06, 0x09, 0x09, 0x06}  //  7f.

};


/* File operations structure */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = nokia5110_open,
    .release = nokia5110_release,
    .write = nokia5110_write,
    .read = nokia5110_read
};

static int nokia5110_open(struct inode* inodep, struct file* filep)
{
    pr_info("[%s - %d] Device opened\n", __func__, __LINE__);
    return 0;
}



static int nokia5110_release (struct inode* inodep, struct file* filep)
{
    pr_info("[%s - %d] Device closed\n", __func__, __LINE__);
    filep->private_data = NULL;
    return 0;
}


static ssize_t nokia5110_write(struct file* filep, const char* buf, size_t Len, loff_t* offset)
{
    int ret;

    pr_info("[%s - %d] Writing to device\n", __func__, __LINE__);

    /* Clear message buffer */
    memset(message, 0x0, sizeof(message));


    /* Check for buffer overflow */
    if (Len > sizeof(message) - 1) {
        pr_info("[%s - %d] Input data too large, truncating to %zu bytes\n",
                __func__, __LINE__, sizeof(message) - 1);
        Len = sizeof(message) -1;
    }

    /* Copy data from user space */
    ret = copy_from_user(message, buf, Len);
    if (ret) {
        pr_err("[%s - %d] copy_from_user failed: %d bytes not copied\n",
                __func__, __LINE__, ret);
        return -EFAULT;
    }

    pr_info("[%s - %d] Data from user: %s\n", __func__, __LINE__, message);

    /* Clear screen and print message */
    nokia5110_clear_screen();
    nokia5110_print_string(message);

    return Len;
}



static ssize_t nokia5110_read(struct file* filp, char __user* buf, size_t Len, loff_t* off)
{
    ssize_t bytes_to_read = min(Len, (size_t) (MAX_LENGTH - *off));

     pr_info("[%s - %d] Reading from device\n", __func__, __LINE__);

    /* Check if end of file */
    if (bytes_to_read <= 0) {
        pr_info("[%s - %d] End of file\n", __func__,__LINE__);
        return 0;
    }

    /* Copy data to user space */
    if (copy_to_user(buf, message + *off, bytes_to_read)) {
        pr_err("[%s - %d] Copy to user failed\n",__func__, __LINE__);
        return -EFAULT;
    }

    /* Update offset */
    *off += bytes_to_read;
    pr_info("[%s - %d] Read %zd bytes\n", __func__, __LINE__, bytes_to_read);

    return bytes_to_read;
}



static int nokia5110_create_device_file(nokia5110_t* module)
{
    int ret = 0;

    pr_info("[%s - %d] Creating device file\n", __func__ ,__LINE__);

    /* Allocate device number */
    ret = alloc_chrdev_region(&(module->dev_num), 0, 1, DEVNUM_NAME);
    if (ret < 0) {
        pr_err("[%s - %d] Cannot register major number: %d\n", __func__, __LINE__, ret);
        goto alloc_dev_failed;
    }

    pr_info("[%s - %d] Registered with major = %d, minor = %d\n",
            __func__, __LINE__, MAJOR(module->dev_num), MINOR (module->dev_num));

    /* Create device class */
    module->class = class_create(THIS_MODULE, CDEV_NAME_CLASS);
    if (IS_ERR(module->class)) {
        ret = PTR_ERR(module->class);
        pr_err("[%s - %d] Cannot create device class: %d\n", __func__, __LINE__, ret);
        goto create_class_failed;
    }

    /* Initialize character device */
    cdev_init(&module->cdev, &fops);
    module->cdev.owner = THIS_MODULE;
    module->cdev.dev = module->dev_num;

    /* Add character device to system */
    ret = cdev_add(&module->cdev, module->dev_num, 1);
    if (ret) {
        pr_err("[%s - %d] Cannot add cdev: %d\n", __func__, __LINE__, ret);
        goto cdev_add_failed;
    }

    module->device = device_create(module->class, NULL, module->dev_num, NULL, CDEV_NAME_DEVICE);
    if (IS_ERR(module->device)) {
        ret = PTR_ERR(module->device);
        goto create_device_failed;
}


    pr_info("[%s - %d] Device file created successfully\n", __func__, __LINE__);
    return 0;


    
cdev_add_failed:
    device_destroy (module->class, module->dev_num);
create_device_failed:
    class_destroy (module->class);
create_class_failed:
    unregister_chrdev_region(module->dev_num, 1);
alloc_dev_failed:
    return ret;
    }

static void nokia5110_set_position(uint8_t x, uint8_t y)
{
int ret;

/* Validate coordinates */
if (x >= NOKIA5110_WIDTH || y >= NOKIA5110_NUM_BANK)
    return;
/* Send commands to set position */
ret = nokia5110_send_byte(NOKIA5110_MODE_CMD, LCD_CMD_SET_X | x);
if (ret < 0) {
    pr_err("[%s %d] Failed to set X position\n", __func__, __LINE__);
    return;
}

ret = nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_SET_Y | y);
if (ret < 0) {
    pr_err("[%s %d] Failed to set Y position\n", __func__, __LINE__);
    return;
}

/* Update current position */
module_nokia5110->x_pos = x;
module_nokia5110->y_pos = y;
}

static int nokia5110_send_byte(bool is_data, unsigned char data)
{
    int ret;
    struct spi_transfer t;
    struct spi_message m;

    /* Set DC pin according to data/command mode */
    gpio_set_value (module_nokia5110->dc_pin, is_data? HIGH: LOW);

    /* Initialize SPI message */
    memset(&t, 0, sizeof(t));
    spi_message_init(&m);

    /* Set up transfer */
    t.tx_buf = &data;
    t.len = 1;
    t.speed_hz = 1000000; /* 1MHz */
    spi_message_add_tail(&t, &m);

    /* Perform transfer */
    ret = spi_sync(module_nokia5110->spi_dev, &m);
    if (ret < 0) {
        pr_err("[%s - %d] SPI transfer failed: %d\n", __func__, __LINE__, ret);
    }

    /* Add small delay for stability */
    udelay(1);

    return ret;
}

static void nokia5110_print_string(const char* data)
{
    /* User space send data always has character LF end of string. So we won't print it to LCD */
    while (*data) {
    if (*data == '\n') {
        /* Handle newline character */
        module_nokia5110->x_pos = 0;
        module_nokia5110->y_pos++;
        if (module_nokia5110->y_pos >= NOKIA5110_NUM_BANK)
            module_nokia5110->y_pos = 0;

        nokia5110_set_position(module_nokia5110->x_pos, module_nokia5110->y_pos);
    } else if (*data != '\r') {
        /* Print normal character */
        nokia5110_print_char(*data);
    }
    data++;
    }

}


void nokia5110_print_char(char c)
{
    int i = 0;
    int ret;

    /* Send empty column before character */
    ret = nokia5110_send_byte(NOKIA5110_MODE_DATA, 0x00);
    if (ret < 0)
        return;

    /* Send character data (5 columns) */
    for(i = 0; i < 5; i++) {
        ret = nokia5110_send_byte (NOKIA5110_MODE_DATA, ASCII [c - 0x20][i]);
        if (ret < 0)
            return;
    }
    /* Send empty column after character */
    ret = nokia5110_send_byte (NOKIA5110_MODE_DATA, 0x00);
    if (ret < 0)
        return;

    /* Update position */
    module_nokia5110->x_pos += 6;
    if (module_nokia5110->x_pos >= NOKIA5110_WIDTH - 6) {
        module_nokia5110->x_pos = 0;
        module_nokia5110->y_pos++;
        if (module_nokia5110->y_pos >= NOKIA5110_NUM_BANK)
            module_nokia5110->y_pos = 0;

        nokia5110_set_position(module_nokia5110->x_pos, module_nokia5110->y_pos);
    }
}

static void nokia5110_clear_screen(void)
{
    int idx = 0;
    int ret;


    /* Set cursor to home position */
    nokia5110_set_position(0, 0);

    /* Fill screen with zeros */
    for(idx = 0; idx < NOKIA5110_WIDTH * NOKIA5110_HEIGHT / 8; idx++) {
        ret = nokia5110_send_byte (NOKIA5110_MODE_DATA, 0x00);
        if (ret < 0) {
            pr_err("[%s - %d] Failed to clear screen\n", __func__, __LINE__);
            return;
        
        }
    }

        /* Reset cursor position */
        nokia5110_set_position(0, 0);
}

static void nokia5110_destroy_device_file(nokia5110_t *module)
{
    cdev_del(&module->cdev);
    device_destroy(module->class, module->dev_num);
    class_destroy(module->class);
    unregister_chrdev_region(module->dev_num, 1);
}



static void nokia5110_init(void)
{
    int ret;

    pr_info("[%s - %d] Nokia5110 display initialization\n", __func__, __LINE__);

    /* Request GPIO pins */
    // ret = gpio_request(module_nokia5110->rst_pin, "RST");
    // if (ret) {
    //     pr_err("[%s - %d] Failed to request RST GPIO: %d\n", __func__, __LINE__, ret);
    //     return;
    // }

    // ret = gpio_request(module_nokia5110->dc_pin, "DC");
    // if (ret) {
    //     pr_err("[%s - %d] Failed to request DC GPIO: %d\n", __func__, __LINE__, ret);
    //     gpio_free(module_nokia5110->rst_pin);
    //     return;
    // }

    /* Set GPIO directions */
    gpio_direction_output(module_nokia5110->rst_pin, LOW);
    gpio_direction_output(module_nokia5110->dc_pin, LOW);

    /* Reset LCD */
    gpio_set_value (module_nokia5110->rst_pin, LOW);
    mdelay(100); /* Longer reset pulse for reliability */
    gpio_set_value(module_nokia5110->rst_pin, HIGH);
    mdelay(100); /* ALLow LCD to stabilize */


    /* Initialize LCD with improved sequence */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_EXTENDED);     /* LCD Extended Commands */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_CONTRAST);     /* Set LCD Contrast */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_TEMP_COEF);    /* Set Temp coefficient */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_BIAS);         /* LCD bias mode 1:48 */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_BASIC);        /* LCD Basic Commands */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_NORMAL);       /* LCD in normal mode */

    /* Additional initialization commands for better display quality */
    nokia5110_send_byte (NOKIA5110_MODE_CMD, LCD_CMD_DISPLAY_ON);

    /* Clear screen and set cursor position */
    nokia5110_clear_screen();

    /* Clear screen and set cursor position */
    nokia5110_clear_screen();

    /* Initialize position variables */
    module_nokia5110->x_pos = 0;
    module_nokia5110->y_pos = 0;
}





static int nokia5110_spi_probe(struct spi_device* spi)
{
    nokia5110_t* module;
    struct device_node *np = spi->dev.of_node;
    int ret;

    pr_info("[%s - %d] Probing Nokia5110 SPI device\n", __func__, __LINE__);

    /* Allocate memory for driver structure */
    module =  kzalloc(sizeof(*module), GFP_KERNEL);
    if (!module) {
        pr_err("[%s - %d] Failed to allocate memory\n", __func__, __LINE__);
        return -ENOMEM;
    }
    /* Get GPIO pins from device tree */
    module->rst_pin = of_get_named_gpio(np, "rst-gpios", 0);
    if (module->rst_pin < 0) {
        pr_err("[%s - %d] Failed to get reset-gpios from DT: %d\n",
        __func__, __LINE__, module->rst_pin);
    ret = module->rst_pin;
    goto err_free_module;
    }

    module->dc_pin = of_get_named_gpio(np, "dc-gpios", 0);
    if (module->dc_pin < 0) {
        pr_err("Cannot get dc-gpios\n");
        ret = module->dc_pin;
    goto err_free_module;
}


    pr_info("[%s - %d] GPIO pins: RST=%d, DC=%d\n",
        __func__, __LINE__, module->rst_pin, module->dc_pin);

    /* Configure SPI device */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    if (!spi->max_speed_hz)
        spi->max_speed_hz = 4000000; /*4MHz default */

    ret = spi_setup(spi);
    if (ret < 0) {
        pr_err("[%s - %d] Failed to setup SPI: %d\n", __func__, __LINE__, ret);
        goto err_free_module;
    }


    ret = gpio_request(module->rst_pin, "nokia_rst");
    if (ret)
        goto err_free_module;

    ret = gpio_request(module->dc_pin, "nokia_dc");
    if (ret)
        goto err_free_rst;

    gpio_direction_output(module->rst_pin, 1);
    gpio_direction_output(module->dc_pin, 1);


    /* Create device file */
    ret = nokia5110_create_device_file(module);
    if (ret != 0) {
        pr_err("[%s - %d] Failed to create device file: %d\n", __func__, __LINE__, ret);
        goto err_free_module;
    }

    /* Store SPI device in module structure */
    module->spi_dev = spi;
    module_nokia5110 = module;

    /* Initialize LCD */
    nokia5110_init();

    /* Display welcome message */
    nokia5110_clear_screen();
    nokia5110_print_string("DevLinux\nHello World\n");
    pr_info("[%s - %d] Nokia5110 device created successfully\n", __func__, __LINE__);

    
    /* Store driver data in SPI device */
    spi_set_drvdata(spi, module);

    return 0;

err_free_rst:
    gpio_free(module->rst_pin);

err_free_module:
    kfree (module);
    return ret;
}

static void nokia5110_cleanup(void)
{
    gpio_free(module_nokia5110->rst_pin);
    gpio_free(module_nokia5110->dc_pin);
}



static int nokia5110_spi_remove(struct spi_device* spi)
{
    nokia5110_t* module = spi_get_drvdata(spi);

    pr_info("[%s - %d] Removing Nokia5110 SPI device\n", __func__, __LINE__);

    /* Clean up LCD resources */
    nokia5110_cleanup();

    /* Clean up device file */
    nokia5110_destroy_device_file(module);

    /* Free module memory */
    kfree(module);
    module_nokia5110 = NULL;

    return 0;
}


/* Device tree match table */
static const struct of_device_id nokia5110_of_match_id[] = {
{   .compatible = "nokia5110" },
{   /* sentinel */ }
};
MODULE_DEVICE_TABLE (of, nokia5110_of_match_id);

/* SPI driver structure */
static struct spi_driver nokia5110_spi_driver = {
    .driver = {
        .name = "nokia5110",
        .owner = THIS_MODULE,
        .of_match_table = nokia5110_of_match_id,
    },
    .probe  = nokia5110_spi_probe,
    .remove = nokia5110_spi_remove,
};

module_spi_driver (nokia5110_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("PA");
MODULE_DESCRIPTION("SPI Device Driver for Nokia 5110 (BCM2711)");
