#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../include/dht11-data.h"

#define DHT11_CHAR_DEVICE "/dev/dht11_module"

int main(int argc, char **argv)
{
    struct dht11_measurement measurement;
    int fd = 0;
    if ((fd = open(DHT11_CHAR_DEVICE, O_RDONLY)) < -1)
        return 1;
    read(fd, &measurement, sizeof(struct dht11_measurement));
    if (measurement.successful)
        printf("Humidity: %d.%d\%RH Temperature: %d.%dC\n",
               measurement.humidity,
               measurement.humidity_decimal,
               measurement.temperature,
               measurement.temperature_decimal);
    else
        printf("Invalid reading\n");
    return 0;
}
