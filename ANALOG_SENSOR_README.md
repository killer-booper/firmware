# Analog Sensor Support for Meshtastic

This fork adds support for generic analog sensors on ESP32-based Meshtastic devices, enabling water quality monitoring (TDS), soil moisture sensing, and other analog measurements.

## 🎯 Features

- **Generic Analog Sensor Framework**: Supports multiple sensor types with different calculation methods
- **TDS Water Quality Monitoring**: Cubic polynomial formula from manufacturer with temperature compensation
- **Soil Moisture Sensing**: Inverse linear relationship (high voltage = dry, low voltage = wet)
- **Custom Linear Sensors**: Easy configuration for any analog sensor with linear output
- **Full Calibration Support**: Adjustable ADC calibration, offset, and multiplier values
- **App Integration**: Data logs and graphs in Meshtastic apps via environment telemetry

## 🔧 Hardware

**Tested On:**
- Heltec WiFi LoRa 32 V3 (ESP32-S3)
- DFRobot Gravity TDS Sensor

**Supported Boards:**
- Any ESP32-based Meshtastic device with available ADC pins

**GPIO Pins (Heltec V3):**
- **Recommended:** GPIO7 (ADC1_CH6, board label "18") - Most stable
- **Also Available:** GPIO1, 2, 4, 5, 6 (see code for ADC channel mapping)
- **Avoid:** GPIO6 may have stability issues

## 📋 Supported Sensor Types

### 1. TDS (Total Dissolved Solids) Sensor
- **Use Case:** Water quality monitoring (aquariums, hydroponics, pools)
- **Output:** PPM (Parts Per Million)
- **Calculation:** Cubic polynomial with temperature compensation
- **Example:** DFRobot Gravity Analog TDS Sensor

### 2. Soil Moisture Sensor
- **Use Case:** Plant watering, agriculture monitoring
- **Output:** Percentage (0-100%)
- **Calculation:** Inverse linear (high voltage = dry)
- **Example:** Capacitive or resistive soil moisture sensors

### 3. Generic Linear Sensor
- **Use Case:** Any analog sensor with linear voltage output
- **Output:** Configurable units
- **Calculation:** Simple linear (voltage × slope + offset)
- **Examples:** pH sensors, light sensors, pressure sensors

## 🚀 Installation

### Quick Start

1. **Clone this fork:**
```bash
   git clone https://github.com/YOUR_USERNAME/firmware.git
   cd firmware
   git checkout analog-sensor-support
```

2. **Build for your board:**
```bash
   pio run -e heltec-v3
```

3. **Flash:**
```bash
   pio run -e heltec-v3 -t upload
```

### Hardware Setup

**TDS Sensor Connection (Heltec V3):**
```
TDS Sensor VCC  →  3.3V (Heltec)
TDS Sensor GND  →  GND (Heltec)
TDS Sensor SIG  →  GPIO7 / Pin 18 (Heltec)
```

⚠️ **Important:** Ensure your sensor outputs 0-3.3V. If it outputs 5V, use a voltage divider!

### Configuration

**Enable in Web UI:**
1. Navigate to: **Module Config → Telemetry**
2. Enable **Environment Measurement**
3. Set **Environment Update Interval** (recommended: 60 seconds)

**Default Sensor:** TDS sensor on GPIO7 (25°C temperature)

## 🎛️ Calibration

### ADC Voltage Calibration

The ESP32 ADC is often inaccurate. Calibrate with a multimeter:

1. Measure actual voltage with DMM between GPIO pin and GND
2. Note the voltage reported in serial logs (uncalibrated)
3. Calculate: `voltageCalibration = DMM_reading / uncalibrated_reading`
4. Update in `AnalogSensor.cpp`:
```cpp
   .voltageCalibration = 1.08,  // Your calculated value
```

**Example:**
- DMM reads: 0.33V
- ESP32 reports: 0.307V (uncalibrated)
- Calibration: 0.33 / 0.307 = 1.08

### TDS Sensor Calibration

1. **Zero Offset** (reading in air):
   - Leave sensor in air
   - Note the TDS reading
   - Set `outputOffset` to this value
