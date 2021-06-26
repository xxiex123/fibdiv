#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/time.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 2000

#define RIGHT 0
#define LEFT 1

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static ktime_t kt;
static unsigned int buff_size;
static long long fib_BN_size;
uint64_t *c_div;

static void fib_BN_init(uint64_t *x, long long size)
{
    for (int i = 0; i < size; i++)
        x[i] = 0;
}

static bool fib_BN_equalzero(uint64_t *x)
{
    for (int i = 0; i < fib_BN_size; i++) {
        if (x[i] != 0)
            return false;
    }
    return true;
}

static void fib_BN_assign(uint64_t *x, uint64_t *y, long long size)
{
    for (int i = 0; i < size; i++)
        x[i] = y[i];
}

static void fib_BN_shift(uint64_t *x, bool flag, unsigned int offset)
{
    if (offset >= 64 || offset == 0)
        return;

    if (!flag) {
        uint64_t mask = 0;
        for (int i = 0; i < offset; i++) {
            mask = (mask << 1) | 0x0000000000000001;
        }
        for (int i = 0; i < fib_BN_size; i++) {
            x[i] = x[i] >> offset;
            if (i + 1 < fib_BN_size) {
                uint64_t tmp = x[i + 1] & mask;
                x[i] |= tmp << (64 - offset);
            }
        }
    } else {
        uint64_t mask = 0;
        for (int i = 0; i < offset; i++) {
            mask = (mask >> 1) | 0x8000000000000000;
        }
        for (int i = fib_BN_size - 1; i >= 0; i--) {
            x[i] = x[i] << offset;
            if (i - 1 >= 0) {
                uint64_t tmp = x[i - 1] & mask;
                x[i] |= tmp >> (64 - offset);
            }
        }
    }
}
/*
static void fib_BN_leftshift64(uint64_t *x,
                               unsigned int offset,
                               uint64_t size)  // left shift offset * 64bit
{
    if (offset == 0)
        return;

    for (int i = size - 1; i >= 0; i--) {
        if (i >= offset)
            x[i] = x[i - offset];
        else
            x[i] = 0;
    }
}
*/
/*
static int fib_BN_getfirstbit(uint64_t *x)  // get index of MSB
{
    if (fib_BN_equalzero(x))
        return 0;

    int count = 0;
    for (int i = fib_BN_size - 1; i >= 0; i--) {
        if (x[i] == 0)
            continue;
        uint64_t tmp = x[i];
        while (tmp != 1) {
            tmp = tmp >> 1;
            count++;
        }
        return i * 64 + count;
    }
}
*/
/*
static void fib_BN_cdivTen(void)
{
    unsigned int tmp = 0;

    for (int i = fib_BN_size; i >= 0; i--) {
        uint64_t result = 0;

        for (int j = 63; j >= 0; j--) {
            tmp = (c[i] & 0x8000000000000000) ? (tmp << 1) | 0x0000000000000001
                                              : tmp << 1;
            if (tmp >= 10) {
                tmp -= 10;
                result = (result << 1) | 1;
            } else
                result = result << 1;
            c[i] = c[i] << 1;
        }
        c[i] = result;
    }
}
*/
static void fib_BN_add(uint64_t *x,
                       uint64_t *y,
                       uint64_t *z,
                       uint64_t size)  // x + y = z
{
    uint64_t carry = 0;
    unsigned __int128 tmp;

    for (int i = 0; i < size; i++) {
        tmp = (unsigned __int128) x[i] + y[i] + carry;
        carry = (tmp >> 64) ? 1 : 0;
        z[i] = tmp;
    }
}

static void fib_BN_sub(uint64_t *x,
                       uint64_t *y,
                       uint64_t *z,
                       uint64_t size)  // x - y = z
{
    int borrow = 0;

    for (int i = 0; i < size; i++) {
        int next_borrow = (x[i] < y[i]) ? 1 : 0;
        z[i] = x[i] - y[i] - borrow;
        borrow = next_borrow;
    }
}

