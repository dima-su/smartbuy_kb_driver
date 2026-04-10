#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>

#define DRIVER_AUTHOR "Dmitry Sukharev <arezxbusiness@gmail.com>"
#define DRIVER_DESC "Universal open source USB HID keyboard driver"

#define DEVICE_NAME "usb_kb_name"
#define CLASS_NAME "usb_kb_dev_class"
#define VENDOR_ID 0x1a2c
#define KEYBOARD_ID 0x407e

static struct last_pressed_strafe {
  char active;
  char last;
  unsigned char phys;
};

DECLARE_BITMAP(old_keys, KEY_CNT);

static struct usb_device_id universal_id_table[] = {
    {USB_INTERFACE_INFO(USB_CLASS_HID, 1, 1)}, {}};

static struct usb_device_id smartbuy_id_table[] = {
    {USB_DEVICE(VENDOR_ID, KEYBOARD_ID)}, {}};

MODULE_DEVICE_TABLE(usb, universal_id_table);

// Keyboard key bindings
static const unsigned char usb_kbd_keycode[256] = {
    0,   0,   0,   0,   30,  48,  46,  32,  18,  33,  34,  35,  23,  36,  37,
    38,  50,  49,  24,  25,  16,  19,  31,  20,  22,  47,  17,  45,  21,  44,
    2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  28,  1,   14,  15,  57,
    12,  13,  26,  27,  43,  43,  39,  40,  41,  51,  52,  53,  58,  59,  60,
    61,  62,  63,  64,  65,  66,  67,  68,  87,  88,  99,  70,  119, 110, 102,
    104, 111, 107, 109, 106, 105, 108, 103, 69,  98,  55,  74,  78,  96,  79,
    80,  81,  75,  76,  77,  71,  72,  73,  82,  83,  86,  127, 116, 117, 183,
    184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 134, 138, 130, 132,
    128, 129, 131, 137, 133, 135, 136, 113, 115, 114, 0,   0,   0,   121, 0,
    89,  93,  124, 92,  94,  95,  0,   0,   0,   122, 123, 90,  91,  85,  0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   29,
    42,  56,  125, 97,  54,  100, 126, 164, 166, 165, 163, 161, 115, 114, 113,
    150, 158, 159, 128, 136, 177, 178, 176, 142, 152, 173, 140};

static const int modifier_map[] = {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT,
                                   KEY_LEFTMETA, KEY_RIGHTCTRL, KEY_RIGHTSHIFT,
                                   KEY_RIGHTALT, KEY_RIGHTMETA};

struct keyboard_info {
  struct urb *urb;
  struct input_dev *input_dev;
  char *buffer;
  struct usb_device *usb_dev;
  struct last_pressed_strafe ad_group;
  struct last_pressed_strafe ws_group;
};

// Function to call when kb calls back to the driever
//

