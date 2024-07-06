import homedomotics

s = homedomotics.Sensors()
data = s.read_sensors()
print(data.read_temperature_humidity)
print(data.temperature)
print(data.humidity)
print(data.read_air_quality)
print(data.air_quality)
