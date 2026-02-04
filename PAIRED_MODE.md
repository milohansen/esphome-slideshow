# Paired Mode Implementation Summary

## Overview

This implementation adds support for side-by-side image pairs to the ESPHome slideshow component while maintaining full backward compatibility with single-image mode.

## Key Features

- **Mixed Content Support**: Queue can contain both single images and paired images
- **Explicit Pairing**: Pairs are defined using `|` delimiter (e.g., `"left.jpg|right.jpg"`)
- **Backward Compatible**: Existing single-image configurations work unchanged
- **Flexible Memory**: Slot usage adapts to content (singles use 1 slot, pairs use 2 slots)
- **Partial Pair Support**: Missing right image is handled gracefully

## Architecture Changes

### Data Structures

**QueueItem** (slideshow.h):
```cpp
struct QueueItem {
  std::string source_left;   // Single image URL or left side URL
  std::string source_right;  // Empty for singles, populated for pairs
  
  bool is_paired() const { return !source_right.empty(); }
  size_t slot_count() const { return is_paired() ? 2 : 1; }
};
```

**State Variables**:
- `bool pair_layout_` - Configuration flag to enable paired mode
- `std::map<size_t, std::pair<size_t, size_t>> loaded_image_pairs_` - Maps queue index to [left_slot, right_slot]
- Existing `loaded_images_` map continues to handle single images

### Slot Management

The component now supports two loading strategies:

1. **Single Mode** (`pair_layout_=false`): Original behavior, uses `ensure_single_slots_loaded_()`
2. **Paired Mode** (`pair_layout_=true`): New behavior, uses `ensure_paired_slots_loaded_()`

**Paired mode logic**:
- Iterates through desired queue indices (prev/current/next)
- For each item, checks if it's paired via `is_paired()`
- Loads 1 slot for singles, 2 slots for pairs
- Releases items outside the window

### Callback Enhancements

**on_image_ready** now passes 4 parameters:
1. `size_t index` - Queue index
2. `bool success` - Whether load succeeded
3. `bool is_left` - Which side loaded (true=left/single, false=right)
4. `bool is_paired` - Whether the queue item is a pair

This allows YAML lambdas to handle singles and pairs differently.

## Configuration

### Enable Paired Mode

```yaml
slideshow:
  id: my_slideshow
  pair_layout: true  # Enable paired image support
  image_slot_count: 6  # Recommend 6 slots for smooth transitions
  image_slots: [slot0, slot1, slot2, slot3, slot4, slot5]
```

### Enqueue Mixed Content

```yaml
on_refresh:
  then:
    - slideshow.enqueue:
        items: !lambda |-
          return {
            "https://example.com/single1.jpg",
            "https://example.com/left2.jpg|https://example.com/right2.jpg",
            "https://example.com/single3.jpg"
          };
```

### Handle Image Ready Events

```yaml
on_image_ready:
  - lambda: |-
      if (is_paired) {
        if (!is_left) {  // Right side just loaded
          auto* left = id(my_slideshow).get_current_image();
          auto* right = id(my_slideshow).get_current_right_image();
          // Render side-by-side
        }
      } else {
        auto* img = id(my_slideshow).get_current_image();
        // Render full-width
      }
```

## Public API

### New Methods

```cpp
// Check if paired mode is enabled
bool is_pair_layout() const;

// Check if current queue item is a pair
bool is_current_paired() const;

// Get right side of pair (returns nullptr for singles)
SlideshowSlot* get_current_right_image();

// Existing method now works for both modes
SlideshowSlot* get_current_image();  // Returns left for pairs, image for singles
```

## Implementation Files Changed

### C++ Files
- **slideshow.h**: Added QueueItem helpers, state variables, new methods, updated callback signatures
- **slideshow.cpp**: 
  - Updated enqueue() to parse `|` delimiter
  - Added ensure_paired_slots_loaded_() and helper methods
  - Updated on_image_ready/error to handle both maps
  - Added load_single_to_slot_() and load_pair_to_slots_()

### Python Files
- **__init__.py**: Added CONF_PAIR_LAYOUT, updated schema, updated trigger parameters

### Examples
- **examples/paired_example.yaml**: Complete paired mode configuration example

### Documentation
- **README.md**: Added paired mode section with usage examples

## Memory Considerations

**Single Mode** (3 slots):
- 3 images × ~600KB = ~1.8MB PSRAM

**Paired Mode** (6 slots):
- Mixed: 3 singles (3 slots) or 3 pairs (6 slots) or mixture
- Worst case: 6 images × ~600KB = ~3.6MB PSRAM

**Recommendation**: Test on actual hardware to ensure PSRAM capacity is sufficient.

## Backward Compatibility

All existing single-image configurations work without modification:
- `pair_layout` defaults to `false`
- When disabled, all code paths behave as before
- Existing trigger signatures extended with new parameters (old lambdas still work)

## Testing Checklist

- [ ] Single mode functionality unchanged
- [ ] Paired mode with all pairs
- [ ] Paired mode with all singles
- [ ] Paired mode with mixed content
- [ ] Partial pairs (empty right URL)
- [ ] Memory usage profiling with 6 slots
- [ ] Advance through mixed queue
- [ ] Previous through mixed queue
- [ ] Error handling for failed left/right images

## Known Limitations

1. **Queue builder** doesn't support pairs (only enqueue with `|` delimiter)
2. **Error handling** for partial pairs leaves mapping intact (design choice for flexibility)
3. **Slot count validation** doesn't enforce even numbers in paired mode
4. **No runtime mode switching** - requires reflash to change pair_layout

## Future Enhancements

Potential improvements for future iterations:

1. **Slot count validation**: Add Python validation to require even slot counts in paired mode
2. **Better partial pair handling**: Add `is_complete_pair()` helper method
3. **Dynamic slot reuse**: If left image fails, reuse right slot for optimization
4. **Runtime mode switching**: Allow changing pair_layout without reflash (complex)
5. **Queue builder support**: Extend queue_builder_t to return structured pair data

## Technical Debt

Recommended GitHub Issues to track:

1. **Dynamic slot count validation**: Ensure image_slot_count matches image_slots.size()
2. **Slot reuse optimization**: Reuse slots from failed images
3. **Comprehensive paired mode testing**: Unit tests for slot invariants in paired mode
