import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ANALOG


AUTO_LOAD = ['sensor']
MULTI_CONF = True

ADCPLEX_ns = cg.esphome_ns.namespace('adcplex')
ADCPLEXComponent = ADCPLEX_ns.class_('ADCPLEXComponent', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ADCPLEXComponent),
    cv.Required(CONF_ANALOG): cv.string,
}).extend(cv.COMPONENT_SCHEMA))


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)