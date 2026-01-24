# ESPHome Slideshow Component

A smart slideshow component for ESPHome that orchestrates multiple `online_image` instances to create a smooth, memory-efficient image slideshow with backend queue integration.

## Features

- 🖼️ **Dynamic Queue**: Fetches slideshow queue from your backend API
- 🔄 **Smart Caching**: Maintains a sliding window (prev/current/next) in memory
- 📥 **Automatic Prefetching**: Downloads next image before it's needed
- 💾 **Memory Efficient**: Only keeps 3 images in PSRAM at once
- ⏮️ **Bidirectional**: Support for both advance and previous
- ⏸️ **Pause/Resume**: Pause auto-advance at any time
- 🔁 **Auto-refresh**: Periodically updates queue from backend
- 🎯 **HTTP Caching**: Leverages ETag/Last-Modified headers
- 🎨 **LVGL Ready**: Direct integration with LVGL displays

## Directory Structure

```
components/slideshow/
├── __init__.py          # Python component definition
├── slideshow.h          # C++ header
├── slideshow.cpp        # C++ implementation
└── README.md           # This file
```

## Installation

### Option 1: Local Component

1. Create `components/slideshow/` in your ESPHome config directory
2. Copy all files from this implementation
3. Reference in your YAML:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [slideshow]
```

### Option 2: Git Repository

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/yourusername/esphome-slideshow
    components: [slideshow]
```

## Backend API Requirements

Your backend must implement these endpoints:

### GET `/api/devices/{device_id}/slideshow`

Returns the queue of image IDs:

```json
{
  "queue": [
    "abc123",
    "def456",
    "ghi789"
  ]
}
```

### GET `/api/devices/{device_id}/images/{image_id}`

Returns the processed JPEG image for this device.

**Important**: This URL is constructed automatically as:
```
{backend_url}/images/{device_id}/{image_id}
```

**Headers**: Should support HTTP caching headers:
- `ETag`: Unique identifier for this version
- `Last-Modified`: When the image was last updated
- `Cache-Control`: How long to cache

## Configuration

### Minimal Configuration

```yaml
http_request:
  timeout: 10s

image:
  - file: "loading.png"
    id: loading_placeholder
    type: RGB565

online_image:
  - url: ""
    id: slide_slot_0
    type: RGB565
    resize: 600x1024
    format: JPEG
    placeholder: loading_placeholder
    on_download_finished:
      lambda: 'id(my_slideshow).on_image_ready(0, x);'
    on_error:
      lambda: 'id(my_slideshow).on_image_error(0);'
  
  # Repeat for slide_slot_1 and slide_slot_2

slideshow:
  id: my_slideshow
  backend_url: "https://your-backend.com/api/devices/living-room"
  device_id: "living-room"
  advance_interval: 10s
  image_slots:
    - slide_slot_0
    - slide_slot_1
    - slide_slot_2
```

### Full Configuration

```yaml
slideshow:
  id: my_slideshow
  
  # Required
  backend_url: "https://your-backend.com/api/devices/living-room"
  device_id: "living-room"
  image_slots:
    - slide_slot_0
    - slide_slot_1
    - slide_slot_2
  
  # Optional
  advance_interval: 10s          # Time between auto-advance
  queue_refresh_interval: 60s    # How often to refresh queue
  auto_advance: true             # Enable auto-advance
  
  # Callbacks
  on_advance:
    - logger.log: "Advanced to next image"
  
  on_image_ready:
    - lambda: |-
        ESP_LOGI("app", "Image %d ready (cached: %s)", 
                 index, cached ? "yes" : "no");
  
  on_queue_updated:
    - lambda: 'ESP_LOGI("app", "Queue: %d images", x);'
  
  on_error:
    - lambda: 'ESP_LOGE("app", "Error: %s", x.c_str());'
```

## Actions

### `slideshow.advance`

Move to the next image in the queue.

```yaml
button:
  - platform: ...
    on_press:
      - slideshow.advance: my_slideshow
```

### `slideshow.previous`

Move to the previous image in the queue.

```yaml
button:
  - platform: ...
    on_press:
      - slideshow.previous: my_slideshow
```

### `slideshow.pause`

Pause auto-advance.

```yaml
button:
  - platform: ...
    on_press:
      - slideshow.pause: my_slideshow
```

### `slideshow.resume`

Resume auto-advance.

```yaml
button:
  - platform: ...
    on_press:
      - slideshow.resume: my_slideshow
```

### `slideshow.refresh_queue`

Manually refresh the queue from the backend.

```yaml
button:
  - platform: ...
    on_press:
      - slideshow.refresh_queue: my_slideshow
```

## Lambda API

Access slideshow state in lambdas:

```cpp
// Get current image for display
auto *img = id(my_slideshow).get_current_image();
if (img && img->get_width() > 0) {
  it.image(0, 0, img);
}

// Get current index
size_t index = id(my_slideshow).current_index();

// Get queue size
size_t size = id(my_slideshow).queue_size();

// Check if paused
bool paused = id(my_slideshow).is_paused();
```

## Display Integration

### Standard ESPHome Display

