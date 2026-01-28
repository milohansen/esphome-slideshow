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
        this->img_->add_on_finished_callback([this](bool success)
                                             { this->callbacks_.call(success); });
        this->img_->add_on_error_callback([this]()
                                          { this->callbacks_.call(false); });
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
        return this->img_->is_ready();
      }

      bool is_failed() override
      {
        return this->img_->is_failed();
      }

    protected:
      online_image::OnlineImage *img_;
    };

  } // namespace slideshow
} // namespace esphome