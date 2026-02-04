#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "slideshow.h"

#include "slideshow_online_image.h"
#include "slideshow_embedded_image.h"
#ifdef USE_LOCAL_IMAGE
#include "esphome/components/local_image/local_image.h"
#include "slideshow_local_image.h"
#endif

namespace esphome
{
  namespace slideshow
  {

    static const char *const TAG = "slideshow";

    // Helper function to trim whitespace
    static void trim(std::string &s)
    {
      auto is_whitespace = [](unsigned char c)
      { return std::isspace(c); };
      s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c)
                                      { return !is_whitespace(c); }));
      s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c)
                           { return !is_whitespace(c); })
                  .base(),
              s.end());
    }

    void SlideshowComponent::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up slideshow...");

      if (image_slots_.empty())
      {
        ESP_LOGE(TAG, "No image slots configured!");
        mark_failed();
        return;
      }

      if (slot_count_ == 0)
      {
        ESP_LOGE(TAG, "Slot count must be greater than zero!");
        mark_failed();
        return;
      }

      if (advance_interval_ > 0)
      {
        set_interval("advance", advance_interval_ * 60000, [this]()
                     {
          if (!paused_ && !queue_.empty()) {
            advance();
          } });
      }

      if (refresh_interval_ > 0)
      {
        set_interval("refresh", refresh_interval_ * 60000, [this]()
                     {
          ESP_LOGD(TAG, "Triggering refresh...");
          this->on_refresh_callbacks_.call(0); });
      }

      this->on_refresh_callbacks_.call(0);
    }

    void SlideshowComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Slideshow:");
      ESP_LOGCONFIG(TAG, "  Advance interval: %um", advance_interval_);
      ESP_LOGCONFIG(TAG, "  Refresh interval: %um", refresh_interval_);
      ESP_LOGCONFIG(TAG, "  Image slots: %d", image_slots_.size());
    }

    void SlideshowComponent::loop()
    {
      if (suspended_)
        return;

      // Only reload slots when state has changed (dirty flag optimization)
      if (slots_dirty_)
      {
        ensure_slots_loaded_();
        slots_dirty_ = false;
      }

      if (needs_more_photos_)
      {
        refresh();
      }
    }

    void SlideshowComponent::add_image_slot(online_image::OnlineImage *slot)
    {
      this->image_slots_.push_back(std::unique_ptr<SlideshowSlot>(new OnlineImageSlot(slot)));
    }
    void SlideshowComponent::add_image_slot(esphome::image::Image *slot)
    {
      this->image_slots_.push_back(std::unique_ptr<SlideshowSlot>(new EmbeddedImageSlot(slot)));
    }

// Guarded implementation for LocalImage
#ifdef USE_LOCAL_IMAGE
    void SlideshowComponent::add_image_slot(local_image::LocalImage *slot)
    {
      this->image_slots_.push_back(std::unique_ptr<SlideshowSlot>(new LocalImageSlot(slot)));
    }