static void KbCallback(struct urb *urb) {
  struct keyboard_info *kbd = urb->context;
  u8 *keys_buffer = urb->transfer_buffer;
  DECLARE_BITMAP(new_keys, KEY_CNT);
  unsigned int key;
  int i;

  if (urb->status)
    goto resubmit;

  bitmap_zero(new_keys, KEY_CNT);

  for (int i = 0; i < 8; i++) {
    if (keys_buffer[0] & (1 << i))
      set_bit(modifier_map[i], new_keys);
  }

  for (i = 0; i < 6; i++) {
    u8 code = keys_buffer[2 + i];
    if (code && code < 256) {
      key = usb_kbd_keycode[code];
      if (key)
        set_bit(key, new_keys);
    }
  }

  unsigned int new_ad_active = 0;
  int ad_phys = 0;
  if (test_bit(KEY_A, new_keys))
    ad_phys |= (1 << 0);
  if (test_bit(KEY_D, new_keys))
    ad_phys |= (1 << 1);

  if (ad_phys != kbd->ad_group.phys) {
    if (ad_phys & ~kbd->ad_group.phys) {
      if (ad_phys & (1 << 0) && !(kbd->ad_group.phys & (1 << 0)))
        new_ad_active = KEY_A;
      else if (ad_phys & (1 << 1) && !(kbd->ad_group.phys & (1 << 1)))
        new_ad_active = KEY_D;
      if (!new_ad_active && (ad_phys & (1 << 0)))
        new_ad_active = KEY_A;
      else if (!new_ad_active && (ad_phys & (1 << 1)))
        new_ad_active = KEY_D;
      if (kbd->ad_group.active)
        kbd->ad_group.last = kbd->ad_group.active;
    } else {
      if ((kbd->ad_group.active == KEY_A && !(ad_phys & (1 << 0))) ||
          (kbd->ad_group.active == KEY_D && !(ad_phys & (1 << 1)))) {
        if (kbd->ad_group.last && test_bit(kbd->ad_group.last, new_keys))
          new_ad_active = kbd->ad_group.last;
        else
          new_ad_active = 0;
      } else {
        if (kbd->ad_group.active && test_bit(kbd->ad_group.active, new_keys))
          new_ad_active = kbd->ad_group.active;
        else
          new_ad_active = 0;
      }
    }
  } else {
    if (kbd->ad_group.active && test_bit(kbd->ad_group.active, new_keys))
      new_ad_active = kbd->ad_group.active;
    else
      new_ad_active = 0;
  }
  kbd->ad_group.phys = ad_phys;

  unsigned int new_ws_active = 0;
  int ws_phys = 0;
  if (test_bit(KEY_W, new_keys))
    ws_phys |= (1 << 0);
  if (test_bit(KEY_S, new_keys))
    ws_phys |= (1 << 1);

  if (ws_phys != kbd->ws_group.phys) {
    if (ws_phys & ~kbd->ws_group.phys) {
      if (ws_phys & (1 << 0) && !(kbd->ws_group.phys & (1 << 0)))
        new_ws_active = KEY_W;
      else if (ws_phys & (1 << 1) && !(kbd->ws_group.phys & (1 << 1)))
        new_ws_active = KEY_S;
      if (!new_ws_active && (ws_phys & (1 << 0)))
        new_ws_active = KEY_W;
      else if (!new_ws_active && (ws_phys & (1 << 1)))
        new_ws_active = KEY_S;
      if (kbd->ws_group.active)
        kbd->ws_group.last = kbd->ws_group.active;
    } else {
      if ((kbd->ws_group.active == KEY_W && !(ws_phys & (1 << 0))) ||
          (kbd->ws_group.active == KEY_S && !(ws_phys & (1 << 1)))) {
        if (kbd->ws_group.last && test_bit(kbd->ws_group.last, new_keys))
          new_ws_active = kbd->ws_group.last;
        else
          new_ws_active = 0;
      } else {
        if (kbd->ws_group.active && test_bit(kbd->ws_group.active, new_keys))
          new_ws_active = kbd->ws_group.active;
        else
          new_ws_active = 0;
      }
    }
  } else {
    if (kbd->ws_group.active && test_bit(kbd->ws_group.active, new_keys))
      new_ws_active = kbd->ws_group.active;
    else
      new_ws_active = 0;
  }
  kbd->ws_group.phys = ws_phys;

  clear_bit(KEY_A, new_keys);
  clear_bit(KEY_D, new_keys);
  clear_bit(KEY_W, new_keys);
  clear_bit(KEY_S, new_keys);

  if (new_ad_active)
    set_bit(new_ad_active, new_keys);
  if (new_ws_active)
    set_bit(new_ws_active, new_keys);

  for_each_set_bit(key, new_keys, KEY_CNT) {
    if (!test_bit(key, old_keys))
      input_report_key(kbd->input_dev, key, 1);
  }
  for_each_set_bit(key, old_keys, KEY_CNT) {
    if (!test_bit(key, new_keys))
      input_report_key(kbd->input_dev, key, 0);
  }

  bitmap_copy(old_keys, new_keys, KEY_CNT);
  kbd->ad_group.active = new_ad_active;
  kbd->ws_group.active = new_ws_active;

  input_sync(kbd->input_dev);

resubmit:
  usb_submit_urb(urb, GFP_ATOMIC);
}

