import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, uart, sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_HUMIDITY,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    STATE_CLASS_MEASUREMENT,
)
from . import dm_fan_ns

DmFan = dm_fan_ns.class_("DmFan", fan.Fan, cg.Component, uart.UARTDevice)

CONF_UART_ID = "uart_id"
CONF_TEMPERATURE = "temperature"
CONF_HUMIDITY = "humidity"

# ÄNDERUNG HIER: Nutzen der modernen fan.fan_schema()-Funktion anstelle der gelöschten Variable
CONFIG_SCHEMA = fan.fan_schema(DmFan).extend({
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    cv.Optional(CONF_HUMIDITY): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        device_class=DEVICE_CLASS_HUMIDITY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await fan.register_fan(var, config)

    # ---------------- SENSORS BINDING ----------------
    if t := config.get(CONF_TEMPERATURE):
        s = await sensor.new_sensor(t)
        cg.add(var.set_temperature_sensor(s))

    if h := config.get(CONF_HUMIDITY):
        s = await sensor.new_sensor(h)
        cg.add(var.set_humidity_sensor(s))
