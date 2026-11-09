#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include <vector>
#include <functional>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace sensor_aggregation {

enum SensorType {
  SENSOR_TYPE__PRESENCE = 0,
  SENSOR_TYPE__STILL = 1,
  SENSOR_TYPE__MOVEMENT = 2,
  SENSOR_TYPE__MOVEMENT_TOWARDS = 3,
  SENSOR_TYPE__MOVEMENT_AWAY = 4,
};

enum UpdateState {
  UPDATE_STATE__NO_UPDATE = 0,
  UPDATE_STATE__UPDATED = 1,
  UPDATE_STATE__DELAYED_UPDATE = 2
};


struct InputCalibrationConfig {
  uint32_t calibration_delay{0};
  uint32_t first_time_calibration_delay{0};
  uint32_t last_calibration{0};
  uint32_t clear_since_timestamp{0};
  bool has_calibrated_once{false};

  CallbackManager<void()> calibration_callback_{};

  std::vector<binary_sensor::BinarySensor *> exclude_sensors{};

  InputCalibrationConfig(uint32_t calibration_delay = 0,
                         uint32_t first_time_calibration_delay = 0,
                         const std::vector<binary_sensor::BinarySensor *> &exclude_sensors = {})
      : calibration_delay(calibration_delay),
        first_time_calibration_delay(first_time_calibration_delay),
        last_calibration(0),
        exclude_sensors(exclude_sensors),
        has_calibrated_once(false) {
        
  }

  inline void set_is_clear(uint32_t timestamp) {
    clear_since_timestamp = std::min(timestamp, clear_since_timestamp);
  }

  inline void set_is_not_clear() {
    clear_since_timestamp = UINT32_MAX;
  }

  void add_on_calibration_callback(std::function<void()> &&callback) {
    this->calibration_callback_.add(std::move(callback));
  }

  uint32_t next_calibration_delay() const {
    if(!has_calibrated_once && first_time_calibration_delay != 0) {
      return first_time_calibration_delay;
    } 
    return calibration_delay;
  }

  void calibrate(uint32_t timestamp) {
    this->calibration_callback_.call();
    this->has_calibrated_once = true;
    this->last_calibration = timestamp;
  }

  inline uint32_t calibration_deadline() const {
    if(clear_since_timestamp == UINT32_MAX) {
      return UINT32_MAX;
    }

    auto delay = next_calibration_delay();
    if(delay == 0) {
      return UINT32_MAX;
    }
    return std::max(clear_since_timestamp, last_calibration) + delay;
  }
};

class SensorAggregationComponent : public Component {

  struct InputConfig {

    binary_sensor::BinarySensor *sensor;
    SensorType type;

    uint32_t detect_delay;
    uint32_t release_delay;

    optional<bool> curent_state{};
    optional<bool> delayed_state{};

    bool cancel_deadline = false;
    uint32_t last_raw_value_change;
    uint32_t last_actual_value_change;
    std::string tag; 

    uint32_t ms_since_last_raw_value_update(uint32_t now) {
      return now - last_raw_value_change;
    }

    uint32_t ms_since_last_actual_value_update(uint32_t now) {
      return now - last_actual_value_change;
    }

    uint32_t get_delay() {
      if(!this->delayed_state.has_value()) {
        return 0;
      }
      return this->delayed_state.value() ? this->detect_delay : this->release_delay;
    }


    uint32_t get_detect_delay() {
      return detect_delay;
    }

    void set_detect_delay(uint32_t value) {
      detect_delay = value;
    }

    uint32_t get_release_delay() {
      return release_delay;
    }

    void set_release_delay(uint32_t value) {
      release_delay = value;
    }

    bool has_pending_delayed_state() const {
      return this->delayed_state.has_value();
    }

    optional<bool> pending_delayed_state() const {
      return this->delayed_state;
    }

    bool raw_state() const {
      return this->delayed_state.value_or(curent_state.value_or(false));
    }

    bool actual_state() const {
      return curent_state.value_or(false);
    }

    uint32_t delay_deadline() const {
      if(!this->delayed_state.has_value())
        return UINT32_MAX; 

      auto delay = this->delayed_state.value() ? this->detect_delay : this->release_delay;
      if(delay == 0) {
        return UINT32_MAX;
      }
      if(cancel_deadline) {
        return 0;
      }
      return delay + last_raw_value_change;
    }

    void reset_deadline() {

      if(!this->delayed_state.has_value()) {
        return; 
      }
      this->cancel_deadline = true;
    }