```cpp
   .outputOffset = 49.0,  // Your air reading
```

2. **Multiplier** (using calibrated TDS meter):
   - Measure TDS with calibrated external meter
   - Check serial logs for Term1, Term2, Term3 values
   - Calculate: `cubicMultiplier = expected_TDS / (Term1 + Term2 + Term3)`
```cpp
   .cubicMultiplier = 0.5538,  // Your calculated value
```

**Note:** Term2 is NEGATIVE (-255.86) - this is correct per manufacturer formula!

## 📊 Data Fields

The sensor data appears in the Meshtastic app as:

| App Display | Actual Data | Notes |
|-------------|-------------|-------|
| Temperature | TDS/Sensor Value | Required for logging |
| Humidity | TDS/Sensor Value | Required for logging |
| Voltage | Raw ADC Voltage | Diagnostic |
| Current | TDS/Sensor Value | May not log |

⚠️ **Critical:** Temperature and/or Humidity fields MUST be enabled for the app to store history. Without these, no environment data will be logged!

## 📁 Files Modified/Created

**New Files:**
- `src/modules/Telemetry/Sensor/AnalogSensor.h` - Header file
- `src/modules/Telemetry/Sensor/AnalogSensor.cpp` - Implementation

**Modified Files:**
- `src/modules/Telemetry/EnvironmentTelemetry.cpp` - Register analog sensor

## 🔌 Adding Custom Sensors

### Example: Custom pH Sensor

In `EnvironmentTelemetry.cpp`:
```cpp
#if defined(ARCH_ESP32)
    // pH sensor: 0-14 pH range, 0V = pH 0, 3.3V = pH 14
    sensors.push_front(AnalogSensor::createLinearSensor(
        7,              // GPIO pin
        4.242,          // Slope: 14 pH / 3.3V = 4.242
        0.0,            // Offset
        "pH"            // Name
    ));
#endif
```

### Example: Soil Moisture Sensor
```cpp
#if defined(ARCH_ESP32)
    sensors.push_front(AnalogSensor::createSoilMoistureSensor(7));
#endif
```

## 🐛 Troubleshooting

**No readings / all zeros:**
- Check GPIO pin connection
- Verify sensor is powered (3.3V)
- Check `initDevice()` logs for initialization errors

**Readings unstable:**
- GPIO6 may be unstable - use GPIO7 instead
- Increase `NUM_SAMPLES` in AnalogSensor.cpp (default: 10)
- Check for electrical noise / bad connections

**No app logging / graphs:**
- Ensure Temperature or Humidity fields are enabled
- Check "Environment Measurement" is ON in web UI
- Verify telemetry interval is reasonable (30-300 seconds)

**Voltage reads wrong:**
- Calibrate ADC with multimeter (see Calibration section)
- Check sensor output is truly 0-3.3V (not 5V!)

## 📝 Technical Details

**ADC Configuration:**
- Resolution: 12-bit (ESP32-S3) or 9-12 bit (ESP32 classic)
- Reference: 3.3V
- Attenuation: 11dB (full 0-3.3V range)
- Samples: 10 readings averaged per measurement

**TDS Calculation Formula:**
```
compensationCoefficient = 1.0 + 0.02 × (temperature - 25.0)
compensationVoltage = voltage / compensationCoefficient
TDS = (133.42×V³ - 255.86×V² + 857.39×V) × multiplier
```

## 🤝 Contributing

This is a fork for testing and community use. To contribute:

1. Test with your hardware
2. Report issues on this fork's GitHub Issues
3. Submit pull requests with improvements

## 📜 License

Same as Meshtastic Firmware: GPL-3.0

## 🙏 Acknowledgments

- Meshtastic team for the excellent firmware platform
- DFRobot for TDS sensor documentation and formula
- Community testers and contributors

## 📞 Support

- **Issues:** GitHub Issues on this repository
- **Meshtastic Discord:** #custom-hardware channel
- **Documentation:** See comments in AnalogSensor.cpp for detailed calibration

---

**Version:** 1.0.0  
**Last Updated:** 2026-02-06  
**Tested Firmware Base:** Meshtastic 2.5.x
