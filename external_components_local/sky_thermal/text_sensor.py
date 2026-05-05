import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from . import sky_thermal_ns, SkyThermal

CONF_SKY_THERMAL_ID = "sky_thermal_id"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend({
    cv.GenerateID(CONF_SKY_THERMAL_ID): cv.use_id(SkyThermal),
})

def to_code(config):
    parent = yield cg.get_variable(config[CONF_SKY_THERMAL_ID])
    var = yield text_sensor.new_text_sensor(config)
    cg.add(parent.set_image_sensor(var))
