#include "sensor_aggregation.h"
#include <algorithm>
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace sensor_aggregation {

static const char *const TAG = "sensor_aggregation";

void SensorAggregationComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Sensor Aggregation...");
  this->_next_delay_expiry = 0;
  this->_next_calibration_check = UINT32_MAX;

  if(this->presence_sensor_) {
    this->presence_sensor_->publish_initial_state(false);
  }

  if(this->movement_sensor_) {
    this->movement_sensor_->publish_initial_state(false);
  }

  if(this->movement_towards_sensor_) {
    this->movement_towards_sensor_->publish_initial_state(false);
  }

  if(this->movement_away_sensor_) {
    this->movement_away_sensor_->publish_initial_state(false);
  }

  uint32_t now = millis();
  this->update_all_detection_counts_(now);
}

void SensorAggregationComponent::loop() {
  uint32_t now = millis();

  if (now >= this->_next_delay_expiry) {
    update_all_detection_counts_(now);
  } if (now >= this->_next_calibration_check) {
    check_calibration_(now);
  }
}

void SensorAggregationComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Sensor Aggregation:");
  ESP_LOGCONFIG(TAG, "  Number of Inputs: %u", this->inputs_.size());
}

void SensorAggregationComponent::register_input(binary_sensor::BinarySensor *input, SensorType type,
                                              uint32_t detect_delay, uint32_t release_delay, const std::string &tag) {
  auto now = millis();
  bool state = input->state;
  auto input_config = InputConfig {
    .sensor = input,
    .type = type,
    .detect_delay = detect_delay,
    .release_delay = release_delay,
    .curent_state = {state},
    .delayed_state = {},
    .cancel_deadline = false,
    .last_raw_value_change = now,
    .last_actual_value_change = now,
    .tag = tag
  };
  
  // Register callback for this sensor
  input->add_on_state_callback([this, input](bool state) {
    this->on_input_state_change_(input, state);
  });

  if(state) {
    this->increment_detection_count_(type);
  }
  this->inputs_.push_back(input_config);
}

void SensorAggregationComponent::register_input_calibration(InputCalibrationConfig* cal_config) {
   
  auto now = millis();

  // Update the next calibration check timestamp
  cal_config->set_is_clear(now);
  this->_next_calibration_check = std::min(cal_config->calibration_deadline(), this->_next_calibration_check);

  // Store the configuration instance
  this->input_calibrations_.push_back(cal_config);
}

void SensorAggregationComponent::update_all_detection_counts_(uint32_t now) {
  this->_next_delay_expiry = this->max_delay_interval_;
  
  // Reset all counts, the total counts will be up to date once this method completes
  this->reset_detection_counts_();

  bool check_calibrations = false;

  // Reset all devices waiting to set true
  if(this->_reset_delays_next_interval) {
    for (auto &input : this->inputs_) { 
      input.expedite_if_delayed_state_is_true();
    }
    this->_reset_delays_next_interval = false;
  }

  for (auto &input : this->inputs_) {

    bool state = false;
    uint32_t ms_since_last_update = input.ms_since_last_actual_value_update(now);

    auto update_status = input.yield_detect_state(now, &state);
    if(state) {
      this->increment_detection_count_(input.type);
    }

    switch (update_status) {
      case UPDATE_STATE__NO_UPDATE: {
        // No value changed
        break;
      } 
      case UPDATE_STATE__DELAYED_UPDATE: {
        // Wait for the delay before setting state
        this->set_next_delay_deadline(input);
        break;
      }
      case UPDATE_STATE__UPDATED: {

        if(!state) {
          ESP_LOGD(TAG, "Input Sensor \"%s\" triggered ACTUAL was OFF for %ums", input.sensor->get_name().c_str(), ms_since_last_update);
        } else {
          ESP_LOGD(TAG, "Input Sensor \"%s\" triggered ACTUAL was ON for %ums", input.sensor->get_name().c_str(), ms_since_last_update);
        }
        // Value change
        this->state_callback_.call(input.tag, state);

        if(!state) {
          // If value set to false then update calibration delays 
          this->_next_calibration_check = now;
        }
      }
    }
  }

  this->publish_detection_states_changes();
}

