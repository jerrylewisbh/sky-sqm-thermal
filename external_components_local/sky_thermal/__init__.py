import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

sky_thermal_ns = cg.esphome_ns.namespace('sky_thermal')
SkyThermal = sky_thermal_ns.class_('SkyThermal', cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SkyThermal),
}).extend(cv.polling_component_schema('5s')).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    
    cg.add_library("Adafruit MLX90640", "1.1.1")
    cg.add_library("Adafruit BusIO", "1.14.1")
    cg.add_library("Adafruit BME280 Library", None)
    cg.add_library("Adafruit TSL2561", None)
    cg.add_library("Adafruit TSL2591 Library", None)
    cg.add_library("Adafruit Unified Sensor", None)
    cg.add_library("Wire", None)
    cg.add_library("SPI", None)
