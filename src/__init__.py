"""ESPHome Slideshow Component"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import online_image, http_request
from esphome.const import (
    CONF_ID,
    CONF_URL,
)

DEPENDENCIES = ["http_request", "online_image"]
AUTO_LOAD = ["online_image"]

CONF_BACKEND_URL = "backend_url"
CONF_DEVICE_ID = "device_id"
CONF_ADVANCE_INTERVAL = "advance_interval"
CONF_QUEUE_REFRESH_INTERVAL = "queue_refresh_interval"
CONF_AUTO_ADVANCE = "auto_advance"
CONF_IMAGE_SLOTS = "image_slots"
CONF_ON_ADVANCE = "on_advance"
CONF_ON_IMAGE_READY = "on_image_ready"
CONF_ON_QUEUE_UPDATED = "on_queue_updated"
CONF_ON_ERROR = "on_error"

slideshow_ns = cg.esphome_ns.namespace("slideshow")
SlideshowComponent = slideshow_ns.class_("SlideshowComponent", cg.Component)

# Triggers
OnAdvanceTrigger = slideshow_ns.class_("OnAdvanceTrigger", automation.Trigger.template(cg.size_t))
OnImageReadyTrigger = slideshow_ns.class_("OnImageReadyTrigger", automation.Trigger.template(cg.size_t, cg.bool_))
OnQueueUpdatedTrigger = slideshow_ns.class_("OnQueueUpdatedTrigger", automation.Trigger.template(cg.size_t))
OnErrorTrigger = slideshow_ns.class_("OnErrorTrigger", automation.Trigger.template(cg.std_string))

# Actions
AdvanceAction = slideshow_ns.class_("AdvanceAction", automation.Action)
PreviousAction = slideshow_ns.class_("PreviousAction", automation.Action)
PauseAction = slideshow_ns.class_("PauseAction", automation.Action)
ResumeAction = slideshow_ns.class_("ResumeAction", automation.Action)
RefreshQueueAction = slideshow_ns.class_("RefreshQueueAction", automation.Action)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SlideshowComponent),
    cv.Required(CONF_BACKEND_URL): cv.url,
    cv.Required(CONF_DEVICE_ID): cv.string,
    cv.Optional(CONF_ADVANCE_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_QUEUE_REFRESH_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_AUTO_ADVANCE, default=True): cv.boolean,
    cv.Required(CONF_IMAGE_SLOTS): cv.ensure_list(cv.use_id(online_image.OnlineImage)),
    cv.Optional(CONF_ON_ADVANCE): automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(OnAdvanceTrigger),
    }),
    cv.Optional(CONF_ON_IMAGE_READY): automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(OnImageReadyTrigger),
    }),
    cv.Optional(CONF_ON_QUEUE_UPDATED): automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(OnQueueUpdatedTrigger),
    }),
    cv.Optional(CONF_ON_ERROR): automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(OnErrorTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configuration
    cg.add(var.set_backend_url(config[CONF_BACKEND_URL]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_advance_interval(config[CONF_ADVANCE_INTERVAL]))
    cg.add(var.set_queue_refresh_interval(config[CONF_QUEUE_REFRESH_INTERVAL]))
    cg.add(var.set_auto_advance(config[CONF_AUTO_ADVANCE]))

    # Get http_request component (required dependency)
    http = await cg.get_variable(http_request.CONF_HTTP_REQUEST_ID)
    cg.add(var.set_http_request(http))

    # Add image slots
    for slot_id in config[CONF_IMAGE_SLOTS]:
        slot = await cg.get_variable(slot_id)
        cg.add(var.add_image_slot(slot))

    # Setup triggers
    for conf in config.get(CONF_ON_ADVANCE, []):
        trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.size_t, "x")], conf)

    for conf in config.get(CONF_ON_IMAGE_READY, []):
        trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.size_t, "index"), (cg.bool_, "cached")], conf
        )

    for conf in config.get(CONF_ON_QUEUE_UPDATED, []):
        trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.size_t, "x")], conf)

    for conf in config.get(CONF_ON_ERROR, []):
        trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)


# Actions
@automation.register_action(
    "slideshow.advance",
    AdvanceAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
    }),
)
async def slideshow_advance_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.previous",
    PreviousAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
    }),
)
async def slideshow_previous_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.pause",
    PauseAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
    }),
)
async def slideshow_pause_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.resume",
    ResumeAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
    }),
)
async def slideshow_resume_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.refresh_queue",
    RefreshQueueAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
    }),
)
async def slideshow_refresh_queue_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)