"""
DM Fan — fan platform for ESPHome 2026.x
v3.0 — 3-stage WiFi handshake (non-blocking)
"""
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
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from . import dm_fan_ns

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "fan"]

DmFan = dm_fan_ns.class_("DmFan", fan.Fan, cg.Component, uart.UARTDevice)

CONF_UART_ID     = "uart_id"
CONF_TEMPERATURE = "temperature"
CONF_HUMIDITY    = "humidity"

CONFIG_SCHEMA = fan.fan_schema(DmFan).extend({
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    cv.Optional(CONF_HUMIDITY): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        device_class=DEVICE_CLASS_HUMIDITY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await fan.register_fan(var, config)

    if temp_conf := config.get(CONF_TEMPERATURE):
        sens = await sensor.new_sensor(temp_conf)
        cg.add(var.set_temperature_sensor(sens))

    if humi_conf := config.get(CONF_HUMIDITY):
        sens = await sensor.new_sensor(humi_conf)
        cg.add(var.set_humidity_sensor(sens))
