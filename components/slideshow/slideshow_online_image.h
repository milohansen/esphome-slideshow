#pragma once

#include "esphome/core/component.h"
#include "esphome/components/online_image/online_image.h"

#include "slideshow.h"

namespace esphome
{
  namespace slideshow
  {
    /**
     * @brief Adapter for online images that can be downloaded from URLs.
     * 
     * @note This class does not own the image pointer - it's a non-owning observer.
     */
    class OnlineImageSlot : public SlideshowSlot
    {
    public:
      explicit OnlineImageSlot(esphome::online_image::OnlineImage *img) : img_(img)
      {
        this->img_->add_on_finished_callback([this](bool cached)
                                             {
                                              this->callbacks_.call(true);
                                              this->ready_ = true;
                                              this->failed_ = false; });
        this->img_->add_on_error_callback([this]()
                                          {
                                            this->callbacks_.call(false);
                                            this->ready_ = false;
                                            this->failed_ = true; });
      }
      
      // Delete copy operations
      OnlineImageSlot(const OnlineImageSlot&) = delete;
      OnlineImageSlot& operator=(const OnlineImageSlot&) = delete;

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

      [[nodiscard]] esphome::image::Image *get_image() const override
      {
        return this->img_;
      }

      [[nodiscard]] bool is_ready() const override
      {
        return this->ready_;
      }

      [[nodiscard]] bool is_failed() const override
      {
        return this->failed_;
      }

    protected:
      esphome::online_image::OnlineImage *img_; // non-owning observer
      bool ready_{false};
      bool failed_{false};
    };

  } // namespace slideshow
} // namespace esphome