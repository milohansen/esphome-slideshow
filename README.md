# ESPHome Slideshow Component

A smart slideshow component for ESPHome that orchestrates multiple image slots (like `online_image` or `local_image`) to create a smooth, memory-efficient image slideshow. It decouples the image _source_ logic from the image _display_ logic.

## Features

- 🧩 **Source Agnostic**: Works with URLs (`online_image`) or Files (`local_image`).
- 🧠 **Queue Provider Pattern**: You define _how_ to get images (API, JSON, hardcoded list) via YAML scripts or lambdas.
- 🔄 **Smart Caching**: Maintains a sliding window (prev/current/next) in memory.
- 📥 **Automatic Prefetching**: Downloads/Loads the next image before it's needed.
- 💾 **Memory Efficient**: Only keeps a configurable number of images (slots) loaded in PSRAM.
- ⏮️ **Bidirectional**: Support for both advance and previous.
- 🎯 **Generic Slots**: Abstract interface allowing mix-and-match of image components.
- 🎨 **LVGL Ready**: Easy integration with LVGL displays.
- 🖼️ **Paired Images**: Display side-by-side image pairs with mixed single and paired content support.

## Directory Structure

```

components/slideshow/
├── __init__.py                # Python component definition
├── slideshow.h                # C++ header
├── slideshow.cpp              # C++ implementation
├── slideshow_online_image.h   # Adapter for OnlineImage
├── slideshow_local_image.h    # Adapter for LocalImage
├── slideshow_embedded_image.h # Adapter for raw Image
└── README.md                  # This file

```

## Installation

### Option 1: Local Component

