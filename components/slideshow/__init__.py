"""ESPHome Slideshow Component"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import online_image, image
from esphome.const import (
    CONF_ID,
)

# Check if local_image is available in the environment
try:
    from esphome.components import local_image # pyright: ignore[reportAttributeAccessIssue]
    HAS_LOCAL_IMAGE = True
except ImportError:
    HAS_LOCAL_IMAGE = False

DEPENDENCIES = ["online_image"]
AUTO_LOAD = ["online_image"]

CONF_SOURCE = "source"
CONF_ADVANCE_INTERVAL = "advance_interval"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_IMAGE_SLOTS = "image_slots"
CONF_IMAGE_SLOT_COUNT = "image_slot_count"
CONF_ON_ADVANCE = "on_advance"
CONF_ON_IMAGE_READY = "on_image_ready"
CONF_ON_QUEUE_UPDATED = "on_queue_updated"
CONF_ON_ERROR = "on_error"
CONF_ON_REFRESH = "on_refresh"

slideshow_ns = cg.esphome_ns.namespace("slideshow")
SlideshowComponent = slideshow_ns.class_("SlideshowComponent", cg.Component)

# Triggers
OnAdvanceTrigger = slideshow_ns.class_("OnAdvanceTrigger", automation.Trigger.template(cg.size_t))
OnImageReadyTrigger = slideshow_ns.class_("OnImageReadyTrigger", automation.Trigger.template(cg.size_t, cg.bool_))
OnQueueUpdatedTrigger = slideshow_ns.class_("OnQueueUpdatedTrigger", automation.Trigger.template(cg.size_t))
OnErrorTrigger = slideshow_ns.class_("OnErrorTrigger", automation.Trigger.template(cg.std_string))
OnRefreshTrigger = slideshow_ns.class_("OnRefreshTrigger", automation.Trigger.template(cg.size_t))

# Actions
AdvanceAction = slideshow_ns.class_("AdvanceAction", automation.Action)
PreviousAction = slideshow_ns.class_("PreviousAction", automation.Action)
PauseAction = slideshow_ns.class_("PauseAction", automation.Action)
ResumeAction = slideshow_ns.class_("ResumeAction", automation.Action)
RefreshAction = slideshow_ns.class_("RefreshAction", automation.Action)
EnqueueAction = slideshow_ns.class_("EnqueueAction", automation.Action)

SuspendAction = slideshow_ns.class_("SuspendAction", automation.Action)
UnsuspendAction = slideshow_ns.class_("UnsuspendAction", automation.Action)

def validate_image_slot(value):
    """Validate that the slot is a supported image type."""
    # Accept online_image, image, or local_image types
    if HAS_LOCAL_IMAGE:
        return cv.Any(
            cv.use_id(online_image.OnlineImage),
            cv.use_id(image.Image),
            cv.use_id(local_image.LocalImage), # pyright: ignore[reportPossiblyUnboundVariable]
        )(value)
    else:
        return cv.Any(
            cv.use_id(online_image.OnlineImage),
            cv.use_id(image.Image),
        )(value)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SlideshowComponent),

    cv.Optional(CONF_ADVANCE_INTERVAL): cv.positive_time_period_minutes,
    cv.Optional(CONF_REFRESH_INTERVAL): cv.positive_time_period_minutes,

    cv.Required(CONF_IMAGE_SLOTS): cv.ensure_list(validate_image_slot),
    cv.Required(CONF_IMAGE_SLOT_COUNT): cv.positive_int,

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
    cv.Optional(CONF_ON_REFRESH): automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(OnRefreshTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configuration with defaults
    cg.add(var.set_advance_interval(config.get(CONF_ADVANCE_INTERVAL, 5)))
    cg.add(var.set_refresh_interval(config.get(CONF_REFRESH_INTERVAL, 25)))
    cg.add(var.set_slot_count(config[CONF_IMAGE_SLOT_COUNT]))

    # Add image slots - the overloaded add_image_slot method handles type detection
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

    for conf in config.get(CONF_ON_REFRESH, []):
        trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.size_t, "current_size")], conf)


# Actions
@automation.register_action(
    "slideshow.advance",
    AdvanceAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_advance_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.previous",
    PreviousAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_previous_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.pause",
    PauseAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_pause_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.resume",
    ResumeAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_resume_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action(
    "slideshow.refresh",
    RefreshAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_refresh_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "slideshow.enqueue",
    EnqueueAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(SlideshowComponent),
        cv.Required("items"): cv.returning_lambda, # Accepts a lambda returning vector<string>
    }),
)
async def slideshow_enqueue_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    # Process the lambda to get the vector
    template_ = await cg.process_lambda(
        config["items"], args, return_type=cg.std_vector.template(cg.std_string)
    )
    cg.add(var.set_items(template_)) # pyright: ignore[reportArgumentType]
    return var

@automation.register_action(
    "slideshow.suspend",
    SuspendAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_suspend_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action(
    "slideshow.unsuspend",
    UnsuspendAction,
    automation.maybe_simple_id({
        cv.Required(CONF_ID): cv.use_id(SlideshowComponent),
    }), # pyright: ignore[reportArgumentType]
)
async def slideshow_unsuspend_to_code(config, action_id, template_arg, _args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)