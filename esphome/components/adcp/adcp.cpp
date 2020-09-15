#include "adcp.h"
#include "esphome/core/log.h"

namespace esphome {
namespace adcp {

static const char *TAG = "ADCP";

void ADCPComponent::dump_config() { ESP_LOGCONFIG(TAG, "Setting up ADCP..."); }

void ADCPSensor::update() {
  this->power_pin_->digital_write(true);
  delay(this->delay_);
  float state = this->parent_->get_source()->sample();
  this->power_pin_->digital_write(false);
}

}  // namespace adcp
}  // namespace esphome
