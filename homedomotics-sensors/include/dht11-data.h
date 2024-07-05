#ifndef DHT11_DATA
#define DHT11_DATA

#define DHT11_CHAR_DEVICE "/dev/dht11_module"

struct dht11_measurement
{
    unsigned char successful;
    unsigned char humidity;
    unsigned char humidity_decimal;
    unsigned char temperature;
    unsigned char temperature_decimal;
};
#endif