static void fib_BN_mul(uint64_t *x,
                       uint64_t *y,
                       uint64_t *z,
                       uint64_t size)  // x * y = z
{
    uint64_t tmp[size];
    fib_BN_init(tmp, size);
    fib_BN_init(z, size);

    for (int i = 0; i < size; ++i) {
        if (x[i] == 0)
            continue;
        for (int j = 0; j < size; ++j) {
            if (y[i] == 0)
                continue;
            if (i + j < size) {
                fib_BN_init(tmp, size);
                unsigned __int128 intermediate =
                    x[i] * (unsigned __int128) y[j];
                tmp[i + j] = intermediate;
                if (i + j + 1 < size)
                    tmp[i + j + 1] = intermediate >> 64;
                fib_BN_add(tmp, z, z, size);
            }
        }
    }
}
/*
static void fib_BN_cinit(void)
{
    for (int i = 0; i <= fib_BN_size; i++)
        c_div[i] |= 0xFFFFFFFFFFFFFFFF;
    // divide by 10 and then +1
    fib_BN_cdivTen();
    uint64_t tmp[fib_BN_size + 1];
    fib_BN_init(tmp, fib_BN_size + 1);
    tmp[0] = 1;
    fib_BN_add(c_div, tmp, c_div, fib_BN_size + 1);  // c = c + 1
}
*/
/*
static unsigned int fib_BN_divTen(uint64_t *x)  // x = x / 10
{
    unsigned int size = (fib_BN_size + 1) * 2;
    uint64_t tmp[3][size];

    for (int i = 0; i < size; i++) {
        if (i < fib_BN_size)
            tmp[0][i] = x[i];
        else
            tmp[0][i] = 0;
        if (i < fib_BN_size + 1)
            tmp[1][i] = c_div[i];
        else
            tmp[1][i] = 0;
    }
    // tmp[0] = x, tmp[1] = c_div, tmp[2] = 0;
    fib_BN_mul(tmp[0], tmp[1], tmp[2], size);

    for (int i = 0; i < fib_BN_size; i++) {
        x[i] = tmp[2][i + fib_BN_size + 1];
        tmp[0][i] = (i == 0) ? 10 : 0;
    }

    // tmp[0] = 10, tmp[2] = quotient
    fib_BN_mul(tmp[0], tmp[2], tmp[1], size / 2 + 1);

    unsigned int result = (unsigned int) tmp[1][fib_BN_size + 1];
    printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ divTen done, remainder = %u\n",
           result);

    return result;
}
*/
/*
static char *fib_BN_toStr10(uint64_t *x)
{
    char *result = kmalloc(buff_size, GFP_KERNEL);
    int index = 0;

    if (result == NULL) {
        printk("kmalloc fail\n");
        return NULL;
    }

    while (!fib_BN_equalzero(x)) {
        result[index] = "0123456789"[fib_BN_divTen(x)];
        index++;
    }
    int count = 0;
    for (int i = index - 1; i > count; i--) {
        char tmp = result[i];
        result[i] = result[count];
        result[count] = tmp;
        count++;
    }
    result[index] = '\0';

    return result;
}
*/
static char *fib_BN_toStr16(uint64_t *x)
{
    char *result = kmalloc(buff_size, GFP_KERNEL);
    int index = 0;
    if (result == NULL) {
        printk("kmalloc fail\n");
        return NULL;
    }

    while (!fib_BN_equalzero(x)) {
        uint64_t tmp = x[0] & 0x000000000000000F;
        result[index] = "0123456789abcdef"[tmp];
        index++;
        fib_BN_shift(x, RIGHT, 4);
    }

    int count = 0;
    for (int i = index - 1; i > count; i--) {
        char tmp = result[i];
        result[i] = result[count];
        result[count] = tmp;
        count++;
    }
    result[index] = '\0';

    return result;
}

static char *fib_BN_fd(unsigned int n)  // fast doubling
{
    if (n == 0) {
        char *result = kmalloc(buff_size, GFP_KERNEL);
        result[0] = '0';
        result[1] = '\0';
        return result;
    }
    unsigned int h = 0;
    for (unsigned int i = n; i; ++h, i >>= 1)
        ;

    uint64_t a[fib_BN_size];  // F(0) = 0
    uint64_t b[fib_BN_size];  // F(1) = 1
    fib_BN_init(a, fib_BN_size);
    fib_BN_init(b, fib_BN_size);
    b[0] = 1;

    uint64_t c[fib_BN_size];
    uint64_t d[fib_BN_size];
    fib_BN_init(c, fib_BN_size);
    fib_BN_init(d, fib_BN_size);

    uint64_t tmp[2][fib_BN_size];
    fib_BN_init(tmp[0], fib_BN_size);
    fib_BN_init(tmp[1], fib_BN_size);

    int max_n = 2;
    int max_size = 1;
    for (unsigned int mask = 1 << (h - 1); mask; mask >>= 1) {  // Run h times!

        fib_BN_mul(a, a, tmp[0], max_size);
        fib_BN_mul(b, b, tmp[1], max_size);
        fib_BN_add(tmp[0], tmp[1], d, max_size);

        fib_BN_shift(b, LEFT, 1);
        fib_BN_sub(b, a, b, max_size);
        fib_BN_mul(a, b, c, max_size);

        if (mask & n) {  // n_j is odd: k = (n_j-1)/2 => n_j = 2k + 1
            fib_BN_assign(a, d, max_size);
            fib_BN_add(c, d, b, max_size);
        } else {  // n_j is even: k = n_j/2 => n_j = 2k
            fib_BN_assign(a, c, max_size);
            fib_BN_assign(b, d, max_size);
        }
        max_n <<= 1;
        max_size = max_n * 7 / 640 + 1;
        max_size = (max_size > fib_BN_size) ? fib_BN_size : max_size;
    }

    return fib_BN_toStr16(a);
}

