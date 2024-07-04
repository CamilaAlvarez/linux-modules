#ifndef MQ135_DATA
#define MQ135_DATA

#define MQ135_DEVICE "/dev/mq135_device"

#define ADS1115_ADDRESS (0x48)
#define CONFIG_REGISTER (0x01)
#define HI_THRESH_REGISTER (0x03)
#define LO_THRESH_RWGISTER (0x02)
#define CONVERSION_REGISTER (0x00)
// LSB
#define CONFIG_COMP_QUE_1CONV (0x0000)
#define CONFIG_COMP_NOLAT (0x0000)
#define CONFIG_COMP_POL_LOW (0x0000)
#define CONFIG_COMP_MODE_TRAD (0x0000)
#define CONFIG_DATA_RATE (0x0080)
// MSB
#define CONFIG_MODE (0x0100)
#define CONFIG_PGA_DEFAULT (0x0400)
#define CONFIG_MUX_A0 (0x4000)
#define CONFIG_OS (0x8000)

struct mq135_measurement
{
    int air_quality;
    unsigned char read_data;
};

#endif