1. Create `components/slideshow/` in your ESPHome config directory.
2. Copy all files from this implementation.
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
      url: [https://github.com/yourusername/esphome-slideshow](https://github.com/yourusername/esphome-slideshow)
    components: [slideshow]

```

## Configuration

### Basic Configuration

You must define the `image_slots` (the physical resources) and the `slideshow` controller.

```yaml
# 1. Define your slots (e.g., OnlineImage for web URLs)
online_image:
  - url: ""
    id: slot0
    type: RGB565
    resize: 800x480
    on_download_finished:
      # Notify slideshow when done
      - lambda: "id(my_slideshow).on_image_ready(0);"
    on_error:
      - lambda: "id(my_slideshow).on_image_error(0);"

  - url: ""
    id: slot1
    type: RGB565
    resize: 800x480
    on_download_finished:
      - lambda: "id(my_slideshow).on_image_ready(1);"
    on_error:
      - lambda: "id(my_slideshow).on_image_error(1);"

# 2. Define the Slideshow Controller
slideshow:
  id: my_slideshow
  advance_interval: 10s # Auto-advance time
  refresh_interval: 60s # How often to trigger 'on_refresh'

  # Link the slots
  image_slots:
    - slot0
    - slot1

  # 3. Define how to get images (The "Provider" Pattern)
  on_refresh:
    then:
      - logger.log: "Slideshow needs more images!"
      # Call a script to fetch data, or return a static list
      - slideshow.enqueue:
          items: !lambda |-
            return {
              "[https://picsum.photos/id/1/800/480](https://picsum.photos/id/1/800/480)",
              "[https://picsum.photos/id/2/800/480](https://picsum.photos/id/2/800/480)",
              "[https://picsum.photos/id/3/800/480](https://picsum.photos/id/3/800/480)"
            };
```

### Advanced: Dynamic API Fetching

Instead of a hardcoded list, use `http_request` to fetch JSON from an API, parse it, and push it to the slideshow.

```yaml
slideshow:
  id: my_slideshow
  refresh_interval: 5min

  # When the component needs data, it fires this trigger
  on_refresh:
    then:
      - script.execute: fetch_api_images

script:
  - id: fetch_api_images
    then:
      - http_request.get:
          url: "[https://my-api.com/gallery.json](https://my-api.com/gallery.json)"
          on_response:
            then:
              - lambda: |-
                  // Parse your custom JSON format
                  // Assuming JSON: ["url1", "url2", ...]
                  // Use ArduinoJson to parse 'body'

                  std::vector<std::string> new_items;
                  // ... populating vector logic ...
                  new_items.push_back("[https://site.com/img1.jpg](https://site.com/img1.jpg)");

                  // Push data to slideshow
                  id(my_slideshow).enqueue(new_items);
```

### Paired Mode: Side-by-Side Images

Enable `pair_layout: true` to support displaying two images side-by-side. In this mode, the queue can contain a mix of single images and paired images (separated by `|`).

```yaml
slideshow:
  id: my_slideshow
  pair_layout: true  # Enable paired image support
  
  # Note: Need more slots for paired mode (recommend 6 for smooth transitions)
  image_slots:
    - slot0
    - slot1
    - slot2
    - slot3
    - slot4
    - slot5
  image_slot_count: 6
  
  on_refresh:
    then:
      - slideshow.enqueue:
          items: !lambda |-
            return {
              "https://example.com/single1.jpg",                      // Single image
              "https://example.com/left.jpg|https://example.com/right.jpg",  // Paired images
              "https://example.com/single2.jpg",                      // Single image
              "https://example.com/left2.jpg|https://example.com/right2.jpg" // Paired images
            };
  
  on_image_ready:
    - lambda: |-
        size_t idx = index;
        bool is_left_side = is_left;
        bool is_paired_item = is_paired;
        
        if (is_paired_item) {
          // Handle paired image loading
          if (!is_left_side) {  // Both images should be ready when right finishes
            auto* left = id(my_slideshow).get_current_image();
            auto* right = id(my_slideshow).get_current_right_image();
            
            if (left && right) {
              // Render side-by-side
              it.image(0, 0, left->get_image());     // Left half
              it.image(512, 0, right->get_image());  // Right half
            }
          }
        } else {
          // Handle single image
          auto* img = id(my_slideshow).get_current_image();
          if (img) {
            it.image(0, 0, img->get_image());  // Full width
          }
        }
```

**Key points:**
- Use `|` to separate left and right image URLs in the queue
- `on_image_ready` receives 3 parameters: `(index, is_left, is_paired)`
- Check `is_current_paired()` to determine if current item is a pair
- Use `get_current_image()` for left/single image, `get_current_right_image()` for right image
- Missing right image (partial pair) is supported - `get_current_right_image()` returns `nullptr`

See [examples/paired_example.yaml](examples/paired_example.yaml) for a complete configuration.

## Actions

### `slideshow.enqueue`

Adds new items to the end of the playlist. The items are strings (URLs or file paths).

```yaml
- slideshow.enqueue:
    id: my_slideshow
    items: !lambda |-
      return {"image1.jpg", "image2.jpg"};
```

### `slideshow.advance` / `slideshow.previous`

Manually navigate the queue.

```yaml
on_button_press:
  - slideshow.advance: my_slideshow
```

### `slideshow.pause` / `slideshow.resume`

Control the auto-advance timer.

```yaml
on_button_press:
  - slideshow.pause: my_slideshow
```

### `slideshow.refresh`

Manually trigger the `on_refresh` trigger defined in your configuration. This is useful for forcing an update (e.g., pulling a new playlist).

```yaml
on_button_press:
  - slideshow.refresh: my_slideshow
```

## Architecture

This component uses a **Controller-Slot** architecture:

1. **The Queue**: A list of strings (URLs or file paths).
2. **The Slots**: Physical buffers (e.g., `online_image` components).
3. **The Controller**: Maps Queue Index → Slot Index.

**Example State:**

- Queue Size: 100 images
- Slots: 3 (Index 0, 1, 2)
- Current Image: Queue Index 50

**Mapping:**

- Queue Index 49 (Prev) → Slot 2
- Queue Index 50 (Curr) → Slot 0
- Queue Index 51 (Next) → Slot 1

As you advance, the controller rotates the slots, releasing the old "Previous" image and loading the new "Next" image.

## Supported Slot Types

The component automatically detects the type of component passed to `image_slots`:

| Component          | Behavior                               | Source String Format        |
| ------------------ | -------------------------------------- | --------------------------- |
| **`online_image`** | Downloads HTTP/HTTPS images            | `http://...`, `https://...` |
| **`local_image`**  | Loads files from filesystem (SD/Flash) | `/images/photo.jpg`         |
| **`image`**        | Static raw images (embedded)           | _N/A (Static)_              |

## Lambda API

Access the slideshow state in C++ lambdas:

```cpp
// Get current displayable image (generic interface)
auto *slot = id(my_slideshow).get_current_image();
if (slot && slot->is_ready()) {
  // Get underlying esphome::image::Image*
  auto *img = slot->get_image();
  it.image(0, 0, img);
}

// Get basic state
size_t idx = id(my_slideshow).current_index();
size_t total = id(my_slideshow).queue_size();
bool paused = id(my_slideshow).is_paused();

// Trigger an enqueue manually from C++
std::vector<std::string> items = {"url1", "url2"};
id(my_slideshow).enqueue(items);

// Paired mode specific APIs
if (id(my_slideshow).is_pair_layout()) {
  // Check if current item is a pair
  if (id(my_slideshow).is_current_paired()) {
    auto *left = id(my_slideshow).get_current_image();
    auto *right = id(my_slideshow).get_current_right_image();
    
    if (left && right) {
      // Render side-by-side
      it.image(0, 0, left->get_image());
      it.image(512, 0, right->get_image());
    }
  } else {
    // Current item is single, even in paired mode
    auto *img = id(my_slideshow).get_current_image();
    it.image(0, 0, img->get_image());
  }
}
```
