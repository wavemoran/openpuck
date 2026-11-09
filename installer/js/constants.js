// Shared constants for the OpenPuck installer. Values mirror tools/flash.py.

/** USB ids of the ESP32-C3 built-in USB-Serial-JTAG port. */
export const USB_VID = 0x303a;
export const USB_PID = 0x1001;
export const USB_FILTERS = [{ usbVendorId: USB_VID, usbProductId: USB_PID }];

/** 4 MB flash and the flash ID (manufacturer/type/size) we are willing to write to. */
export const FLASH_SIZE = 4 * 1024 * 1024;
export const EXPECTED_FLASH_ID = 0x164020;

export const PARTITION_TABLE_OFFSET = 0x8000;
/** Bytes of the partition table compared to detect a layout change. */
export const PARTITION_TABLE_COMPARE_LEN = 0xc00;
export const PARTITION_ENTRY_SIZE = 32;
export const PARTITION_MAGIC = [0xaa, 0x50];
export const IMAGE_MAGIC = 0xe9;
export const PARTITION_TYPE_DATA = 1;

/** GPIO pins: power latch (output) and power button (reads 1 while pressed). */
export const LATCH_PIN = 7;
export const BUTTON_PIN = 10;

export const IO_MUX_GPIO0 = 0x60009004; // IO_MUX_GPIOn_REG = this + 4 * n
export const GPIO_OUT_W1TS = 0x60004008;
export const GPIO_ENABLE_W1TS = 0x60004024;
export const GPIO_IN = 0x6000403c;
export const GPIO_FUNC0_OUT_SEL = 0x60004554; // + 4 * n
export const MCU_SEL_GPIO = 1 << 12;
export const MCU_SEL_MASK = 0x7 << 12;
export const FUN_IE = 1 << 9;
export const OUT_SEL_SIMPLE_GPIO = 0x80;

/** Serial buffer size; Chrome overflows with the 255 byte default. */
export const SERIAL_BUFFER_SIZE = 1048576;
export const BAUD_RATE = 115200;

/** Timing (milliseconds). */
export const BUTTON_RELEASE_TIMEOUT_MS = 30000;
export const BUTTON_POLL_MS = 100;
export const BUTTON_SETTLE_MS = 500;
export const PUCK_WAIT_TIMEOUT_MS = 600000;
export const PUCK_SETTLE_MS = 2000;
export const RESET_PULSE_MS = 200;
export const DISCONNECT_TIMEOUT_MS = 5000;
export const MD5_VERIFY_ATTEMPTS = 3;