/*
static char *fib_BN_sequence(long long k)
{
    if (k == 0) {
        char *a = kmalloc(buff_size, GFP_KERNEL);
        a[0] = '0';
        a[1] = '\0';
        return a;
    } else if (k == 1) {
        char *a = kmalloc(buff_size, GFP_KERNEL);
        a[0] = '1';
        a[1] = '\0';
        return a;
    }

    uint64_t tmp[3][fib_BN_size];

    for (int i = 0; i < 3; i++)
        fib_BN_init(tmp[i], fib_BN_size);
    tmp[1][0] = 1;

    int target[3] = {2, 0, 1};
    for (int i = 2; i <= k; i++) {
        int s = target[0];
        target[0] = target[1];
        target[1] = target[2];
        target[2] = s;

        fib_BN_add(tmp[target[0]], tmp[target[1]], tmp[target[2]], fib_BN_size);
    }
    char *result;
    result = fib_BN_toStr16(tmp[target[2]]);
    return result;
}
*/
/*
static char* fib_gmp(long long k)
{
    if (k == 0) {
        char *a = kmalloc(2, GFP_KERNEL);
        a[0] = '0';
        a[1] = '\0';
        return a;
    } else if (k == 1) {
        char *a = kmalloc(2, GFP_KERNEL);
        a[0] = '1';
        a[1] = '\0';
        return a;
    }
    //initial the mpz_t
    mpz_t tmp[3];
    for (int i = 0; i < 3; i++)
        mpz_init2(tmp[0], fib_result_size_bit);
    mpz_set_ui(tmp[0],0);
    mpz_set_ui(tmp[1],1);

    int target[3] = {2, 0, 1};

    for (int i = 2; i <= k; i++) {
        int tmp = target[0];
        target[0] = target[1];
        target[1] = target[2];
        target[2] = tmp;

        mpz_add(tmp[target[2]], tmp[target[0]], tmp[target[1]]);
    }

    char *result = kmalloc(mpz_sizeinbase(tmp[target[2]], 10) + 2, GFP_KERNEL);
    if (result == NULL){
        printk("kmalloc fail\n");
        return NULL;
    }

    mpz_get_str(result, 10, tmp[target[2]]);
    mpz_clears(tmp);
    return result;
}

static char *fib_bintostr16(unsigned __int128 x)
{
    uint64_t tmp[2];
    tmp[0] = x;
    tmp[1] = x >> 64;

    return fib_BN_toStr16(tmp);
}

static char *fib_sequence(long long k)
{
    // FIXME: use clz/ctz and fast algorithms to speed up
    unsigned __int128 f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    char *result = fib_bintostr16(f[k]);
    return result;
}
*/
static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    kt = ktime_get();
    buff_size = size;
    fib_BN_size = (long long) ((*offset * 7) / 640 + 1);
    fib_BN_size = (fib_BN_size == 1) ? 2 : fib_BN_size;
    // char *tmp = fib_BN_sequence(*offset);
    char *tmp = fib_BN_fd(*offset);
    copy_to_user(buf, tmp, size);
    kfree(tmp);
    /*(if (*offset < 150) {
        char *tmp = fib_sequence(*offset);
        copy_to_user(buf, tmp, size);
        kfree(tmp);
    } else {
    */
    // c = kmalloc(sizeof(uint64_t) * (fib_BN_size + 1), GFP_KERNEL);
    // fib_BN_cinit();
    // char *tmp = fib_BN_sequence(*offset);
    // copy_to_user(buf, tmp, size);
    // kfree(c);
    // kfree(tmp);
    //}
    kt = ktime_sub(ktime_get(), kt);
    return 1;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return (int) (ktime_to_ns(kt));
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now

    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