void SensorAggregationComponent::on_input_state_change_(binary_sensor::BinarySensor *sensor, bool state) {
  // Find the input config for this sensor
  for (auto &input : this->inputs_) {
    if (input.sensor != sensor) {
      continue;
    }
   
    uint32_t now = millis();
    uint32_t ms_since_last_raw_value_update = input.ms_since_last_raw_value_update(now);
    uint32_t ms_since_last_actual_value_update = input.ms_since_last_actual_value_update(now);

    if(state) {
      ESP_LOGD(TAG, "Input Sensor \"%s\" triggered RAW was OFF for %ums", input.sensor->get_name().c_str(), ms_since_last_raw_value_update);
    } else {
      ESP_LOGD(TAG, "Input Sensor \"%s\" triggered RAW was ON for %ums", input.sensor->get_name().c_str(), ms_since_last_raw_value_update);
    }
    
    switch (input.push_or_delay_state(state, now)) {
      case UPDATE_STATE__NO_UPDATE: {
        return;
      } 
      case UPDATE_STATE__DELAYED_UPDATE: {
        if(state && has_detection()) {
          // If the state is true, and there are already detections
          // Ignore delayed state
          this->reset_delays_on_next_interval(now);
        } else {
          this->set_next_delay_deadline(input);
        }
        return;
      }
      case UPDATE_STATE__UPDATED: {

        if(state) {
          ESP_LOGD(TAG, "Input Sensor \"%s\" triggered ACTUAL was OFF for %ums", input.sensor->get_name().c_str(), ms_since_last_actual_value_update);
        } else {
          ESP_LOGD(TAG, "Input Sensor \"%s\" triggered ACTUAL was NO for %ums", input.sensor->get_name().c_str(), ms_since_last_actual_value_update);
        }

        this->increment_detection_count_(input.type, state? +1 : -1);

        // Notify state change handlers
        this->state_callback_.call(input.tag, state);
        this->publish_detection_states_changes();

        if(state) {
          // If value set to true, then cancel the delays for
          // any other sensors waiting with a true state
          this->reset_delays_on_next_interval(now);
        } else {
          // If value set to false then update calibration delays next loop
          this->_next_calibration_check = now;
        }

        return;
      }
    }
  }
}

void SensorAggregationComponent::check_calibration_(uint32_t now) {

  this->_next_calibration_check = UINT32_MAX;
  for (auto input_calibration : this->input_calibrations_) {

    bool can_calibrate = !std::any_of(inputs_.begin(), inputs_.end(),
      [input_calibration](const InputConfig& input_check) {
          // Skip sensors that are explicitly excluded
          auto ignore_sensor = std::find(input_calibration->exclude_sensors.begin(), input_calibration->exclude_sensors.end(), input_check.sensor);
          if(ignore_sensor != input_calibration->exclude_sensors.end()) {
            return false;
          }
          // Note that the real sensor value is used rather than the delayed value
          // this is because the calling calibrate while the scene isnt really clear 
          // can put the device into a invalidate state
          return input_check.sensor->state;
      });

    if(can_calibrate) {
      input_calibration->set_is_clear(now);

    } else {
      input_calibration->set_is_not_clear();
      continue;
    }

    if (now >= input_calibration->calibration_deadline()) {
      input_calibration->calibrate(now);
    }
    this->_next_calibration_check = std::min(input_calibration->calibration_deadline(), this->_next_calibration_check);

  }
}

bool SensorAggregationComponent::is_active_by_tag(const std::string &tag) const {
  auto now = millis();
  for (const auto &input : this->inputs_) {
    if (input.tag == tag && input.peek_detect_state(now)) {
      return true;
    }
  }
  return false;
}

std::vector<binary_sensor::BinarySensor *> SensorAggregationComponent::get_active_sensors_by_type(SensorType type) const {
  std::vector<binary_sensor::BinarySensor *> active_sensors;
  auto now = millis();
  for (const auto &input : this->inputs_) {
    if (input.type == type && input.peek_detect_state(now)) {
      active_sensors.push_back(input.sensor);
    }
  }
  return active_sensors;
}

std::vector<binary_sensor::BinarySensor *> SensorAggregationComponent::get_active_sensors_by_tag(const std::string &tag) const {
  std::vector<binary_sensor::BinarySensor *> active_sensors;
  auto now = millis();
  for (const auto &input : this->inputs_) {
    if (input.tag == tag && input.peek_detect_state(now)) {
      active_sensors.push_back(input.sensor);
    }
  }
  return active_sensors;
}

}  // namespace sensor_aggregation
}  // namespace esphome