```yaml
display:
  - platform: st7789v
    lambda: |-
      auto *current = id(my_slideshow).get_current_image();
      if (current && current->get_width() > 0) {
        it.image(0, 0, current);
      } else {
        // Show loading placeholder
        it.image(0, 0, id(loading_placeholder));
      }
```

### LVGL Display

```yaml
lvgl:
  pages:
    - id: slideshow_page
      widgets:
        - img:
            id: slideshow_img
            align: CENTER

# Update LVGL image source
interval:
  - interval: 100ms
    then:
      - lambda: |-
          auto *current = id(my_slideshow).get_current_image();
          if (current && current->get_width() > 0) {
            lv_img_set_src(id(slideshow_img), current->get_lv_img_dsc());
          }
```

## Memory Management

The component implements a **sliding window cache**:

1. Always keeps **current** image loaded
2. Prefetches **next** image (for smooth advance)
3. Keeps **previous** image (for smooth back navigation)
4. Releases images outside this 3-image window

**Memory usage** (with 600×1024 RGB565 images):
- 3 images × ~1.2MB = **~3.6MB PSRAM**
- Download buffer: **~64KB**
- Metadata: **<1KB**

**Total: ~3.7MB** (fits comfortably in ESP32-S3 with 8MB PSRAM)

## How It Works

### Startup Sequence

1. Component calls backend to fetch queue
2. Parses JSON response into image IDs
3. Loads images into slots:
   - Slot 0: Queue index 0 (current)
   - Slot 1: Queue index 1 (next)
   - Slot 2: (empty initially)

### Advance Sequence

1. User presses button or auto-advance timer fires
2. `current_index` increments (0 → 1)
3. Component evaluates desired window: [0, 1, 2]
4. Slot assignments shift:
   - Keep slot 0 (now previous)
   - Keep slot 1 (now current)
   - Load queue index 2 into slot 2 (now next)

### Memory Release

When advancing from index 1 → 2:
- Desired window: [1, 2, 3]
- Queue index 0 is no longer needed
- Component calls `online_image.release()` on its slot
- Memory is freed for future use

## Troubleshooting

### Images not downloading

**Check**:
1. Is `http_request` component configured?
2. Are backend URLs correct?
3. Check logs for HTTP errors

**Debug**:
```yaml
logger:
  level: DEBUG
  logs:
    slideshow: DEBUG
    online_image: DEBUG
    http_request: DEBUG
```

### Out of memory crashes

**Solutions**:
1. Ensure ESP32-S3 with PSRAM (2MB minimum, 8MB recommended)
2. Reduce image resolution in backend processing
3. Use JPEG format (10x smaller than raw RGB)
4. Reduce number of slots (2 minimum for prev/current)

### Queue not refreshing

**Check**:
1. Is `queue_refresh_interval` set?
2. Is WiFi connected?
3. Backend endpoint returning valid JSON?

**Manual refresh**:
```yaml
button:
  on_press:
    - slideshow.refresh_queue: my_slideshow
```

### Images showing incorrectly

**Common issues**:
1. **Wrong image type**: Match `type` in `online_image` to backend format (RGB565 recommended)
2. **Wrong resize**: Ensure `resize` matches your display dimensions
3. **Wrong format**: Ensure `format` matches backend (JPEG recommended)

## Performance Tips

1. **Backend optimization**:
   - Serve images at exact device resolution
   - Use JPEG with 80-85% quality
   - Enable HTTP caching headers
   - Use CDN for image delivery

2. **ESPHome optimization**:
   - Set `buffer_size: 32768` for slow connections
   - Increase `queue_refresh_interval` to reduce API calls
   - Use `placeholder` images for better UX during download

3. **Memory optimization**:
   - Use RGB565 instead of RGB (2 bytes vs 3 bytes per pixel)
   - Keep `image_slots` at 3 (prev/current/next)
   - Enable JPEG format (requires less download buffer)

## Advanced Usage

### Conditional Queue Refresh

```yaml
# Only refresh queue when specific condition met
binary_sensor:
  - platform: ...
    id: motion_sensor
    on_press:
      - if:
          condition:
            # Only if queue hasn't been refreshed in last hour
            lambda: 'return millis() - id(my_slideshow).last_queue_refresh() > 3600000;'
          then:
            - slideshow.refresh_queue: my_slideshow
```

### Dynamic Advance Interval

```yaml
# Slow down at night
time:
  - platform: homeassistant
    on_time:
      - hours: 22
        then:
          - lambda: 'id(my_slideshow).set_advance_interval(30000);'  # 30s
      - hours: 7
        then:
          - lambda: 'id(my_slideshow).set_advance_interval(10000);'  # 10s
```

### Queue Size Display

```yaml
# Show queue size on screen
text_sensor:
  - platform: template
    name: "Slideshow Queue Size"
    lambda: |-
      return to_string(id(my_slideshow).queue_size());
    update_interval: 10s
```

## Contributing

Contributions welcome! Please:
1. Follow ESPHome coding style
2. Add tests for new features
3. Update documentation

## License

MIT License - see LICENSE file

## Credits

Built on top of ESPHome's excellent `online_image` component, which handles all the heavy lifting of image downloading and decoding.