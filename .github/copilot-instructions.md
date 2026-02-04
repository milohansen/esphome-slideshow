# ESPHome Slideshow Component - AI Coding Instructions

## Project Overview
This is a custom ESPHome component written in C++ with Python configuration that creates memory-efficient image slideshows on ESP32 devices with displays (LVGL, e-ink). It uses a **Controller-Slot architecture** to decouple image sources (URLs, files, embedded) from display logic.

## Architecture Pattern: Queue Provider + Sliding Window Cache

### Core Concepts
1. **Queue**: Vector of image sources (URLs or file paths) - can grow to hundreds of items
2. **Slots**: Limited pool (typically 3) of actual image buffers in PSRAM - the physical memory constraints
3. **Controller**: [SlideshowComponent](components/slideshow/slideshow.h) maps queue indices to slot indices using a sliding window cache

**Critical invariant**: `loaded_images_` (queue→slot mapping) and `loading_slots_` are disjoint sets to prevent slot conflicts.

### Slot Management Strategy
The `ensure_slots_loaded_()` method maintains prev/current/next images in memory:
- On advance: releases old "prev" slot, loads new "next" image to freed slot
- Uses modulo arithmetic for circular queue navigation (`advance_index()`, `retreat_index()`)
- **Dirty flag optimization**: `slots_dirty_` prevents unnecessary reloading in `loop()`

## Component Interfaces

### Abstract Slot Interface: `SlideshowSlot`
Adapters wrap different ESPHome image types. All implementations in [components/slideshow/](components/slideshow/):
- `OnlineImageSlot` - downloads from HTTP/HTTPS URLs via `online_image`
- `LocalImageSlot` - loads from filesystem (SD/Flash) via `local_image` (guarded by `#ifdef USE_LOCAL_IMAGE`)
- `EmbeddedImageSlot` - static compiled-in images via `image`

**Pattern**: Slots use non-owning observer pointers (`OnlineImage*`) and register callbacks during construction to notify controller when ready/failed.

## Configuration & Python Codegen

### Python Layer: [__init__.py](components/slideshow/__init__.py)
- Defines ESPHome schema with custom validators (`validate_image_slot`)
- Auto-detects slot types and generates appropriate C++ `add_image_slot()` calls
- Registers triggers (OnAdvance, OnImageReady, etc.) and actions (advance, pause, enqueue)
- **Conditional import pattern**: Checks `HAS_LOCAL_IMAGE` to support optional `local_image` component

### YAML Configuration Pattern
Users define slots then reference them:
```yaml
online_image:
  - url: ""
    id: slot0
    on_download_finished:
      - lambda: "id(my_slideshow).on_image_ready(0);"

slideshow:
  id: my_slideshow
  image_slots: [slot0, slot1, slot2]
  image_slot_count: 3  # Must match number of slots
```

**Why `on_download_finished` callbacks**: ESPHome's event system requires YAML-level wiring since C++ can't auto-discover component relationships.

## Developer Workflows

### Building & Testing
This is a **component library**, not a standalone application. Test via example YAML:
```bash
# Validate configuration
esphome config example.yaml

# Build for specific board
esphome compile example.yaml

# Build and upload (requires connected device)
esphome run example.yaml
```

### Adding New Slot Type
1. Create adapter header in [components/slideshow/](components/slideshow/) following `slideshow_online_image.h` pattern
2. Implement `SlideshowSlot` interface with proper lifecycle callbacks
3. Add guarded `#ifdef USE_<COMPONENT>` to [slideshow.cpp](components/slideshow/slideshow.cpp)
4. Update [__init__.py](components/slideshow/__init__.py) validator and `to_code()` function

## Critical Implementation Details

### Callback Managers
- `OnceCallbackManager`: Single-fire callbacks for async operations (image load complete)
- `CallbackManager<T>`: Multi-subscriber callbacks for events (on_advance, on_error)
- **Usage pattern**: Slots call `callbacks_.call(bool)` on completion, controller processes result

### Memory Management
- Slots are `std::unique_ptr<SlideshowSlot>` owned by component
- `release()` method frees image buffer in PSRAM without destroying slot object
- Critical for ESP32 PSRAM constraints (typically 4-8MB)

### ESPHome Lifecycle Integration
- `setup()`: Registers scheduled intervals for auto-advance and refresh triggers
- `loop()`: Only processes when `!suspended_` and slots are dirty
- `get_setup_priority()`: Returns `LATE` to ensure image components initialize first

## Common Patterns

### Provider Pattern (Queue Population)
The `on_refresh` trigger fires when more images needed:
```yaml
on_refresh:
  then:
    - http_request.get:
        url: "https://api.example.com/images"
        on_response:
          - lambda: |-
              std::vector<std::string> urls = parse_json(body);
              id(my_slideshow).enqueue(urls);
```

### LVGL Integration
Pause rendering during image transitions to avoid artifacts:
```yaml
on_advance:
  - lvgl.pause:
      show_snow: false
on_image_ready:
  - lvgl.resume:
```

### Accessing Current Image
```cpp
auto *slot = id(my_slideshow).get_current_image();
if (slot && slot->is_ready()) {
  auto *img = slot->get_image();  // Returns esphome::image::Image*
  it.image(0, 0, img);
}
```

## File Organization
- [slideshow.h](components/slideshow/slideshow.h) - Component class, triggers, actions
- [slideshow.cpp](components/slideshow/slideshow.cpp) - Core logic, slot management
- [slideshow_*_image.h](components/slideshow/) - Slot adapters (header-only)
- [__init__.py](components/slideshow/__init__.py) - ESPHome integration layer

## Testing & Debugging
- Set `logger.level: DEBUG` and `logs.slideshow: DEBUG` in YAML
- Watch for "Advanced to index", "Image ready in slot", "Enqueuing" log messages
- Use `slideshow.refresh_queue` action to manually trigger provider pattern
- Examples in [examples/](examples/) demonstrate e-ink and LCD configurations

## Dependencies
- **Required**: `online_image`, `http_request` (for URL-based images)
- **Optional**: `local_image` (filesystem), `lvgl` (display integration)
- **Framework**: ESP-IDF with PSRAM enabled (`CONFIG_SPIRAM_XIP_FROM_PSRAM`)
