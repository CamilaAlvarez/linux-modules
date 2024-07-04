#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../include/mq135-data.h"

int main(int argc, char **argv)
{
    struct mq135_measurement measurement;
    int fd = 0;
    if ((fd = open(MQ135_DEVICE, O_RDONLY)) < -1)
        return 1;
    read(fd, &measurement, sizeof(struct mq135_measurement));
    if (measurement.read_data)
        printf("Air quality: %d\n", measurement.air_quality);
    else
        printf("Invalid reading\n");
    return 0;
}
