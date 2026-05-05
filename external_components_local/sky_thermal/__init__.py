import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

sky_thermal_ns = cg.esphome_ns.namespace('sky_thermal')
SkyThermal = sky_thermal_ns.class_('SkyThermal', cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SkyThermal),
}).extend(cv.polling_component_schema('5s'))

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
