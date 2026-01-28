#pragma once

#include "esphome/core/component.h"

#include "slideshow.h"

namespace esphome
{
  namespace slideshow
  {
    class EmbeddedImageSlot : public SlideshowSlot
    {
    public:
      EmbeddedImageSlot(esphome::image::Image *img) : img_(img) {}

      void set_source(const std::string &source) override
      {
        // Translate the generic source into what local_image expects
        ESP_LOGE("slideshow", "EmbeddedImageSlot does not support set_source with string. Source: %s", source.c_str());
      }

      void update() override
      {
        this->callbacks_.call(true);
      }

      void release() override
      {
        ESP_LOGI("slideshow", "EmbeddedImageSlot does not support release. Image cannot be released.");
      }

      esphome::image::Image *get_image() override
      {
        return this->img_;
      }

      bool is_ready() override
      {
        return true;
      }

      bool is_failed() override
      {
        return false;
      }

    protected:
      esphome::image::Image *img_;
    };

  } // namespace slideshow
} // namespace esphome