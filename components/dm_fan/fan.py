"""
DM Fan — fan platform for ESPHome 2026.x
v4.0 — MCU version readout, boot response parsing, BLE remote beacon (Phase 1)
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan, uart, sensor, text_sensor, esp32_ble_tracker
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
AUTO_LOAD = ["sensor", "text_sensor", "fan"]

DmFan = dm_fan_ns.class_("DmFan", fan.Fan, cg.Component, uart.UARTDevice)

CONF_UART_ID          = "uart_id"
CONF_TEMPERATURE      = "temperature"
CONF_HUMIDITY         = "humidity"
CONF_MCU_VERSION      = "mcu_version"
CONF_LOG_RAW_FRAMES   = "log_raw_frames"
CONF_BLE_REMOTE       = "ble_remote"
CONF_BLE_REPORT_TO_MCU = "ble_report_to_mcu"

_BASE_SCHEMA = fan.fan_schema(DmFan).extend({
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Optional(CONF_LOG_RAW_FRAMES, default=False): cv.boolean,
    # BLE remote (Phase 1: receive + decode + log). Requires a BLE tracker in the
    # config (bluetooth_proxy or esp32_ble_tracker). ble_report_to_mcu is the
    # experimental UART forward to the MCU (resource 0x1F41) — off by default.
    cv.Optional(CONF_BLE_REMOTE, default=False): cv.boolean,
    cv.Optional(CONF_BLE_REPORT_TO_MCU, default=False): cv.boolean,
    cv.Optional(CONF_MCU_VERSION): text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon="mdi:chip",
    ),
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

# The BLE tracker reference is only injected when ble_remote is enabled, so
# configs without any bluetooth component keep validating (the tracker's
# use_id would otherwise fail with "Couldn't find ID").
_BLE_SCHEMA = _BASE_SCHEMA.extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


def _dm_fan_schema(config):
    if isinstance(config, dict) and config.get(CONF_BLE_REMOTE):
        return _BLE_SCHEMA(config)
    return _BASE_SCHEMA(config)


CONFIG_SCHEMA = _dm_fan_schema


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await fan.register_fan(var, config)

    cg.add(var.set_log_raw_frames(config[CONF_LOG_RAW_FRAMES]))

    if mcuv_conf := config.get(CONF_MCU_VERSION):
        sens = await text_sensor.new_text_sensor(mcuv_conf)
        cg.add(var.set_mcu_version_sensor(sens))

    if temp_conf := config.get(CONF_TEMPERATURE):
        sens = await sensor.new_sensor(temp_conf)
        cg.add(var.set_temperature_sensor(sens))

    if humi_conf := config.get(CONF_HUMIDITY):
        sens = await sensor.new_sensor(humi_conf)
        cg.add(var.set_humidity_sensor(sens))

    if config[CONF_BLE_REMOTE]:
        cg.add(var.set_ble_remote(True))
        cg.add(var.set_ble_report_to_mcu(config[CONF_BLE_REPORT_TO_MCU]))
        # Register as a BLE advertisement listener on the tracker hub.
        await esp32_ble_tracker.register_ble_device(var, config)
