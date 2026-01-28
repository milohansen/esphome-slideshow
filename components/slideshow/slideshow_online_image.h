#pragma once

#include "esphome/core/component.h"
#include "esphome/components/online_image/online_image.h"

#include "slideshow.h"

namespace esphome
{
  namespace slideshow
  {
    class OnlineImageSlot : public SlideshowSlot
    {
    public:
      OnlineImageSlot(esphome::online_image::OnlineImage *img) : img_(img)
      {
        this->img_->add_on_finished_callback([this](bool cached)
                                             {
                                              ESP_LOGI("slideshow", "Image finished with cached: %s", cached ? "true" : "false");
                                              this->callbacks_.call(true);
                                              this->ready_ = true;
                                              this->failed_ = false; });
        this->img_->add_on_error_callback([this]()
                                          {
                                            this->callbacks_.call(false);
                                            this->ready_ = false;
                                            this->failed_ = true; });
      }

      void set_source(const std::string &source) override
      {
        this->img_->set_url(source);
      }

      void update() override
      {
        this->img_->update();
      }

      void release() override
      {
        this->img_->release();
      }

      esphome::image::Image *get_image() override
      {
        return this->img_;
      }

      bool is_ready() override
      {
        return this->ready_;
      }

      bool is_failed() override
      {
        return this->failed_;
      }

    protected:
      online_image::OnlineImage *img_;
      bool ready_{false};
      bool failed_{false};
    };

  } // namespace slideshow
} // namespace esphome