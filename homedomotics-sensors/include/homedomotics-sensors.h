#ifndef HOMEDOMOTICS_SENSORS
#define HOMEDOMOTICS_SENSORS

#include "ky004-data.h"
#include "mq135-data.h"
#include "dht11-data.h"

typedef struct mq135_measurement AirQuality;
typedef struct dht11_measurement TemperatureHumidity;

int wait_to_read(void);
AirQuality *read_air_quality(void);
TemperatureHumidity *read_temperature_humidity(void);

#endif