    bool expedite_if_delayed_state_is_true() {
      if(!this->delayed_state.value_or(false)) {
        return false;
      }
      this->cancel_deadline = true;
      return true;
    }

    UpdateState push_or_delay_state(bool state, uint32_t now) {

      // If switching back to the current value, cancel the delayed value
      if(this->curent_state.has_value() && state == this->curent_state.value())  {
        this->delayed_state = {};
        this->curent_state = {state};
        last_actual_value_change = now;
        this->cancel_deadline = false;
        this->last_actual_value_change = now;
        this->last_raw_value_change = now;
        return UPDATE_STATE__NO_UPDATE;
      }

      // Ignore setting the same value twice
      if(this->delayed_state.has_value() && state == this->delayed_state.value())  {
        return UPDATE_STATE__NO_UPDATE;
      }

      this->last_raw_value_change = now;
      this->cancel_deadline = false;

      this->cancel_deadline = false;
      if((state ? this->detect_delay : this->release_delay) != 0) {
        this->delayed_state = {state};
        return UPDATE_STATE__DELAYED_UPDATE;
      } else {
        this->last_actual_value_change = now;
        this->delayed_state = {};
        this->curent_state = {state};
        return UPDATE_STATE__UPDATED;
      }
    }

    UpdateState yield_detect_state(uint32_t now, bool* value) {
    
      if(!this->delayed_state.has_value()) {
        *value = this->curent_state.value_or(false);
        return UPDATE_STATE__NO_UPDATE; 
      }

      uint32_t delay = this->delayed_state.value() ? this->detect_delay : this->release_delay;

      if (this->cancel_deadline || now - this->last_raw_value_change >= delay) {
        this->curent_state = this->delayed_state;
        this->delayed_state = {};
        this->last_actual_value_change = now;
        return UPDATE_STATE__UPDATED; 
      }
      *value =  this->curent_state.value_or(!this->delayed_state.value_or(true));
      return UPDATE_STATE__DELAYED_UPDATE;
    }

    bool peek_detect_state(uint32_t now) const {
      if(!this->delayed_state.has_value()) {
        return this->curent_state.value_or(false);
      }

      uint32_t delay = this->delayed_state.value() ? this->detect_delay : this->release_delay;
      if (this->cancel_deadline || now - this->last_raw_value_change >= delay) {
        return this->delayed_state.value(); 
      }

      return this->curent_state.value_or(!this->delayed_state.value_or(true));
    }
  };

 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void loop() override;

  void register_input(binary_sensor::BinarySensor *input, SensorType type, uint32_t detect_delay, uint32_t release_delay, const std::string &tag = "");

  void register_input_calibration(InputCalibrationConfig* cal_config);
  
  void register_presence_binary_sensor(binary_sensor::BinarySensor *sensor) { this->presence_sensor_ = sensor; }
  void register_movement_binary_sensor(binary_sensor::BinarySensor *sensor) { this->movement_sensor_ = sensor; }
  void register_movement_towards_binary_sensor(binary_sensor::BinarySensor *sensor) { this->movement_towards_sensor_ = sensor; }
  void register_movement_away_binary_sensor(binary_sensor::BinarySensor *sensor) { this->movement_away_sensor_ = sensor; }

  bool is_active_by_tag(const std::string &tag) const;
  std::vector<binary_sensor::BinarySensor *> get_active_sensors_by_type(SensorType type) const;
  std::vector<binary_sensor::BinarySensor *> get_active_sensors_by_tag(const std::string &tag) const;

  void add_on_state_callback(std::function<void(const std::string &, bool)> &&callback) {
    this->state_callback_.add(std::move(callback));
  }

  InputConfig* get_input_config_by_binary_sensor(binary_sensor::BinarySensor* input) {
      auto it = std::find_if(inputs_.begin(), inputs_.end(),
          [input](const InputConfig& cfg) { return cfg.sensor == input;  });

      if (it != inputs_.end()) {
          return &(*it); 
      }
      return nullptr; 
  }
  
 protected:
  CallbackManager<void(const std::string &, bool)> state_callback_{};

  std::vector<InputConfig> inputs_{};
  std::vector<InputCalibrationConfig*> input_calibrations_{};

  const uint32_t max_delay_interval_ = 60000;
  uint32_t _next_delay_expiry = 0;
  bool _reset_delays_next_interval = false;
  uint32_t _next_calibration_check = 0;

  bool published_detection_status = false;

  int8_t presence_count = 0;
  bool published_presence_status = false;

