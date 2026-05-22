import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

sky_thermal_ns = cg.esphome_ns.namespace('sky_thermal')
SkyThermal = sky_thermal_ns.class_('SkyThermal', cg.PollingComponent)

CONF_SQM_CALIBRATION_K = "sqm_calibration_k"
CONF_BME_PRESSURE_OFFSET = "bme_pressure_offset"
CONF_BME_HUMIDITY_OFFSET = "bme_humidity_offset"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SkyThermal),
    cv.Optional(CONF_SQM_CALIBRATION_K, default=19.5): cv.float_,
    cv.Optional(CONF_BME_PRESSURE_OFFSET, default=0.0): cv.float_,
    cv.Optional(CONF_BME_HUMIDITY_OFFSET, default=0.0): cv.float_,
}).extend(cv.polling_component_schema('5s')).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    cg.add(var.set_sqm_calibration_k(config[CONF_SQM_CALIBRATION_K]))
    cg.add(var.set_bme_pressure_offset(config[CONF_BME_PRESSURE_OFFSET]))
    cg.add(var.set_bme_humidity_offset(config[CONF_BME_HUMIDITY_OFFSET]))

    cg.add_library("Adafruit MLX90640", "1.1.1")
    cg.add_library("Adafruit BusIO", "1.14.1")
    cg.add_library("Adafruit BME280 Library", None)
    cg.add_library("Adafruit TSL2561", None)
    cg.add_library("Adafruit TSL2591 Library", None)
    cg.add_library("Adafruit Unified Sensor", None)
    cg.add_library("Wire", None)
    cg.add_library("SPI", None)
