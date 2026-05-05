import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import sky_thermal_ns, SkyThermal

CONF_SKY_THERMAL_ID = "sky_thermal_id"
CONF_RAIN_SENSOR = "rain_sensor"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_SKY_THERMAL_ID): cv.use_id(SkyThermal),
    cv.Required(CONF_RAIN_SENSOR): cv.use_id(binary_sensor.BinarySensor),
})

def to_code(config):
    parent = yield cg.get_variable(config[CONF_SKY_THERMAL_ID])
    rain = yield cg.get_variable(config[CONF_RAIN_SENSOR])
    cg.add(parent.set_rain_sensor(rain))
