// src/slideshow_local_image.h
#include "esphome/components/local_image/local_image.h" // explicit include

class EmbeddedImageSlot : public SlideshowSlot {
 public:
  EmbeddedImageSlot(esphome::image::Image *img) : img_(img) {}

  void set_source(const std::string &source) override {
    // Translate the generic source into what local_image expects
    ESP_LOGE(TAG, "EmbeddedImageSlot does not support set_source with string. Source: %s", source.c_str());
  }

  void update() override {
    // this->img_->load(); // Assuming it has a load/update method
  }

  esphome::image::Image *get_image() override {
    return this->img_;
  }

  bool is_ready() override {
    return this->img_->get_width() > 0;
  }

 protected:
  esphome::image::Image *img_;
};