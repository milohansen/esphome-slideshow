// slideshow.cpp
#include "slideshow.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome
{
  namespace slideshow
  {

    static const char *const TAG = "slideshow";

    void SlideshowComponent::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up slideshow...");

      if (image_slots_.empty())
      {
        ESP_LOGE(TAG, "No image slots configured!");
        mark_failed();
        return;
      }

      if (backend_url_.empty() || device_id_.empty())
      {
        ESP_LOGE(TAG, "Backend URL and device ID must be configured!");
        mark_failed();
        return;
      }

      // Initial queue fetch
      refresh_queue();
    }

    void SlideshowComponent::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Slideshow:");
      ESP_LOGCONFIG(TAG, "  Backend URL: %s", backend_url_.c_str());
      ESP_LOGCONFIG(TAG, "  Device ID: %s", device_id_.c_str());
      ESP_LOGCONFIG(TAG, "  Advance interval: %ums", advance_interval_);
      ESP_LOGCONFIG(TAG, "  Queue refresh interval: %ums", queue_refresh_interval_);
      // ESP_LOGCONFIG(TAG, "  Auto advance: %s", auto_advance_ ? "YES" : "NO");
      ESP_LOGCONFIG(TAG, "  Image slots: %d", image_slots_.size());
    }

    void SlideshowComponent::loop()
    {
      // Auto-advance timer
      if (advance_interval_ > 0 && !paused_ && queue_.size() > 0)
      {
        uint32_t now = millis();
        if (now - last_advance_ >= advance_interval_)
        {
          advance();
          last_advance_ = now;
        }
      }

      // Queue refresh timer
      if (queue_refresh_interval_ > 0)
      {
        uint32_t now = millis();
        if (now - last_queue_refresh_ >= queue_refresh_interval_)
        {
          refresh_queue();
        }
      }

      // Ensure proper slots are loaded
      ensure_slots_loaded_();
    }

      void SlideshowComponent::add_image_slot(online_image::OnlineImage *slot)
      {
        // Create the adapter and store it
        auto *adapter = new OnlineImageSlot(slot);
        this->image_slots_.push_back(adapter);
      }
      void SlideshowComponent::add_image_slot(esphome::image::Image *slot)
      {
        // Create the adapter and store it
        auto *adapter = new EmbeddedImageSlot(slot);
        this->image_slots_.push_back(adapter);
      }

// Guarded implementation for LocalImage
#ifdef USE_LOCAL_IMAGE
      void SlideshowComponent::add_image_slot(local_image::LocalImage *slot)
      {
        // Create the adapter and store it
        auto *adapter = new LocalImageSlot(slot);
        this->image_slots_.push_back(adapter);
      }
