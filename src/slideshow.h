// slideshow.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/online_image/online_image.h"
#include "esphome/components/http_request/http_request.h"
#include <vector>
#include <map>
#include <set>

namespace esphome
{
  namespace slideshow
  {

    // Abstract interface for any slot (Online, Local, Generated)
    class SlideshowSlot
    {
    public:
      virtual ~SlideshowSlot() = default;

      // The slideshow calls this to load new content
      virtual void set_source(const std::string &source) = 0;

      // Trigger the loading process (download or file read)
      virtual void update() = 0;

      // Release memory if possible
      virtual void release() = 0;

      // Return the underlying generic Image for the Display component
      virtual display::Image *get_image() = 0;

      // Status check
      virtual bool is_ready() = 0;
      virtual bool is_failed() = 0;
    };

    struct QueueItem
    {
      std::string image_id;
      std::string url;
    };

    class SlideshowComponent : public Component
    {
    public:
      void setup() override;
      void loop() override;
      void dump_config() override;
      float get_setup_priority() const override { return setup_priority::LATE; }

      // Configuration
      void set_backend_url(const std::string &url) { backend_url_ = url; }
      void set_device_id(const std::string &id) { device_id_ = id; }
      void set_advance_interval(uint32_t ms) { advance_interval_ = ms; }
      void set_queue_refresh_interval(uint32_t ms) { queue_refresh_interval_ = ms; }
      // void set_auto_advance(bool enable) { auto_advance_ = enable; }
      void add_image_slot(online_image::OnlineImage *slot);
      void set_http_request(http_request::HttpRequestComponent *http) { http_request_ = http; }

      // Control API
      void advance();
      void previous();
      void pause();
      void resume();
      void jump_to(size_t index);
      void refresh_queue();

      // State queries
      size_t current_index() const { return current_index_; }
      bool is_paused() const { return paused_; }
      size_t queue_size() const { return queue_.size(); }
      online_image::OnlineImage *get_current_image();
      online_image::OnlineImage *get_slot(size_t slot_index);

      // Called by online_image callbacks
      void on_image_ready(size_t slot_index, bool from_cache);
      void on_image_error(size_t slot_index);

      // Callbacks
      void add_on_advance_callback(std::function<void(size_t)> &&callback)
      {
        on_advance_callbacks_.add(std::move(callback));
      }
      void add_on_image_ready_callback(std::function<void(size_t, bool)> &&callback)
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

    protected:
      // Queue management
      void fetch_queue_();
      void parse_queue_response_(const std::string &json);
      std::string build_image_url_(const std::string &image_id);

      // Slot management
      void ensure_slots_loaded_();
      size_t find_free_slot_();
      void release_slot_(size_t slot_index);
      void load_image_to_slot_(size_t queue_index, size_t slot_index);
      bool is_slot_loading_(size_t slot_index);

      // State
      std::string backend_url_;
      std::string device_id_;
      uint32_t advance_interval_{10000};
      uint32_t queue_refresh_interval_{60000};
      // bool auto_advance_{true};
      bool paused_{false};

      // Queue data
      std::vector<QueueItem> queue_;
      size_t current_index_{0};

      // Image slots
      std::vector<online_image::OnlineImage *> image_slots_;

      // Mapping: queue_index -> slot_index
      std::map<size_t, size_t> loaded_images_;

      // Track which slots are currently downloading
      std::set<size_t> loading_slots_;

      // Timing
      uint32_t last_advance_{0};
      uint32_t last_queue_refresh_{0};

      // Dependencies
      http_request::HttpRequestComponent *http_request_{nullptr};

      // Callbacks
      CallbackManager<void(size_t)> on_advance_callbacks_;
      CallbackManager<void(size_t, bool)> on_image_ready_callbacks_;
      CallbackManager<void(size_t)> on_queue_updated_callbacks_;
      CallbackManager<void(std::string)> on_error_callbacks_;
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

    class OnImageReadyTrigger : public Trigger<size_t, bool>
    {
    public:
      explicit OnImageReadyTrigger(SlideshowComponent *parent)
      {
        parent->add_on_image_ready_callback([this](size_t index, bool cached)
                                            { this->trigger(index, cached); });
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

    // Actions
    template <typename... Ts>
    class AdvanceAction : public Action<Ts...>
    {
    public:
      explicit AdvanceAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(Ts... x) override { slideshow_->advance(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class PreviousAction : public Action<Ts...>
    {
    public:
      explicit PreviousAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(Ts... x) override { slideshow_->previous(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class PauseAction : public Action<Ts...>
    {
    public:
      explicit PauseAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(Ts... x) override { slideshow_->pause(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class ResumeAction : public Action<Ts...>
    {
    public:
      explicit ResumeAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(Ts... x) override { slideshow_->resume(); }

    protected:
      SlideshowComponent *slideshow_;
    };

    template <typename... Ts>
    class RefreshQueueAction : public Action<Ts...>
    {
    public:
      explicit RefreshQueueAction(SlideshowComponent *slideshow) : slideshow_(slideshow) {}
      void play(Ts... x) override { slideshow_->refresh_queue(); }

    protected:
      SlideshowComponent *slideshow_;
    };

  } // namespace slideshow
} // namespace esphome