  int8_t movement_count = 0;
  bool published_movement_status = false;

  int8_t movement_towards_count = 0;
  bool published_movement_towards_status = false;

  int8_t movement_away_count = 0;
  bool published_movement_away_status = false;

  binary_sensor::BinarySensor *presence_sensor_{nullptr};
  binary_sensor::BinarySensor *movement_sensor_{nullptr};
  binary_sensor::BinarySensor *movement_towards_sensor_{nullptr};
  binary_sensor::BinarySensor *movement_away_sensor_{nullptr};


  inline bool has_detection() {
    return (presence_count + movement_count + movement_towards_count + movement_away_count) != 0;
  }

  inline bool presence_detected() {
    return presence_count != 0;
  }

  inline bool movement_detected() {
    return movement_count != 0;
  }

  inline bool movement_towards_detected() {
    return movement_towards_count != 0;
  }

  inline bool movement_away_detected() {
    return movement_away_count != 0;
  }


  inline void set_next_delay_deadline(InputConfig& input) {
    auto deadline = input.delay_deadline();

    if (this->_next_delay_expiry > deadline) {

      this->_next_delay_expiry = deadline;
      // When ever a sensor move into a true state, all other sensors in a delayed true 
      // state are fast tracked to also reflect a true state
      this->_reset_delays_next_interval = input.pending_delayed_state().value_or(false) || this->_reset_delays_next_interval;
    }
  }

  inline void reset_delays_on_next_interval(uint32_t now) {
    this->_next_delay_expiry = now;
    this->_reset_delays_next_interval = true;
  }

  void on_input_state_change_(binary_sensor::BinarySensor *sensor, bool state);

  void update_all_detection_counts_(uint32_t now);


  void reset_detection_counts_() {
    this->presence_count = 0;
    this->movement_count = 0;
    this->movement_towards_count = 0;
    this->movement_away_count = 0;
  }

  void increment_detection_count_(SensorType sensor_type, int8_t sum = 1) {
    int8_t* count_ptr = nullptr;
    switch (sensor_type) {
      case SENSOR_TYPE__PRESENCE:
        count_ptr = &presence_count;  
        break;
       case SENSOR_TYPE__STILL:
        count_ptr = &presence_count;
        break;
      case SENSOR_TYPE__MOVEMENT:
        count_ptr = &movement_count;
        break;
      case SENSOR_TYPE__MOVEMENT_TOWARDS:
        count_ptr = &movement_towards_count;
        break;
      case SENSOR_TYPE__MOVEMENT_AWAY:
        count_ptr = &movement_away_count;
        break;
    }

    if (count_ptr == nullptr) {
      return;
    }
    
    *count_ptr += sum;
    
    if (*count_ptr < 0) {
      *count_ptr = 0;
    }
  }

  void publish_detection_states_changes() {

    if(published_detection_status != this->has_detection()) {
      published_detection_status = !published_detection_status;
      this->publish_update_state(presence_sensor_, published_detection_status);
    }

    if(published_movement_status != this->movement_detected()) {
      published_movement_status = !published_movement_status;
      this->publish_update_state(movement_sensor_, published_movement_status);
    }

    if(published_movement_towards_status != this->movement_towards_detected()) {
      published_movement_towards_status = !published_movement_towards_status;
      this->publish_update_state(movement_towards_sensor_, published_movement_towards_status);
    }

    if(published_movement_away_status != this->movement_away_detected()) {
      published_movement_away_status = !published_movement_away_status;
      this->publish_update_state(movement_away_sensor_, published_movement_away_status);
    }
  }

  void check_calibration_(uint32_t now);

  void publish_update_state(binary_sensor::BinarySensor *sensor, bool val) {
    if(!sensor) {
      return;
    }

    if(!sensor->has_state()) {
        sensor->publish_initial_state(val);
    } else {
      sensor->publish_state(val);
    }
  }
};

class CalibrationTrigger : public Trigger<> {
  public:
   explicit CalibrationTrigger(InputCalibrationConfig *parent) {
     parent->add_on_calibration_callback([this]() {
       this->trigger();
     });
   }
};

class SensorStateChangeTrigger : public Trigger<const std::string&, bool> {
  public:
   explicit SensorStateChangeTrigger(SensorAggregationComponent *parent) {
     parent->add_on_state_callback([this](const std::string &tag, bool state) {
       this->trigger(tag, state);
     });
   }
 };

}  // namespace sensor_aggregation
}  // namespace esphome
