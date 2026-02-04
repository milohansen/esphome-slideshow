#pragma once

#include "esphome/core/component.h"

#include "slideshow.h"

namespace esphome
{
  namespace slideshow
  {
    /**
     * @brief Adapter for embedded (compiled-in) images.
     * 
     * These images are static and cannot be changed at runtime.
     * 
     * @note This class does not own the image pointer - it's a non-owning observer.
     */
    class EmbeddedImageSlot : public SlideshowSlot
    {
    public:
      explicit EmbeddedImageSlot(esphome::image::Image *img) : img_(img) {}
      
      // Delete copy operations
      EmbeddedImageSlot(const EmbeddedImageSlot&) = delete;
      EmbeddedImageSlot& operator=(const EmbeddedImageSlot&) = delete;

      void set_source(const std::string &source) override
      {
        // Translate the generic source into what local_image expects
        ESP_LOGW("slideshow", "EmbeddedImageSlot does not support set_source with string. Source: %s", source.c_str());
      }

      void update() override
      {
        this->callbacks_.call(true);
      }

      void release() override
      {
        ESP_LOGW("slideshow", "EmbeddedImageSlot does not support release. Image cannot be released.");
      }

      [[nodiscard]] esphome::image::Image *get_image() const override
      {
        return this->img_;
      }

      [[nodiscard]] bool is_ready() const override
      {
        return true;
      }

      [[nodiscard]] bool is_failed() const override
      {
        return false;
      }

    protected:
      esphome::image::Image *img_; // non-owning observer
    };

  } // namespace slideshow
} // namespace esphome