// Init function to setup connection with KB
static int probe(struct usb_interface *usb_intf,
                 const struct usb_device_id *dev_id) {

  printk(KERN_INFO "SuperKBDrive: Connecting keyboard. . .");

  struct usb_device *usb_kb = interface_to_usbdev(usb_intf);
  struct keyboard_info *kbd;
  struct usb_host_interface *alt = usb_intf->cur_altsetting;
  struct usb_endpoint_descriptor *kb_endpoint;

  int i = 0;
  for (i = 0; i < alt->desc.bNumEndpoints; i++) {
    kb_endpoint = &alt->endpoint[i].desc;

    if (usb_endpoint_is_int_in(kb_endpoint))
      break;
  }

  if (i == alt->desc.bNumEndpoints) {
    dev_err(&usb_intf->dev, "No interrupt IN endpoint");
    return -ENODEV;
  }

  kbd = kzalloc(sizeof(*kbd), GFP_KERNEL);

  if (!kbd) {
    return -ENOMEM;
  }
  kbd->usb_dev = usb_get_dev(usb_kb);

  kbd->input_dev = input_allocate_device();

  if (!kbd->input_dev) {
    kfree(kbd);
    return -ENOMEM;
  }

  kbd->input_dev->name = "Cool kb driver";
  kbd->input_dev->phys = "usb-0000:00:14.0-2/input0";
  kbd->input_dev->id.bustype = BUS_USB;
  kbd->input_dev->id.vendor = le16_to_cpu(usb_kb->descriptor.idVendor);
  kbd->input_dev->id.product = le16_to_cpu(usb_kb->descriptor.idProduct);

  set_bit(EV_KEY, kbd->input_dev->evbit);
  set_bit(EV_REP, kbd->input_dev->evbit);

  for (i = 0; i < 256; i++)
    set_bit(i, kbd->input_dev->keybit);

  int error_code = input_register_device(kbd->input_dev);

  kbd->urb = usb_alloc_urb(0, GFP_KERNEL);
  if (!kbd->urb) {
    return ~ENOMEM;
  }

  unsigned int kb_pipe_id =
      usb_rcvintpipe(usb_kb, kb_endpoint->bEndpointAddress);

  // Initializing a buffer
  kbd->buffer = kmalloc(kb_endpoint->wMaxPacketSize, GFP_KERNEL);

  if (!kbd->buffer) {
    input_unregister_device(kbd->input_dev);
    usb_free_urb(kbd->urb);
    kfree(kbd);
    return ENOMEM;
  }

  // Initialing our URB for interrupt-type keyboard raw bytes transers
  usb_fill_int_urb(kbd->urb, usb_kb, kb_pipe_id, kbd->buffer,
                   kb_endpoint->wMaxPacketSize, KbCallback, kbd, 4);

  if (!kbd->usb_dev) {
    printk(
        KERN_ERR
        "Test keyboard driver error: Couldn't allocate mem for input device");
    return -ENOMEM;
  }

  int err_code = usb_submit_urb(kbd->urb, GFP_KERNEL);
  if (err_code) {
    usb_free_urb(kbd->urb);
    kfree(kbd->buffer);
    input_unregister_device(kbd->input_dev);
    kfree(kbd);
    printk(KERN_ERR "Couldn't submit urb. . .");
    return err_code;
  }

  usb_set_intfdata(usb_intf, kbd);

  printk(KERN_INFO "SuperKBDrive: keybord connected!");

  return 0;
}

// Disconnecting keyboard
static void disconnect(struct usb_interface *usb_interface) {
  struct keyboard_info *kbd_info = usb_get_intfdata(usb_interface);

  if (!kbd_info)
    return;
  printk(KERN_INFO "SuperKBDrive: Cleaning up the mess. . .");
  usb_set_intfdata(usb_interface, NULL);

  usb_kill_urb(kbd_info->urb); // Sync stopping URB

  usb_free_urb(kbd_info->urb); // Releasing URB

  kfree(kbd_info->buffer); // Deleting buffer

  input_unregister_device(kbd_info->input_dev); // Unbinding from input devices

  kfree(kbd_info);
  printk(KERN_INFO "SuperKBDrive: Done cleaning.");
  return;
}

// setting up driver to be registered
struct usb_driver kb_driver_info = {
    .name = "super_keyboard_driver",
    .probe = probe,
    .disconnect = disconnect,
    .id_table = smartbuy_id_table,
};

// Function to run when module is loaded in kernel
static int __init kb_driver_init(void) {
  usb_register(&kb_driver_info);
  printk(KERN_INFO "Super keyboard driver is loaded and initialized. . .\n");
  return 0;
}

// Function to run when module is unloaded from kernel
static void __exit kb_driver_exit(void) {
  usb_deregister(&kb_driver_info);

  printk(KERN_INFO "Super keyboard driver exitted and unloaded\n");

  return;
}

// Setting init and exit funcs for driver
module_init(kb_driver_init);
module_exit(kb_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SOFTDEP("linux-headers base-devel");
