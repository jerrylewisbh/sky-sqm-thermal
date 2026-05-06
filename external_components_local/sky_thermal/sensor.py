import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID, UNIT_CELSIUS, ICON_THERMOMETER, DEVICE_CLASS_TEMPERATURE
from . import sky_thermal_ns, SkyThermal

CONF_SKY_THERMAL_ID = "sky_thermal_id"
CONF_MEAN_TEMP = "mean_temp"
CONF_MIN_TEMP = "min_temp"
CONF_MAX_TEMP = "max_temp"
CONF_CENTER_TEMP = "center_temp"
CONF_BME_TEMP = "bme_temp"
CONF_BME_HUMIDITY = "bme_humidity"
CONF_BME_PRESSURE = "bme_pressure"
CONF_TSL_ILLUMINANCE = "tsl_illuminance"
CONF_WIND_SPEED = "wind_speed"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_SKY_THERMAL_ID): cv.use_id(SkyThermal),
    cv.Optional(CONF_MEAN_TEMP): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, device_class=DEVICE_CLASS_TEMPERATURE, accuracy_decimals=1),
    cv.Optional(CONF_MIN_TEMP): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, device_class=DEVICE_CLASS_TEMPERATURE, accuracy_decimals=1),
    cv.Optional(CONF_MAX_TEMP): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, device_class=DEVICE_CLASS_TEMPERATURE, accuracy_decimals=1),
    cv.Optional(CONF_CENTER_TEMP): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, device_class=DEVICE_CLASS_TEMPERATURE, accuracy_decimals=1),
    cv.Optional(CONF_BME_TEMP): sensor.sensor_schema(unit_of_measurement="°C", icon="mdi:thermometer", device_class="temperature", accuracy_decimals=1),
    cv.Optional(CONF_BME_HUMIDITY): sensor.sensor_schema(unit_of_measurement="%", icon="mdi:water-percent", device_class="humidity", accuracy_decimals=1),
    cv.Optional(CONF_BME_PRESSURE): sensor.sensor_schema(unit_of_measurement="hPa", icon="mdi:gauge", device_class="pressure", accuracy_decimals=1),
    cv.Optional(CONF_TSL_ILLUMINANCE): sensor.sensor_schema(unit_of_measurement="lx", icon="mdi:brightness-5", device_class="illuminance", accuracy_decimals=0),
    cv.Optional(CONF_WIND_SPEED): cv.use_id(sensor.Sensor),
})

def to_code(config):
    parent = yield cg.get_variable(config[CONF_SKY_THERMAL_ID])

    if CONF_MEAN_TEMP in config:
        sens = yield sensor.new_sensor(config[CONF_MEAN_TEMP])
        cg.add(parent.set_mean_sensor(sens))
    if CONF_MIN_TEMP in config:
        sens = yield sensor.new_sensor(config[CONF_MIN_TEMP])
        cg.add(parent.set_min_sensor(sens))
    if CONF_MAX_TEMP in config:
        sens = yield sensor.new_sensor(config[CONF_MAX_TEMP])
        cg.add(parent.set_max_sensor(sens))
    if CONF_CENTER_TEMP in config:
        sens = yield sensor.new_sensor(config[CONF_CENTER_TEMP])
        cg.add(parent.set_center_sensor(sens))
    if CONF_BME_TEMP in config:
        sens = yield sensor.new_sensor(config[CONF_BME_TEMP])
        cg.add(parent.set_bme_temp_sensor(sens))
    if CONF_BME_HUMIDITY in config:
        sens = yield sensor.new_sensor(config[CONF_BME_HUMIDITY])
        cg.add(parent.set_bme_humidity_sensor(sens))
    if CONF_BME_PRESSURE in config:
        sens = yield sensor.new_sensor(config[CONF_BME_PRESSURE])
        cg.add(parent.set_bme_pressure_sensor(sens))
    if CONF_TSL_ILLUMINANCE in config:
        sens = yield sensor.new_sensor(config[CONF_TSL_ILLUMINANCE])
        cg.add(parent.set_tsl_illuminance_sensor(sens))
    if CONF_WIND_SPEED in config:
        wind = yield cg.get_variable(config[CONF_WIND_SPEED])
        cg.add(parent.set_wind_sensor(wind))
