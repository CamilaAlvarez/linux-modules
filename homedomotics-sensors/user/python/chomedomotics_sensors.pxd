cdef extern from "../../include/homedomotics-sensors.h":
    ctypedef struct Sensors:
        pass
    ctypedef struct AirQuality:
        int air_quality;
        bint read_data;
    ctypedef struct TemperatureHumidity:
        bint successful;
        int humidity;
        int humidity_decimal;
        int temperature;
        int temperature_decimal;

    bint wait_to_read();
    AirQuality *read_air_quality();
    TemperatureHumidity *read_temperature_humidity();
    