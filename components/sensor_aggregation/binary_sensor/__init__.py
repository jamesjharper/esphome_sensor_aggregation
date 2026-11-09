"""Binary sensor platform for sensor aggregation component."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_MOTION,
    DEVICE_CLASS_OCCUPANCY,
    CONF_DEVICE_CLASS
)
from .. import (
    sensor_aggregation_ns,
    SensorAggregationComponent,
    CONF_OUTPUT_TYPE,
    OUTPUT_TYPES,
)

DEPENDENCIES = ["sensor_aggregation"]

# Platform configuration schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(SensorAggregationComponent),
        cv.Optional("presence"): binary_sensor.binary_sensor_schema().extend(
            {
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_OCCUPANCY): cv.string,
                cv.Optional(CONF_OUTPUT_TYPE, default="presence"): cv.enum(OUTPUT_TYPES, lower=True),
            }
        ),
        cv.Optional("movement"): binary_sensor.binary_sensor_schema().extend(
            {
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_MOTION): cv.string,
                cv.Optional(CONF_OUTPUT_TYPE, default="movement"): cv.enum(OUTPUT_TYPES, lower=True),
            }
        ),
        cv.Optional("movement_towards"): binary_sensor.binary_sensor_schema().extend(
            {
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_MOTION): cv.string,
                cv.Optional(CONF_OUTPUT_TYPE, default="movement_towards"): cv.enum(OUTPUT_TYPES, lower=True),
            }
        ),
        cv.Optional("movement_away"): binary_sensor.binary_sensor_schema().extend(
            {
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_MOTION): cv.string,
                cv.Optional(CONF_OUTPUT_TYPE, default="movement_away"): cv.enum(OUTPUT_TYPES, lower=True),
            }
        ),
    }
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_ID])

    for key in ["presence", "movement", "movement_towards", "movement_away"]:
        if key not in config:
            continue
        conf = config[key]
        var = cg.new_Pvariable(conf[CONF_ID])
        await binary_sensor.register_binary_sensor(var, conf)

        if key == "presence":
            cg.add(hub.register_presence_binary_sensor(var))
        elif key == "movement":
            cg.add(hub.register_movement_binary_sensor(var))
        elif key == "movement_towards":
            cg.add(hub.register_movement_towards_binary_sensor(var))
        elif key == "movement_away":
            cg.add(hub.register_movement_away_binary_sensor(var))
