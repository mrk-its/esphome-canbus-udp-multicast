from itertools import groupby
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.components.canbus import (
    CANBUS_SCHEMA,
    CONF_ON_FRAME,
    CONF_CANBUS_ID,
    CONF_CAN_ID,
    CONF_CAN_ID_MASK,
    CONF_USE_EXTENDED_ID,
    CONF_REMOTE_TRANSMISSION_REQUEST,
    CanbusComponent,
    CanbusTrigger,
    register_canbus,
    validate_id,
)

CONF_MULTICAST_IP = "multicast_ip"
CONF_MULTICAST_PORT = "multicast_port"
CONF_IF_KEY = "if_key"

ns = cg.esphome_ns.namespace('canbus_udp_multicast')
CanbusUdpMulticast = ns.class_(
    'CanbusUdpMulticast',
    CanbusComponent,
)

DEPENDENCIES = ["api"]

CONFIG_SCHEMA = CANBUS_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(CanbusUdpMulticast),
    cv.Optional(CONF_CANBUS_ID): cv.use_id(CanbusComponent),
    cv.Optional(CONF_MULTICAST_IP, default="232.10.11.12"): cv.All(cv.ipv4, cv.string),
    cv.Optional(CONF_MULTICAST_PORT, default=43113): cv.int_range(min=0, max=65535),
    cv.Optional(CONF_IF_KEY, default="WIFI_STA_DEF"): cv.string,

    cv.Optional(CONF_ON_FRAME): automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CanbusTrigger),
            cv.Required(CONF_CAN_ID): cv.int_range(min=0, max=0x1FFFFFFF),
            cv.Optional(CONF_CAN_ID_MASK, default=0x1FFFFFFF): cv.int_range(
                min=0, max=0x1FFFFFFF
            ),
            cv.Optional(CONF_USE_EXTENDED_ID, default=False): cv.boolean,
            cv.Optional(CONF_REMOTE_TRANSMISSION_REQUEST): cv.boolean,
        },
        validate_id,
    ),
})


async def to_code(config):
    if "canbus_id" in config:
        canbus = await cg.get_variable(config["canbus_id"])
    else:
        canbus = cg.RawExpression('nullptr')

    canbus_udp_multicast = cg.new_Pvariable(
        config[CONF_ID],
        canbus,
        config[CONF_MULTICAST_IP],
        config[CONF_MULTICAST_PORT],
        config[CONF_IF_KEY],
    )
    await register_canbus(canbus_udp_multicast, config)

    for conf in config.get(CONF_ON_FRAME, []):
        can_id = conf[CONF_CAN_ID]
        can_id_mask = conf[CONF_CAN_ID_MASK]
        ext_id = conf[CONF_USE_EXTENDED_ID]
        trigger = cg.new_Pvariable(
            conf[CONF_TRIGGER_ID], canbus, can_id, can_id_mask, ext_id
        )
        if CONF_REMOTE_TRANSMISSION_REQUEST in conf:
            cg.add(
                trigger.set_remote_transmission_request(
                    conf[CONF_REMOTE_TRANSMISSION_REQUEST]
                )
            )
        await cg.register_component(trigger, conf)
        await automation.build_automation(
            trigger,
            [
                (cg.std_vector.template(cg.uint8), "x"),
                (cg.uint32, "can_id"),
                (cg.bool_, "remote_transmission_request"),
            ],
            conf,
        )
    add_idf_sdkconfig_option("CONFIG_LWIP_SO_RCVBUF", True)