#endif

    void SlideshowComponent::advance()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot advance: queue is empty");
        return;
      }

      if (current_index_ + 1 >= queue_.size())
      {
        // End of queue - loop back to start
        current_index_ = 0;
      }
      else
      {
        current_index_++;
      }

      ESP_LOGD(TAG, "Advanced to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_].image_id.c_str());

      // Trigger slots reload (will prefetch next)
      ensure_slots_loaded_();

      // Fire callback
      on_advance_callbacks_.call(current_index_);
    }

    void SlideshowComponent::previous()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot go back: queue is empty");
        return;
      }

      if (current_index_ == 0)
      {
        // At start - wrap to end
        current_index_ = queue_.size() - 1;
      }
      else
      {
        current_index_--;
      }

      ESP_LOGD(TAG, "Went back to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_].image_id.c_str());

      ensure_slots_loaded_();
      on_advance_callbacks_.call(current_index_);
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

    void SlideshowComponent::jump_to(size_t index)
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot jump: queue is empty");
        return;
      }

      if (index >= queue_.size())
      {
        ESP_LOGW(TAG, "Invalid index %d (queue size: %d)", index, queue_.size());
        return;
      }

      current_index_ = index;
      ESP_LOGI(TAG, "Jumped to index %d (ID: %s)",
               current_index_, queue_[current_index_].image_id.c_str());

      ensure_slots_loaded_();
      on_advance_callbacks_.call(current_index_);
    }

    void SlideshowComponent::refresh_queue()
    {
      ESP_LOGD(TAG, "Refreshing queue from backend...");
      fetch_queue_();
    }

    SlideshowSlot *SlideshowComponent::get_current_image()
    {
      auto it = loaded_images_.find(current_index_);
      if (it != loaded_images_.end())
      {
        size_t slot_idx = it->second;
        auto *img = image_slots_[slot_idx];

        // Only return if image is actually loaded (width > 0)
        if (img->is_ready())
        {
          return img;
        }
      }
      return nullptr;
    }

    SlideshowSlot *SlideshowComponent::get_slot(size_t slot_index)
    {
      if (slot_index < image_slots_.size())
      {
        return image_slots_[slot_index];
      }
      return nullptr;
    }

    void SlideshowComponent::on_image_ready(size_t slot_index)
    {
      ESP_LOGD(TAG, "Image ready in slot %d", slot_index);

      // Remove from loading set
      loading_slots_.erase(slot_index);

      // Find which queue index this slot corresponds to
      for (const auto &pair : loaded_images_)
      {
        if (pair.second == slot_index)
        {
          ESP_LOGI(TAG, "Loaded image %s (queue index %d)",
                   queue_[pair.first].image_id.c_str(), pair.first);

          // Fire callback
          on_image_ready_callbacks_.call(pair.first, false);
          break;
        }
      }
    }

    void SlideshowComponent::on_image_error(size_t slot_index)
    {
      ESP_LOGE(TAG, "Error loading image in slot %d", slot_index);

      // Remove from loading set
      loading_slots_.erase(slot_index);

      // Find which queue index failed
      for (const auto &pair : loaded_images_)
      {
        if (pair.second == slot_index)
        {
          std::string error = "Failed to load image: " + queue_[pair.first].image_id;
          on_error_callbacks_.call(error);

          // Clear the mapping so we can retry
          loaded_images_.erase(pair.first);
          break;
        }
      }
    }

    // Protected methods

    void SlideshowComponent::fetch_queue_()
    {
      if (!http_request_)
      {
        ESP_LOGE(TAG, "HTTP request component not configured!");
        return;
      }

      std::string url = backend_url_ + "/slideshow";

      ESP_LOGD(TAG, "Fetching queue from: %s", url.c_str());

      http_request_->get(url, [this](int status_code, const std::string &body)
                         {
    if (status_code == 200) {
      ESP_LOGD(TAG, "Queue response: %s", body.c_str());
      parse_queue_response_(body);
    } else {
      ESP_LOGE(TAG, "Queue fetch failed with status %d", status_code);
      on_error_callbacks_.call("Queue fetch failed");
    } });

      last_queue_refresh_ = millis();
    }

    void SlideshowComponent::parse_queue_response_(const std::string &json)
    {
      // Simple JSON parsing for {"queue": ["id1", "id2", ...]}
      // For production, consider using ArduinoJson library

      std::vector<QueueItem> new_queue;

      // Find "queue" array
      size_t queue_pos = json.find("\"queue\"");
      if (queue_pos == std::string::npos)
      {
        ESP_LOGE(TAG, "Invalid queue response: missing 'queue' field");
        return;
      }

      size_t array_start = json.find('[', queue_pos);
      size_t array_end = json.find(']', array_start);

      if (array_start == std::string::npos || array_end == std::string::npos)
      {
        ESP_LOGE(TAG, "Invalid queue response: malformed array");
        return;
      }

      // Extract array content
      std::string array_content = json.substr(array_start + 1, array_end - array_start - 1);

      // Parse image IDs (simple approach - split by comma and clean quotes)
      size_t pos = 0;
      while (pos < array_content.length())
      {
        size_t quote1 = array_content.find('"', pos);
        if (quote1 == std::string::npos)
          break;

        size_t quote2 = array_content.find('"', quote1 + 1);
        if (quote2 == std::string::npos)
          break;

        std::string image_id = array_content.substr(quote1 + 1, quote2 - quote1 - 1);

        QueueItem item;
        item.image_id = image_id;
        item.url = build_image_url_(image_id);
        new_queue.push_back(item);

        pos = quote2 + 1;
      }

      if (new_queue.empty())
      {
        ESP_LOGW(TAG, "Parsed queue is empty");
        return;
      }

      ESP_LOGI(TAG, "Queue updated: %d images", new_queue.size());

      // Update queue
      queue_ = new_queue;

      // Ensure current index is valid
      if (current_index_ >= queue_.size())
      {
        current_index_ = 0;
      }

      // Fire callback
      on_queue_updated_callbacks_.call(queue_.size());

      // Reload slots with new queue
      ensure_slots_loaded_();
    }

    std::string SlideshowComponent::build_image_url_(const std::string &image_id)
    {
      return backend_url_ + "/images/" + device_id_ + "/" + image_id;
    }

    void SlideshowComponent::ensure_slots_loaded_()
    {
      if (queue_.empty() || image_slots_.empty())
      {
        return;
      }

      // Determine which queue indices we want loaded
      std::set<size_t> desired;

      // Always want current
      desired.insert(current_index_);

      // Previous (if exists)
      if (current_index_ > 0)
      {
        desired.insert(current_index_ - 1);
      }

      // Next (if exists)
      if (current_index_ + 1 < queue_.size())
      {
        desired.insert(current_index_ + 1);
      }

      // Release slots outside the desired window
      auto it = loaded_images_.begin();
      while (it != loaded_images_.end())
      {
        size_t queue_idx = it->first;
        size_t slot_idx = it->second;

        if (desired.find(queue_idx) == desired.end())
        {
          // This image is no longer needed
          ESP_LOGD(TAG, "Releasing slot %d (was queue index %d)", slot_idx, queue_idx);
          release_slot_(slot_idx);
          it = loaded_images_.erase(it);
        }
        else
        {
          ++it;
        }
      }

      // Load missing images
      for (size_t queue_idx : desired)
      {
        // Check if already loaded or loading
        if (loaded_images_.find(queue_idx) != loaded_images_.end())
        {
          continue; // Already loaded
        }

        // Find a free slot
        size_t slot_idx = find_free_slot_();
        if (slot_idx == SIZE_MAX)
        {
          ESP_LOGW(TAG, "No free slots available for queue index %d", queue_idx);
          continue;
        }

        // Load this image
        load_image_to_slot_(queue_idx, slot_idx);
      }
    }

    size_t SlideshowComponent::find_free_slot_()
    {
      for (size_t i = 0; i < image_slots_.size(); i++)
      {
        // Check if slot is mapped to a queue index
        bool in_use = false;
        for (const auto &pair : loaded_images_)
        {
          if (pair.second == i)
          {
            in_use = true;
            break;
          }
        }

        // Check if slot is currently loading
        if (loading_slots_.find(i) != loading_slots_.end())
        {
          in_use = true;
        }

        if (!in_use)
        {
          return i;
        }
      }

      return SIZE_MAX; // No free slot
    }

    void SlideshowComponent::release_slot_(size_t slot_index)
    {
      if (slot_index >= image_slots_.size())
      {
        return;
      }

      auto *img = image_slots_[slot_index];
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
      {
        return;
      }

      auto *slot = image_slots_[slot_index];
      const QueueItem &item = queue_[queue_index];

      ESP_LOGI(TAG, "Loading image %s into slot %d (queue index %d)",
               item.image_id.c_str(), slot_index, queue_index);

      // Update mapping
      loaded_images_[queue_index] = slot_index;
      loading_slots_.insert(slot_index);

      // Set URL and trigger download
      // slot->set_url(item.url);
      slot->set_source(queue_[queue_index].url);
      slot->update();
    }

    bool SlideshowComponent::is_slot_loading_(size_t slot_index)
    {
      return loading_slots_.find(slot_index) != loading_slots_.end();
    }

  } // namespace slideshow
} // namespace esphome