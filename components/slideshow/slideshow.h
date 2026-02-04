// slideshow.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/image/image.h"
#include "esphome/components/online_image/online_image.h"

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <cctype>

namespace esphome
{
  namespace slideshow
  {
    class OnceCallbackManager
    {
    public:
      /// Add a callback to the list.
      void add(std::function<void(bool)> callback) { callbacks_.push_back(std::move(callback)); }

      /// Call all callbacks in this manager.
      void call(bool arg)
      {
        auto callbacks = std::move(callbacks_);
        callbacks_.clear();

        for (auto &cb : callbacks)
        {
          cb(arg);
        }
      }
      size_t size() const { return callbacks_.size(); }

      bool empty() const { return callbacks_.empty(); }

      void clear()
      {
        callbacks_.clear();
      }

    protected:
      std::vector<std::function<void(bool)>> callbacks_;
    };

    /**
     * @brief Abstract interface for any slot (Online, Local, Embedded).
     * 
     * Represents an image that can be loaded from various sources.
     * Implementations must handle their specific image lifecycle.
     * 
     * @note Implementations should be move-only to prevent lifetime issues.
     */
    class SlideshowSlot
    {
    public:
      virtual ~SlideshowSlot() = default;

      /// The slideshow calls this to load new content
      virtual void set_source(const std::string &source) = 0;

      /// Trigger the loading process (download or file read)
      virtual void update() = 0;

      /// Release memory if possible
      virtual void release() = 0;

      /// Return the underlying generic Image for the Display component
      [[nodiscard]] virtual esphome::image::Image *get_image() const = 0;

      /// Status check
      [[nodiscard]] virtual bool is_ready() const = 0;
      [[nodiscard]] virtual bool is_failed() const = 0;

      void callback_once(std::function<void(bool)> &&cb)
      {
        this->callbacks_.add(std::move(cb));
      }

    protected:
      OnceCallbackManager callbacks_;
    };

    struct QueueItem
    {
      std::string source_left;   // Always used: single image URL or left side URL
      std::string source_right;  // Empty for singles, populated for pairs
      
      // Helper to determine if this item is a pair
      [[nodiscard]] bool is_paired() const { 
        return !source_right.empty(); 
      }
      
      // Helper to get slot requirement
      [[nodiscard]] size_t slot_count() const {
        return is_paired() ? 2 : 1;
      }
    };

    using queue_builder_t = std::function<std::vector<std::string>()>;

    /**
     * @brief Main slideshow component that manages image display rotation.
     * 
     * Coordinates loading, caching, and display of images from various sources.
     * Maintains a queue of images and manages a pool of reusable image slots.
     * 
     * @invariant loaded_images_ and loading_slots_ are disjoint sets
     * @invariant All slot indices in both maps are < image_slots_.size()
     * @invariant current_index_ is bounded by queue_.size() when used (modulo operation)
     * 
     * @thread_safety Not thread-safe. Must be called from ESPHome main thread.
     */
    class SlideshowComponent : public Component
    {
    public:
      static constexpr size_t INVALID_SLOT = SIZE_MAX;
      
      void setup() override;
      void loop() override;
      void dump_config() override;
      [[nodiscard]] float get_setup_priority() const override { return setup_priority::LATE; }

      // Configuration
      void set_advance_interval(uint32_t ms) { advance_interval_ = ms; }
      void set_refresh_interval(uint32_t ms) { refresh_interval_ = ms; }
      void set_slot_count(size_t count) { slot_count_ = count; }
      void set_pair_layout(bool enabled) { pair_layout_ = enabled; }

      void set_queue_builder(queue_builder_t &&builder) { queue_builder_ = builder; }

      void add_image_slot(online_image::OnlineImage *slot);
      void add_image_slot(esphome::image::Image *slot);
#ifdef USE_LOCAL_IMAGE
      void add_image_slot(local_image::LocalImage *slot);
#endif

      // Control API
      void advance();
      void previous();
      void pause();
      void resume();
      void jump_to(size_t index);
      void refresh();

      void suspend(bool suspend)
      {
        suspended_ = suspend;
      }

      // State queries
      [[nodiscard]] size_t current_index() const { return current_index_; }
      [[nodiscard]] bool is_paused() const { return paused_; }
      [[nodiscard]] size_t queue_size() const { return queue_.size(); }
      SlideshowSlot *get_current_image();
      SlideshowSlot *get_slot(size_t slot_index);
      
      // Paired mode accessors
      SlideshowSlot *get_current_right_image();
      [[nodiscard]] bool is_current_paired() const;
      [[nodiscard]] bool is_pair_layout() const { return pair_layout_; }

      void enqueue(const std::vector<std::string> &items);
      void clear_queue(); // Optional utility

      // Called by online_image callbacks
      void on_image_ready(size_t slot_index);
      void on_image_error(size_t slot_index);

      // Callbacks
      void add_on_advance_callback(std::function<void(size_t)> &&callback)
      {
        on_advance_callbacks_.add(std::move(callback));
      }
      void add_on_image_ready_callback(std::function<void(size_t, bool, bool, bool)> &&callback)
      {
        on_image_ready_callbacks_.add(std::move(callback));
      }
      void add_on_queue_updated_callback(std::function<void(size_t)> &&callback)
      {
        on_queue_updated_callbacks_.add(std::move(callback));
      }
      void add_on_error_callback(std::function<void(std::string)> &&callback)
      {
        on_error_callbacks_.add(std::move(callback));
      }
      void add_on_refresh_callback(std::function<void(size_t)> &&callback)
      {
        on_refresh_callbacks_.add(std::move(callback));
      }

    protected:
      // Queue management
      void update_queue_from_builder_();