#endif

    void SlideshowComponent::advance()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot advance: queue is empty");
        return;
      }

      current_index_ = advance_index(current_index_, queue_.size());

      ESP_LOGD(TAG, "Advanced to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_].source_left.c_str());

      // Fire callback
      on_advance_callbacks_.call(current_index_);

      // Check if we're near the end
      if (current_index_ + 2 >= queue_.size())
      {
        needs_more_photos_ = true;
      }

      // Mark slots as needing reload
      slots_dirty_ = true;
    }

    void SlideshowComponent::previous()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot go back: queue is empty");
        return;
      }

      current_index_ = retreat_index(current_index_, queue_.size());

      ESP_LOGD(TAG, "Went back to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_].source_left.c_str());

      on_advance_callbacks_.call(current_index_);

      // Mark slots as needing reload
      slots_dirty_ = true;
    }

    void SlideshowComponent::pause()
    {
      if (!paused_)
      {
        paused_ = true;
        ESP_LOGI(TAG, "Paused at index %d", current_index_);
      }
    }

    void SlideshowComponent::resume()
    {
      if (paused_)
      {
        paused_ = false;
        last_advance_ = millis(); // Reset timer
        ESP_LOGI(TAG, "Resumed from index %d", current_index_);
      }
    }

    void SlideshowComponent::refresh()
    {
      this->on_refresh_callbacks_.call(0);
      needs_more_photos_ = false;
    }

    void SlideshowComponent::jump_to(size_t index)
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot jump: queue is empty");
        return;
      }

      // Validate index is within reasonable bounds
      if (index >= queue_.size())
      {
        ESP_LOGW(TAG, "Cannot jump to index %d: out of bounds (queue size: %d)", index, queue_.size());
        return;
      }

      current_index_ = index;

      ESP_LOGI(TAG, "Jumped to index %d (ID: %s)",
               current_index_, queue_[current_index_].source_left.c_str());

      on_advance_callbacks_.call(current_index_);

      // Mark slots as needing reload
      slots_dirty_ = true;
    }

    void SlideshowComponent::enqueue(const std::vector<std::string> &items)
    {
      if (items.empty())
        return;

      ESP_LOGI(TAG, "Enqueuing %d new items", items.size());

      size_t valid_count = 0;
      for (const auto &str : items)
      {
        // Validate: skip empty strings or strings that are just whitespace
        auto is_whitespace = [](unsigned char c)
        { return std::isspace(c); };
        if (str.empty() || std::all_of(str.begin(), str.end(), is_whitespace))
        {
          ESP_LOGW(TAG, "Skipping empty or whitespace-only item");
          continue;
        }

        QueueItem item;
        size_t delimiter_pos = str.find('|');

        if (pair_layout_ && delimiter_pos != std::string::npos)
        {
          // Paired item: "url1|url2"
          item.source_left = str.substr(0, delimiter_pos);
          item.source_right = str.substr(delimiter_pos + 1);
          trim(item.source_left);
          trim(item.source_right);
        }
        else
        {
          // Single item: "url" (or pair mode disabled, or no delimiter found)
          item.source_left = str;
          item.source_right = ""; // Empty = single
        }

        if (!pair_layout_ && delimiter_pos != std::string::npos)
        {
          ESP_LOGW(TAG, "Found '|' delimiter but pair_layout is disabled, treating as single");
          item.source_right = "";
        }

        queue_.push_back(item);
        valid_count++;
      }

      if (valid_count > 0)
      {
        ESP_LOGI(TAG, "Successfully enqueued %d valid items", valid_count);
        // Notify listeners
        on_queue_updated_callbacks_.call(queue_.size());

        // Mark slots as needing reload
        slots_dirty_ = true;
      }
    }

    void SlideshowComponent::clear_queue()
    {
      ESP_LOGI(TAG, "Clearing queue (had %d items)", queue_.size());

      queue_.clear();
      current_index_ = 0;

      // Release all loaded slots (pairs)
      for (const auto &[queue_idx, slot_pair] : loaded_images_)
      {
        release_slot_(slot_pair); // Left and Right
      }
      loaded_images_.clear();

      loading_slots_.clear();

      needs_more_photos_ = false;

      // Notify listeners
      on_queue_updated_callbacks_.call(0);
    }

    // Not used
    SlideshowSlot *SlideshowComponent::get_current_image()
    {
      if (queue_.empty())
        return nullptr;

      size_t current_index_mod = current_index_ % queue_.size();

      if (!loaded_images_.contains(current_index_mod))
      {
        return nullptr;
      }
      // return nullptr;

      // if (pair_layout_)
      // {
      //   // Check if it's a loaded pair
      //   auto it_pair = loaded_image_pairs_.find(current_index_mod);
      //   if (it_pair != loaded_image_pairs_.end())
      //   {
      //     auto *slot = image_slots_[it_pair->second.first].get();
      //     return (slot && slot->is_ready()) ? slot : nullptr;
      //   }

        
      //   // Check if it's a loaded single
      //   auto it_pair = loaded_images_.find(current_index_mod);
      //   if (it_pair != loaded_images_.end())
      //   {
      //     auto *slot = image_slots_[it_single->second].get();
      //     return (slot && slot->is_ready()) ? slot : nullptr;
      //   }
      // }
      // else
      // {
      //   // Original single mode logic
      //   if (auto it = loaded_images_.find(current_index_mod); it != loaded_images_.end())
      //   {
      //     size_t slot_idx = it->second;
      //     auto *img = image_slots_[slot_idx].get();

      //     // Only return if image is actually loaded (width > 0)
      //     if (img->is_ready())
      //     {
      //       return img;
      //     }
      //   }
      // }

      return nullptr;
    }

    // Not used
    SlideshowSlot *SlideshowComponent::get_slot(size_t slot_index)
    {
      if (slot_index < image_slots_.size())
      {
        return image_slots_[slot_index].get();
      }
      return nullptr;
    }

    // Not used
    SlideshowSlot *SlideshowComponent::get_current_right_image()
    {
      // if (!pair_layout_ || queue_.empty())
      //   return nullptr;

      // size_t curr_idx = current_index_ % queue_.size();
      // auto it = loaded_image_pairs_.find(curr_idx);

      // if (it != loaded_image_pairs_.end())
      // {
      //   auto *slot = image_slots_[it->second.second].get();
      //   return (slot && slot->is_ready()) ? slot : nullptr;
      // }

      return nullptr; // Current item is not a pair
    }

    // Not used
    bool SlideshowComponent::is_current_paired() const
    {
      if (!pair_layout_ || queue_.empty())
        return false;

      size_t curr_idx = current_index_ % queue_.size();
      return queue_[curr_idx].is_paired();
    }

    void SlideshowComponent::on_image_ready(size_t slot_index, ImagePosition position)
    {
      ESP_LOGD(TAG, "Image ready in slot %d", slot_index);

      // Remove from loading set
      size_t current_loading = loading_slots_.contains(slot_index) ? loading_slots_[slot_index] : 0;
      if (position == ImagePosition::SINGLE || current_loading <= 1)
      {
        loading_slots_.erase(slot_index);
      }
      else
      {
        loading_slots_.insert_or_assign(slot_index, current_loading - 1);
      }

      bool paired = pair_layout_ && position != ImagePosition::SINGLE;

      // Check if this slot belongs to a pair
      for (const auto &[queue_idx, slot_pair] : loaded_images_)
      {
        if ((position == ImagePosition::PAIR_A || position == ImagePosition::SINGLE) && slot_pair.first == slot_index)
        {
          std::string position_str = (position == ImagePosition::SINGLE) ? "single" : "left";
          // Left side of pair
          ESP_LOGI(TAG, "Loaded %s image: %s (queue index %d)", position_str.c_str(),
                   queue_[queue_idx].source_left.c_str(), queue_idx);
          on_image_ready_callbacks_.call(queue_idx, true, paired);
          return;
        }
        else if (position == ImagePosition::PAIR_B && slot_pair.second == slot_index)
        {
          // Right side of pair
          ESP_LOGI(TAG, "Loaded right image: %s (queue index %d)",
                   queue_[queue_idx].source_right.c_str(), queue_idx);
          on_image_ready_callbacks_.call(queue_idx, false, paired);
          return;
        }
      }
    }

    void SlideshowComponent::on_image_error(size_t slot_index, ImagePosition position)
    {
      ESP_LOGE(TAG, "Error loading image in slot %d", slot_index);

      size_t current_loading = loading_slots_.contains(slot_index) ? loading_slots_[slot_index] : 0;
      if (position == ImagePosition::SINGLE || current_loading <= 1)
      {
        loading_slots_.erase(slot_index);
      }
      else
      {
        loading_slots_.insert_or_assign(slot_index, current_loading - 1);
      }

      std::string position_str = (position == ImagePosition::SINGLE) ? "single" : (position == ImagePosition::PAIR_A) ? "left"
                                                                                                                      : "right";

      // Check if this slot belongs to a pair
      for (const auto &[queue_idx, slot_pair] : loaded_images_)
      {
        if ((position == ImagePosition::PAIR_A || position == ImagePosition::SINGLE) && slot_pair.first == slot_index)
        {
          // Left side of pair or single
          const auto &source = queue_[queue_idx].source_left;
          std::string error = "Failed to load " + position_str + " image: " + source;
          on_error_callbacks_.call(error);
          return;
        }
        else if (position == ImagePosition::PAIR_B && slot_pair.second == slot_index)
        {
          // Right side of pair
          const auto &source = queue_[queue_idx].source_right;
          std::string error = "Failed to load " + position_str + " image: " + source;
          on_error_callbacks_.call(error);
          return;
        }
      }
    }

    // Protected methods

    void SlideshowComponent::update_queue_from_builder_()
    {
      if (!queue_builder_)
      {
        // If no builder is defined, user might have provided no source.
        // We can't do anything.
        return;
      }

      // Execute the user's lambda
      std::vector<std::string> sources = queue_builder_();

      if (sources.empty())
      {
        ESP_LOGW(TAG, "Source lambda returned empty list");
        return;
      }

      std::vector<QueueItem> new_queue;
      for (const auto &src : sources)
      {
        QueueItem item;
        item.source_left = src;
        item.source_right = ""; // Builder doesn't support pairs
        new_queue.push_back(item);
      }

      ESP_LOGI(TAG, "Queue updated: %d items", new_queue.size());

      queue_ = new_queue;

      if (current_index_ >= queue_.size())
      {
        current_index_ = 0;
      }

      on_queue_updated_callbacks_.call(queue_.size());

      // Mark slots as needing reload
      slots_dirty_ = true;
    }

    void SlideshowComponent::ensure_slots_loaded_()
    {
      if (queue_.empty() || image_slots_.size() < 2)
      {
        return;
      }

      size_t current_idx = current_index_ % queue_.size();
      std::set<size_t> desired = {current_idx};

      // Add prev/next if queue size permits
      if (queue_.size() > 1)
      {
        desired.insert(retreat_index(current_idx, queue_.size()));
        desired.insert(advance_index(current_idx, queue_.size()));
      }

      // Release items outside window
      release_outside_window_(desired);

      // Load missing items (1 or 2 slots per item)
      for (size_t queue_idx : desired)
      {
        const auto &item = queue_[queue_idx];

        // TODO:? check if `pair_layout_` is true
        if (item.is_paired())
        {
          if (!is_slot_loaded_(queue_idx, 2))
          {
            load_to_slots_(queue_idx, 2);
          }
        }
        else
        {
          if (!is_slot_loaded_(queue_idx))
          {
            load_to_slots_(queue_idx, 1);
          }
        }
      }
    }

    bool SlideshowComponent::is_slot_loaded_(size_t queue_idx, size_t needed) const
    {
      return loaded_images_.contains(queue_idx) && (needed == 1 || (needed == 2 && loaded_images_.at(queue_idx).second != SIZE_MAX));
    }

    void SlideshowComponent::release_outside_window_(const std::set<size_t> &desired)
    {
      auto it = loaded_images_.begin();
      while (it != loaded_images_.end())
      {
        if (desired.find(it->first) == desired.end())
        {
          release_slot_(it->second);
          it = loaded_images_.erase(it);
        }
        else
        {
          ++it;
        }
      }
    }

    void SlideshowComponent::load_to_slots_(size_t queue_idx, size_t needed = 1)
    {
      std::vector<size_t> slots;
      slots.reserve(needed);
      for (size_t i = 0; i < needed; i++)
      {
        size_t slot = find_free_slot_();
        if (slot == SIZE_MAX)
        {
          ESP_LOGW(TAG, "Insufficient slots for item at index %d", queue_idx);
          // Release any previously acquired slots
          for (size_t s : slots)
          {
            release_slot_(s);
          }
          return;
        }
        slots.push_back(slot);
      }

      const auto &item = queue_[queue_idx];
      if (needed == 1)
      {
        load_image_to_slot_(slots[0], item.source_left, ImagePosition::SINGLE);
        loaded_images_[queue_idx] = {slots[0], SIZE_MAX};
        return;
      }
      load_image_to_slot_(slots[0], item.source_left, ImagePosition::PAIR_A);
      load_image_to_slot_(slots[1], item.source_right, ImagePosition::PAIR_B);
      loaded_images_[queue_idx] = {slots[0], slots[1]};
    }

    size_t SlideshowComponent::find_free_slot_()
    {
      for (size_t i = 0; i < image_slots_.size(); i++)
      {
        size_t mod_i = (i + current_index_) % image_slots_.size();
        bool in_use = false;

        for (const auto &[queue_idx, slot_pair] : loaded_images_)
        {
          if (slot_pair.first == mod_i || slot_pair.second == mod_i)
          {
            in_use = true;
            break;
          }
        }

        // Check if slot is currently loading
        if (loading_slots_.find(mod_i) != loading_slots_.end())
        {
          in_use = true;
        }

        if (!in_use)
        {
          return mod_i;
        }
      }

      return SIZE_MAX; // No free slot
    }

    void SlideshowComponent::release_slot_(std::pair<size_t, size_t> pair)
    {
      release_slot_(pair.first);
      if (pair.second != SIZE_MAX)
      {

        release_slot_(pair.second);
      }
    }
    void SlideshowComponent::release_slot_(size_t slot_index)
    {
      if (slot_index >= image_slots_.size())
      {
        return;
      }

      auto *img = image_slots_[slot_index].get();
      if (img->is_ready())
      {
        ESP_LOGD(TAG, "Calling release() on slot %d", slot_index);
        img->release();
      }

      loading_slots_.erase(slot_index);
    }

    void SlideshowComponent::load_image_to_slot_(size_t queue_index, size_t slot_index)
    {
      if (queue_index >= queue_.size() || slot_index >= image_slots_.size())
        return;

      const QueueItem &item = queue_[queue_index];
      load_image_to_slot_(slot_index, item.source_left, ImagePosition::SINGLE);
    }
    void SlideshowComponent::load_image_to_slot_(size_t slot_index, const std::string &source, ImagePosition position)
    {
      if (slot_index >= image_slots_.size())
        return;

      auto *slot = image_slots_[slot_index].get();

      ESP_LOGI(TAG, "Loading source '%s' into slot %d", source.c_str(), slot_index);

      size_t current_loading = loading_slots_.contains(slot_index) ? loading_slots_[slot_index] : 0;
      loading_slots_.insert_or_assign(slot_index, current_loading + 1);

      slot->set_source(source);
      slot->update();

      // Use weak callback to avoid dangling references
      slot->callback_once([this, slot_index, position](bool success)
                          {
        // TODO: Check if slot is still valid/loading before processing callback

        if (success) {
          this->on_image_ready(slot_index, position);
        } else {
          this->on_image_error(slot_index, position);
        } });
    }

    bool SlideshowComponent::is_slot_loading_(size_t slot_index) const
    {
      return loading_slots_.contains(slot_index);
    }

  } // namespace slideshow
} // namespace esphome