#pragma once

#include "slideshow.h"

#ifdef USE_LOCAL_IMAGE

#include "esphome/components/local_image/local_image.h"

namespace esphome
{
  namespace slideshow
  {
    /**
     * @brief Adapter for images loaded from local filesystem.
     * 
     * @note This class does not own the image pointer - it's a non-owning observer.
     */
    class LocalImageSlot : public SlideshowSlot
    {
    public:
      explicit LocalImageSlot(local_image::LocalImage *img) : img_(img)
      {
        this->img_->add_on_finished_callback([this](bool success)
                                             { this->callbacks_.call(true); });
        this->img_->add_on_error_callback([this]()
                                          { this->callbacks_.call(false); });
      }
      
      // Delete copy operations
      LocalImageSlot(const LocalImageSlot&) = delete;
      LocalImageSlot& operator=(const LocalImageSlot&) = delete;

      void set_source(const std::string &source) override
      {
        // Translate the generic source into what local_image expects
        this->img_->set_file_path(source);
      }

      void update() override
      {
        this->img_->load(); // Assuming it has a load/update method
      }

      void release() override
      {
        this->img_->release();
      }

      [[nodiscard]] esphome::image::Image *get_image() const override
      {
        return this->img_;
      }

      [[nodiscard]] bool is_ready() const override
      {
        return this->img_->get_width() > 0;
      }

      [[nodiscard]] bool is_failed() const override
      {
        return this->img_->get_width() == 0;
      }

    protected:
      local_image::LocalImage *img_; // non-owning observer
    };

  } // namespace slideshow
} // namespace esphome

#endif