      // Slot management
      void ensure_slots_loaded_();
      void ensure_single_slots_loaded_();
      void ensure_paired_slots_loaded_();
      [[nodiscard]] size_t find_free_slot_();
      void release_slot_(size_t slot_index);
      void load_image_to_slot_(size_t queue_index, size_t slot_index);
      void load_image_to_slot_(size_t queue_index, size_t slot_index, const std::string &source);
      [[nodiscard]] bool is_slot_loading_(size_t slot_index) const;
      
      // Paired mode helpers
      [[nodiscard]] bool is_pair_loaded_(size_t queue_idx) const;
      [[nodiscard]] bool is_single_loaded_(size_t queue_idx) const;
      void release_outside_window_(const std::set<size_t> &desired);
      void load_single_to_slot_(size_t queue_idx);
      void load_pair_to_slots_(size_t queue_idx);
      
      // Index manipulation helpers
      [[nodiscard]] size_t advance_index(size_t current, size_t queue_size) const {
        return queue_size > 0 ? (current + 1) % queue_size : 0;
      }
      
      [[nodiscard]] size_t retreat_index(size_t current, size_t queue_size) const {
        return queue_size > 0 ? (current + queue_size - 1) % queue_size : 0;
      }
      
#ifndef NDEBUG
      /// Validates internal invariants (debug builds only)
      [[nodiscard]] bool check_slot_invariants_() const;
#endif

      // State
      uint32_t advance_interval_{5};
      uint32_t refresh_interval_{25};
      bool paused_{false};
      bool suspended_{false};
      bool pair_layout_{false};  // Enable support for paired images

      bool needs_more_photos_{false};
      bool slots_dirty_{true}; // Flag to track if slots need reloading

      // The Builder Lambda
      queue_builder_t queue_builder_;

      // Queue data
      std::vector<QueueItem> queue_;
      size_t current_index_{0};

      // Image slots
      std::vector<std::unique_ptr<SlideshowSlot>> image_slots_;
      size_t slot_count_{0};

      // Mapping: queue_index -> slot_index (for single images)
      std::map<size_t, size_t> loaded_images_;
      // Mapping: queue_index -> [left_slot_idx, right_slot_idx] (for paired images)
      std::map<size_t, std::pair<size_t, size_t>> loaded_image_pairs_;
      std::set<size_t> loading_slots_;

      // Timing
      uint32_t last_advance_{0};
      uint32_t last_refresh_{0};

      // Callbacks
      CallbackManager<void(size_t)> on_advance_callbacks_;
      CallbackManager<void(size_t, bool, bool, bool)> on_image_ready_callbacks_;
      CallbackManager<void(size_t)> on_queue_updated_callbacks_;
      CallbackManager<void(std::string)> on_error_callbacks_;
      CallbackManager<void(size_t)> on_refresh_callbacks_;
    };

    // Triggers
    class OnAdvanceTrigger : public Trigger<size_t>
    {
    public:
      explicit OnAdvanceTrigger(SlideshowComponent *parent)
      {
        parent->add_on_advance_callback([this](size_t index)
                                        { this->trigger(index); });
      }
    };

    class OnImageReadyTrigger : public Trigger<size_t, bool, bool, bool>
    {
    public:
      explicit OnImageReadyTrigger(SlideshowComponent *parent)
      {
        parent->add_on_image_ready_callback([this](size_t index, bool success, bool is_left, bool is_paired)
                                            { this->trigger(index, success, is_left, is_paired); });
      }
    };

    class OnQueueUpdatedTrigger : public Trigger<size_t>
    {
    public:
      explicit OnQueueUpdatedTrigger(SlideshowComponent *parent)
      {
        parent->add_on_queue_updated_callback([this](size_t size)
                                              { this->trigger(size); });
      }
    };

    class OnErrorTrigger : public Trigger<std::string>
    {
    public:
      explicit OnErrorTrigger(SlideshowComponent *parent)
      {
        parent->add_on_error_callback([this](std::string error)
                                      { this->trigger(error); });
      }
    };

    class OnRefreshTrigger : public Trigger<size_t>
    {
    public:
      explicit OnRefreshTrigger(SlideshowComponent *parent)
      {
        parent->add_on_refresh_callback([this](size_t current_size)
                                        { this->trigger(current_size); });
      }
    };

    // Actions
    template <typename... Ts>
    class AdvanceAction : public Action<Ts...>
    {
    public:
      explicit AdvanceAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->advance(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class PreviousAction : public Action<Ts...>
    {
    public:
      explicit PreviousAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->previous(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class PauseAction : public Action<Ts...>
    {
    public:
      explicit PauseAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->pause(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class ResumeAction : public Action<Ts...>
    {
    public:
      explicit ResumeAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->resume(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class RefreshAction : public Action<Ts...>
    {
    public:
      explicit RefreshAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->refresh(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class EnqueueAction : public Action<Ts...>
    {
    public:
      explicit EnqueueAction(SlideshowComponent *parent) : parent_(parent) {}
      void play(const Ts &...x) override
      {
        // This allows the action to take a std::vector<std::string>
        this->parent_->enqueue(this->items_);
      }
      // Helper to set items from YAML
      void set_items(const std::vector<std::string> &items) { items_ = items; }

    protected:
      SlideshowComponent *parent_;
      std::vector<std::string> items_;
    };

    template <typename... Ts>
    class SuspendAction : public Action<Ts...>
    {
    public:
      explicit SuspendAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->suspend(true); }

    protected:
      SlideshowComponent *slideshow_;
    };
    template <typename... Ts>
    class UnsuspendAction : public Action<Ts...>
    {
    public:
      explicit UnsuspendAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(const Ts &...x) override { this->slideshow_->suspend(false); }

    protected:
      SlideshowComponent *slideshow_;
    };

  } // namespace slideshow
} // namespace esphome