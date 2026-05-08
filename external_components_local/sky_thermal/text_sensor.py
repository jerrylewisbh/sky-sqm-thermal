import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from . import sky_thermal_ns, SkyThermal

CONF_SKY_THERMAL_ID = "sky_thermal_id"
CONF_IMAGE = "image"
CONF_SKY_CONDITION = "sky_condition"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_SKY_THERMAL_ID): cv.use_id(SkyThermal),
    cv.Optional(CONF_IMAGE): text_sensor.text_sensor_schema(),
    cv.Optional(CONF_SKY_CONDITION): text_sensor.text_sensor_schema(),
})

def to_code(config):
    parent = yield cg.get_variable(config[CONF_SKY_THERMAL_ID])
    if CONF_IMAGE in config:
        ts = yield text_sensor.new_text_sensor(config[CONF_IMAGE])
        cg.add(parent.set_image_sensor(ts))
    if CONF_SKY_CONDITION in config:
        ts = yield text_sensor.new_text_sensor(config[CONF_SKY_CONDITION])
        cg.add(parent.set_sky_condition_sensor(ts))
