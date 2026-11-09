from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_TRIGGER_ID,
    CONF_NAME,
    CONF_DELAY,
    CONF_FILTERS,
)
from esphome.core import CORE
from esphome.cpp_generator import Pvariable
from esphome.cpp_types import Component
from esphome.components.binary_sensor import BinarySensor


MULTI_CONF = True
DEPENDENCIES = []
AUTO_LOAD = ["binary_sensor"]

# Component
const_string_ref = cg.std_string.operator("ref").operator("const")
sensor_aggregation_ns = cg.esphome_ns.namespace("sensor_aggregation")
SensorAggregationComponent = sensor_aggregation_ns.class_("SensorAggregationComponent", Component)
InputCalibrationConfig = sensor_aggregation_ns.class_("InputCalibrationConfig")


SensorStateChangeTrigger = sensor_aggregation_ns.class_("SensorStateChangeTrigger", automation.Trigger.template(const_string_ref, cg.bool_))
CalibrationTrigger = sensor_aggregation_ns.class_("CalibrationTrigger", automation.Trigger.template())


# Configuration
CONF_INPUTS = "inputs"
CONF_INPUT_BINARY_SENSOR = "binary_sensor_id"
CONF_TAG = "tag"
CONF_DETECT_DELAY = "detect_delay"
CONF_RELEASE_DELAY = "release_delay"
CONF_CHECK_INTERVAL = "check_interval"
CONF_SENSOR_TYPE = "sensor_type"
CONF_OUTPUT_TYPE = "output_type"

CONF_INPUT_CALIBRATIONS = "calibrations"
CONF_CALIBRATION_DELAY = "calibration_delay"
CONF_FIRST_TIME_CALIBRATION_DELAY = "first_time_calibration_delay"
CONF_ON_CALIBRATE = "on_calibrate"
CONF_EXCLUDE_FROM_CLEAR = "exclude_from_clear"
CONF_ON_STATE_CHANGE = "on_state_change"

# Enums
SensorType = sensor_aggregation_ns.enum('SensorType')
SENSOR_TYPES = {
    "presence":  SensorType.SENSOR_TYPE__PRESENCE,
    "movement": SensorType.SENSOR_TYPE__MOVEMENT,
    "movement_towards": SensorType.SENSOR_TYPE__MOVEMENT_TOWARDS,
    "movement_away": SensorType.SENSOR_TYPE__MOVEMENT_AWAY,
}


OUTPUT_TYPES = {
    "presence":  SensorType.SENSOR_TYPE__PRESENCE,
    "movement": SensorType.SENSOR_TYPE__MOVEMENT,
    "movement_towards": SensorType.SENSOR_TYPE__MOVEMENT_TOWARDS,
    "movement_away": SensorType.SENSOR_TYPE__MOVEMENT_AWAY,
}


def validate_calibration_config(config):
    """Validate that at least one calibration delay is configured."""
    calibration_delay = config.get(CONF_CALIBRATION_DELAY, cv.TimePeriod(milliseconds=0))
    first_time_delay = config.get(CONF_FIRST_TIME_CALIBRATION_DELAY, cv.TimePeriod(milliseconds=0))
    
    if calibration_delay.total_milliseconds == 0 and first_time_delay.total_milliseconds == 0:
        raise cv.Invalid("At least one of 'calibration_delay' or 'first_time_calibration_delay' must be set to a non-zero value")
    
    return config

CALIBRATION_INPUT_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InputCalibrationConfig),
            cv.Optional(CONF_CALIBRATION_DELAY, default="0s"): cv.time_period,
            cv.Optional(CONF_FIRST_TIME_CALIBRATION_DELAY, default="0s"): cv.time_period,
            cv.Optional(CONF_EXCLUDE_FROM_CLEAR, default=[]): cv.ensure_list(cv.use_id(BinarySensor)),
            cv.Optional(CONF_ON_CALIBRATE): automation.validate_automation({
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CalibrationTrigger),
            }),
        }
    ),
    validate_calibration_config
)

# Schema for input sensor configuration
INPUT_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_INPUT_BINARY_SENSOR): cv.use_id(BinarySensor),
        cv.Required(CONF_SENSOR_TYPE): cv.enum(SENSOR_TYPES, lower=True),
        cv.Optional(CONF_DETECT_DELAY, default="0s"): cv.time_period,
        cv.Optional(CONF_RELEASE_DELAY, default="0s"): cv.time_period,
        cv.Optional(CONF_TAG, default=""): cv.string,
    }
)

# Main component configuration schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SensorAggregationComponent),
        cv.Required(CONF_INPUTS): cv.ensure_list(INPUT_SENSOR_SCHEMA),
        cv.Optional(CONF_INPUT_CALIBRATIONS): cv.ensure_list(CALIBRATION_INPUT_SCHEMA),
        cv.Optional(CONF_ON_STATE_CHANGE): automation.validate_automation({
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(SensorStateChangeTrigger),
        }),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)


    if CONF_ON_STATE_CHANGE in config:
        for trig_config in config[CONF_ON_STATE_CHANGE]:
            trigger = cg.new_Pvariable(trig_config[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, [(const_string_ref, "tag"), (cg.bool_, "state")], trig_config)


    for input_config in config[CONF_INPUTS]:
        input_var = await cg.get_variable(input_config[CONF_INPUT_BINARY_SENSOR])
        
        # Register basic sensor configuration
        cg.add(var.register_input(
            input_var,
            input_config[CONF_SENSOR_TYPE],
            input_config[CONF_DETECT_DELAY].total_milliseconds,
            input_config[CONF_RELEASE_DELAY].total_milliseconds,
            input_config[CONF_TAG]
        ))
        

    for cal_config in config.get(CONF_INPUT_CALIBRATIONS, []):
        # resolve exclude list
        exclude_ids = cal_config.get(CONF_EXCLUDE_FROM_CLEAR, [])
        exclude_vars = []
        for sensor_id in exclude_ids:
            exclude_vars.append(await cg.get_variable(sensor_id))

        cal_var = cg.new_Pvariable(
            cal_config[CONF_ID],
            cal_config[CONF_CALIBRATION_DELAY].total_milliseconds,
            cal_config[CONF_FIRST_TIME_CALIBRATION_DELAY].total_milliseconds,
            exclude_vars
        )


        for trig_config in cal_config.get(CONF_ON_CALIBRATE, []):
            trigger = cg.new_Pvariable(trig_config[CONF_TRIGGER_ID], cal_var)
            await automation.build_automation(trigger, [], trig_config)



        cg.add(var.register_input_calibration(cal_var))
