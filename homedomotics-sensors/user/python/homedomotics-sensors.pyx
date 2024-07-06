# distutils: sources = ../lib/homedomotics-sensors.c
# distutils: include_dirs = ../../include

import cython
from cython.cimports.libc.stdlib import free
from cython.cimports.libc import math
cimport chomedomotics_sensors

# cdef class allows storing arbitrary c types in its fields
cdef class SensorsData:
    cdef bint _read_temperature_humidity
    cdef float _temperature
    cdef float _humidity 
    cdef bint _read_air_quality
    cdef int _air_quality

    def __cinit__(self, read_temp_hum, temperature, humidity, read_air_quality, air_quality):
        self._read_temperature_humidity = read_temp_hum
        self._temperature = temperature
        self._humidity = humidity
        self._read_air_quality = read_air_quality
        self._air_quality = air_quality
    
    @property
    def read_temperature_humidity(self):
        return self._read_temperature_humidity

    @property
    def temperature(self):
        return self._temperature

    @property
    def humidity(self):
        return self._humidity

    @property
    def read_air_quality(self):
        return self._read_air_quality
    @property
    def air_quality(self):
        return self._air_quality

cdef closest_power_ten(int v):
    return math.pow(10, math.floor(math.log10(v)));

cdef class Sensors:
    def __cinit__(self):
        pass

    # Can be called from python, c and cython
    # cdef can be called from cython and C
    # def can only be called from python
    cpdef wait_for_read(self):
        return chomedomotics_sensors.wait_to_read()

    cpdef read_sensors(self):
        cdef chomedomotics_sensors.TemperatureHumidity *read_temperature_data = chomedomotics_sensors.read_temperature_humidity()
        if read_temperature_data is cython.NULL:
            raise IOError("Failed at reading temperature and humidity")
        cdef chomedomotics_sensors.AirQuality *air_quality_data = chomedomotics_sensors.read_air_quality()
        if air_quality_data is cython.NULL:
            free(read_temperature_data)
            raise IOError("Failed at reading air quality")
        return_data = SensorsData(read_temperature_data.successful,
                                  read_temperature_data.temperature +
                                  read_temperature_data.temperature_decimal*1.0/(10*closest_power_ten(read_temperature_data.temperature_decimal)),
                                  read_temperature_data.humidity +
                                  read_temperature_data.humidity_decimal*1.0/(10*closest_power_ten(read_temperature_data.humidity_decimal)),
                                  air_quality_data.read_data,
                                  air_quality_data.air_quality)
        free(read_temperature_data)
        free(air_quality_data)
        return return_data


