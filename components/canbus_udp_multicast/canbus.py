from itertools import groupby
import logging
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.core import CORE
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.components.canbus import (
    CANBUS_SCHEMA,
    CONF_CANBUS_ID,
    CanbusComponent,
    register_canbus,
)

logger = logging.getLogger(__name__)


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
    cv.Optional(CONF_MULTICAST_IP, default="232.10.11.12"): cv.string,
    cv.Optional(CONF_MULTICAST_PORT, default=43113): cv.int_range(min=0, max=65535),
    cv.Optional(CONF_IF_KEY, default="WIFI_STA_DEF"): cv.string,
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
    if CORE.target_framework == 'esp-idf':
        add_idf_sdkconfig_option("CONFIG_LWIP_SO_RCVBUF", True)
    else:
        logger.warning("RCVBUF cannot be set on arduino")
