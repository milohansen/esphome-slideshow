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

    SlideshowComponent::~SlideshowComponent()
    {
      for (auto *slot : this->image_slots_)
      {
        delete slot;
      }
      this->image_slots_.clear();
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
      else
      {
        this->i_slots_.resize(slot_count_, nullptr);
        // for (size_t i = 0; i < slot_count_; i++)
        // {
        //   this->i_slots_[i] = new EmbeddedImageSlot(new esphome::image::Image());
        // }
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

      // TODO: Schedule intervals properly instead of checking every loop

      // Auto-advance timer
      if (advance_interval_ > 0 && !paused_ && queue_.size() > 0)
      {
        uint32_t now = millis();
        if (now - last_advance_ >= advance_interval_ * 60000)
        {
          advance();
          last_advance_ = now;
        }
      }

      // Queue refresh timer
      if (refresh_interval_ > 0)
      {
        uint32_t now = millis();
        if (now - last_refresh_ >= refresh_interval_ * 60000)
        {
          ESP_LOGD(TAG, "Triggering refresh...");
          // Fire the trigger! Pass current queue size as argument
          this->on_refresh_callbacks_.call(0);
          last_refresh_ = now;
        }
      }

      // Ensure proper slots are loaded
      ensure_slots_loaded_();

      if (needs_more_photos_)
      {
        refresh();
      }
    }

    void SlideshowComponent::add_image_slot(online_image::OnlineImage *slot)
    {
      this->image_slots_.push_back(new OnlineImageSlot(slot));
    }
    void SlideshowComponent::add_image_slot(esphome::image::Image *slot)
    {
      this->image_slots_.push_back(new EmbeddedImageSlot(slot));
    }

// Guarded implementation for LocalImage
#ifdef USE_LOCAL_IMAGE
    void SlideshowComponent::add_image_slot(local_image::LocalImage *slot)
    {
      this->image_slots_.push_back(new LocalImageSlot(slot));
    }
#endif

    void SlideshowComponent::advance()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot advance: queue is empty");
        return;
      }

      current_index_++;
      current_index_mod_ = current_index_ % queue_.size();

      ESP_LOGD(TAG, "Advanced to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_mod_].source.c_str());

      // Fire callback
      on_advance_callbacks_.call(current_index_);

      if (current_index_ + 2 >= queue_.size())
      {
        needs_more_photos_ = true;
      }

      // Trigger slots reload (will prefetch next)
      // ensure_slots_loaded_();
    }

    void SlideshowComponent::previous()
    {
      if (queue_.empty())
      {
        ESP_LOGW(TAG, "Cannot go back: queue is empty");
        return;
      }

      current_index_--;
      current_index_mod_ = current_index_ % queue_.size();

      ESP_LOGD(TAG, "Went back to index %d/%d (ID: %s)",
               current_index_, queue_.size(), queue_[current_index_mod_].source.c_str());

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

      current_index_ = index;
      current_index_mod_ = current_index_ % queue_.size();

      ESP_LOGI(TAG, "Jumped to index %d (ID: %s)",
               current_index_, queue_[current_index_mod_].source.c_str());

      on_advance_callbacks_.call(current_index_);
    }

    void SlideshowComponent::enqueue(const std::vector<std::string> &items)
    {
      if (items.empty())
        return;

      ESP_LOGI(TAG, "Enqueuing %d new items", items.size());

      for (const auto &str : items)
      {
        QueueItem item;
        item.source = str; // Store the URL (or "URL|COLOR" string)
        queue_.push_back(item);
      }

      // Notify listeners
      on_queue_updated_callbacks_.call(queue_.size());
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
                   queue_[pair.first].source.c_str(), pair.first);

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
          std::string error = "Failed to load image: " + queue_[pair.first].source;
          on_error_callbacks_.call(error);

          // Clear the mapping so we can retry
          loaded_images_.erase(pair.first);
          break;
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
        item.source = src;
        new_queue.push_back(item);
      }

      ESP_LOGI(TAG, "Queue updated: %d items", new_queue.size());

      queue_ = new_queue;

      if (current_index_ >= queue_.size())
      {
        current_index_ = 0;
      }

      on_queue_updated_callbacks_.call(queue_.size());
      // ensure_slots_loaded_();
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

      // Previous (wrapped)
      if (queue_.size() > 1)
      {
        size_t prev_index = (current_index_ + queue_.size() - 1) % queue_.size();
        desired.insert(prev_index);
      }

      // Next (wrapped)
      if (queue_.size() > 1)
      {
        size_t next_index = (current_index_ + 1) % queue_.size();
        desired.insert(next_index);
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
        size_t mod_i = (i + current_index_) % image_slots_.size();
        // Check if slot is mapped to a queue index
        bool in_use = false;
        for (const auto &pair : loaded_images_)
        {
          if (pair.second == mod_i)
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
        return;

      auto *slot = image_slots_[slot_index];
      const QueueItem &item = queue_[queue_index];

      ESP_LOGI(TAG, "Loading source '%s' into slot %d", item.source.c_str(), slot_index);

      loaded_images_[queue_index] = slot_index;
      loading_slots_.insert(slot_index);

      slot->set_source(item.source);
      slot->update();
      slot->callback_once([this, slot_index](bool success)
                          {
        if (success) {
        this->on_image_ready(slot_index);
        } else {
        this->on_image_error(slot_index);
        } });
    }

    bool SlideshowComponent::is_slot_loading_(size_t slot_index)
    {
      return loading_slots_.find(slot_index) != loading_slots_.end();
    }

  } // namespace slideshow
} // namespace esphome