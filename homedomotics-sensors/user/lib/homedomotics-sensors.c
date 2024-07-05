#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include "../include/homedomotics-sensors.h"

int wait_to_read(void)
{
    int fd, ret;
    fd_set readfs;

    fd = open(KY004_DEVICE, O_RDONLY);
    if (fd < -1)
        return 1;
    FD_ZERO(&readfs);
    FD_SET(fd, &readfs);
    ret = select(fd + 1, &readfs, NULL, NULL, NULL);
    if (ret == -1)
    {
        return 0;
    }
    return 1;
}
AirQuality *read_air_quality(void)
{
    AirQuality *measurement = malloc(sizeof(AirQuality));
    if (measurement == NULL)
        return NULL;
    int fd = 0;
    if ((fd = open(MQ135_DEVICE, O_RDONLY)) < -1)
        return NULL;
    read(fd, &measurement, sizeof(struct mq135_measurement));
    return measurement;
}
TemperatureHumidity *read_temperature_humidity(void)
{
    TemperatureHumidity *measurement = malloc(sizeof(TemperatureHumidity));
    if (measurement == NULL)
        return NULL;
    int fd = 0;
    if ((fd = open(DHT11_CHAR_DEVICE, O_RDONLY)) < -1)
        return NULL;
    read(fd, measurement, sizeof(struct dht11_measurement));
    return measurement;
}