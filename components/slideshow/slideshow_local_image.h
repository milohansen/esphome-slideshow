
#ifdef USE_LOCAL_IMAGE

#include "esphome/components/local_image/local_image.h"

namespace esphome
{
  namespace slideshow
  {
    class LocalImageSlot : public SlideshowSlot
    {
    public:
      LocalImageSlot(local_image::LocalImage *img) : img_(img) {}

      void set_source(const std::string &source) override
      {
        // Translate the generic source into what local_image expects
        this->img_->set_file_path(source);
      }

      void update() override
      {
        this->img_->load(); // Assuming it has a load/update method
      }

      esphome::image::Image *get_image() override
      {
        return this->img_;
      }

      bool is_ready() override
      {
        return this->img_->get_width() > 0;
      }

    protected:
      local_image::LocalImage *img_;
    };

  } // namespace slideshow
} // namespace esphome

#endif