# ESPHome Sensor Aggregation Component

This component allows you to aggregate multiple presence and movement sensors into combined outputs. It's particularly useful when you have multiple sensors covering different areas or using different detection technologies, and you want to combine their outputs into unified presence/movement states.

## Features

- Aggregates multiple input sensors into combined outputs
- Supports different types of detection:
  - Presence (occupancy)
  - Movement
  - Movement towards
  - Movement away
- Configurable detection and release delays (both globally and per-sensor)
- Flexible input configuration - any binary sensor can be used as input
- Multiple output types with appropriate device classes for Home Assistant


## Configuration

### Basic Configuration

```yaml
# Example configuration
sensor_aggregation:
  id: my_aggregator
  detect_delay: 2s    # Optional: Global detection delay
  release_delay: 30s  # Optional: Global release delay
  inputs:
    - id: pir_sensor_1
      sensor_type: movement
      detect_delay: 1s    # Optional: Override global delay
      release_delay: 20s  # Optional: Override global delay
      
    - id: mmwave_presence
      sensor_type: presence
```

### Configuration Variables

#### Main Component
- **id** (*Required*, ID): Unique identifier for the component
- **detect_delay** (*Optional*, Time): Global delay before detecting presence/movement
- **release_delay** (*Optional*, Time): Global delay before clearing presence/movement
- **inputs** (*Required*, List): List of input sensor configurations

#### Input Configuration
- **id** (*Required*, ID): ID of the input sensor
- **sensor_type** (*Required*, Enum): Type of detection this sensor provides
  - `presence`: Stationary presence/occupancy detection
  - `movement`: General movement detection
  - `movement_towards`: Movement approaching the sensor
  - `movement_away`: Movement away from the sensor
- **detect_delay** (*Optional*, Time): Detection delay for this specific sensor
- **release_delay** (*Optional*, Time): Release delay for this specific sensor

### Output Configuration

```yaml
binary_sensor:
  - platform: sensor_aggregation
    presence:
      name: "Room Occupied"
      device_class: occupancy
      
    movement:
      name: "Room Movement"
      device_class: motion
      
    movement_towards:
      name: "Movement Towards"
      device_class: motion
      
    movement_away:
      name: "Movement Away"
      device_class: motion
```

#### Output Types
- **presence**: Combined presence/occupancy state
- **movement**: Combined movement detection
- **movement_towards**: Combined movement towards detection
- **movement_away**: Combined movement away detection

## Example Use Cases

1. **Room Occupancy**: Combine PIR and mmWave sensors for reliable presence detection
```yaml
sensor_aggregation:
  id: room_occupancy
  inputs:
    - id: pir_sensor
      sensor_type: movement
    - id: mmwave_sensor
      sensor_type: presence
```

2. **Directional Movement**: Track movement direction using multiple sensors
```yaml
sensor_aggregation:
  id: hallway_movement
  inputs:
    - id: radar_towards
      sensor_type: movement_towards
    - id: radar_away
      sensor_type: movement_away
```

3. **Multi-Zone Coverage**: Combine multiple sensors for large spaces
```yaml
sensor_aggregation:
  id: living_room
  detect_delay: 1s
  release_delay: 30s
  inputs:
    - id: pir_corner1
      sensor_type: movement
    - id: pir_corner2
      sensor_type: movement
    - id: mmwave_center
      sensor_type: presence
```

## How It Works

The component processes each input sensor based on its configured type and delays. The outputs are updated as follows:

- **Presence**: TRUE if ANY presence sensor detects presence
- **Movement**: TRUE if ANY movement sensor detects movement
- **Movement Towards/Away**: TRUE if ANY corresponding directional sensor detects movement

Detection and release delays help prevent false positives and ensure smooth operation:
- **Detection Delay**: Time to wait before registering a positive detection
- **Release Delay**: Time to wait before clearing a detection

## Troubleshooting

1. **False Positives**: Increase detection delay or adjust sensor sensitivity
2. **Slow Response**: Decrease detection delay or check sensor update intervals
3. **Stuck States**: Ensure release delays aren't too long
4. **No Updates**: Verify sensor IDs and connections
