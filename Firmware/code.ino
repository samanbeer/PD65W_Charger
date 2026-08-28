#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <INA226_WE.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
#define OLED_RESET          -1

#define I2C_DISPLAY_ADDRESS 0x3C //change these if is is not correct for your display
#define I2C_INA_ADDRESS     0x40 //change these if is is not correct for your module

#define NTC1_PIN            A0
#define NTC2_PIN            A1
#define Vout_PIN            A2
#define FAN_PIN             D10

#define R_FIXED             10000.0f
#define NTC_NOMINAL         10000.0f
#define TEMP_NOMINAL        25.0f
#define B_COEFFICIENT       3950.0f
#define ADC_MAX             1023.0f
#define V_REF               3.3f

#define DIVIDER_R1          100000.0f // 100k
#define DIVIDER_R2          10000.0f  // 10k

#define SHUNT_RESISTANCE    0.010f    // 10mOhm (R5)
#define SHUNT_MAX_CURRENT   5.0f      // Max 5A

#define TEMP_FAN_OFF        35
#define TEMP_FAN_MAX        60

#define SENSOR_INTERVAL_MS  100
#define DISPLAY_INTERVAL_MS 250
#define EFFICIENCY_FACTOR   0.92f // module Efficiency 

int fan_speed_pwm;

INA226_WE ina226(I2C_INA_ADDRESS);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int temp_1;
int temp_2;

int input_voltage;
int output_voltage;
int input_amp;
int output_amp;

unsigned long last_sensor_time = 0;
unsigned long last_display_time = 0;

void measure_temperatures();
void measure_output_voltage();
void measure_ina226();
void calculate_output_current();
void control_fan();
void update_display();

void setup() {
  analogReadResolution(10);
  pinMode(FAN_PIN, OUTPUT);
  analogWrite(FAN_PIN, 0);

  Wire.begin();
  ina226.init();
  ina226.setResistorRange(SHUNT_RESISTANCE, SHUNT_MAX_CURRENT);

  display.begin(SSD1306_SWITCHCAPVCC, I2C_DISPLAY_ADDRESS);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  unsigned long current_time = millis();

  if (current_time - last_sensor_time >= SENSOR_INTERVAL_MS) {
    last_sensor_time = current_time;
    measure_temperatures();
    measure_output_voltage();
    measure_ina226();
    calculate_output_current();
    control_fan();
  }

  if (current_time - last_display_time >= DISPLAY_INTERVAL_MS) {
    last_display_time = current_time;
    update_display();
  }
}

void measure_temperatures() {
  const auto calculate_temp = [](int pin) -> int {
    int raw = analogRead(pin);
    if (raw <= 0 || raw >= (int)ADC_MAX) return 0;
    
    float resistance = R_FIXED * (ADC_MAX / (float)raw - 1.0f);
    float steinhart = log(resistance / NTC_NOMINAL) / B_COEFFICIENT + 1.0f / (TEMP_NOMINAL + 273.15f);
    return (int)(1.0f / steinhart - 273.15f);
  };

  temp_1 = calculate_temp(NTC1_PIN);
  temp_2 = calculate_temp(NTC2_PIN);
}

void measure_output_voltage() {
  int raw = analogRead(Vout_PIN);
  float v_adc = (raw / ADC_MAX) * V_REF;
  output_voltage = (int)(v_adc * ((DIVIDER_R1 + DIVIDER_R2) / DIVIDER_R2) * 1000.0f);
}

void measure_ina226() {
  input_voltage = (int)(ina226.getBusVoltage_V() * 1000.0f);
  input_amp = (int)ina226.getCurrent_mA();
}

void calculate_output_current() {
  if (output_voltage > 1000) {
    float pin_mw = (float)input_voltage * (float)input_amp;
    float pout_mw = pin_mw * EFFICIENCY_FACTOR;
    output_amp = (int)(pout_mw / (float)output_voltage);
  } else {
    output_amp = 0;
  }
}

void control_fan() {
  int max_temp = max(temp_1, temp_2); //takes the higher temperature from the two thermistors so it is controled by the hottest one

  if (max_temp < TEMP_FAN_OFF) {
    fan_speed_pwm = 0; //if temperature is below TEMP_FAN_OFF (35°C) the fan is off
  } else if (max_temp >= TEMP_FAN_MAX) {
    fan_speed_pwm = 255; // ir if the temperature is more then TEMP_FAN_MAX (60°C) the fan is at maximum speed
  } else {
    fan_speed_pwm = map(max_temp, TEMP_FAN_OFF, TEMP_FAN_MAX, 70, 255); //if the temprature is between TEMP_FAN_OFF and TEMP_FAN_MAX the fan speed is base on current temperature
  }

  analogWrite(FAN_PIN, fan_speed_pwm);
}

void update_display() {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("IN : ");
  display.print(input_voltage / 1000.0f, 2); // measured voltage and amps previosly were in mV and mA, so it need to be divided by 1000 
  display.print("V ");
  display.print(input_amp / 1000.0f, 2);
  display.print("A");

  display.setCursor(0, 16);
  display.print("OUT: ");
  display.print(output_voltage / 1000.0f, 2);
  display.print("V ");
  display.print(output_amp / 1000.0f, 2);
  display.print("A");

  display.setCursor(0, 32);
  display.print("PWR: ");
  display.print((input_voltage / 1000.0f) * (input_amp / 1000.0f), 1); // V*A = W
  display.print("W");

  display.setCursor(0, 48);
  display.print("T1:");
  display.print(temp_1);
  display.print("C T2:");
  display.print(temp_2);
  display.print("C FAN:");
  display.print(map(fan_speed_pwm, 0, 255, 0, 100));
  display.print("%");

  display.display();
}