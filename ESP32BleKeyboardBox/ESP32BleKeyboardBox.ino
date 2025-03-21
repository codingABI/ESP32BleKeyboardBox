
#include <rtc_wdt.h>
#define RTCWDTTIMEOUTMS 60000 // WDT timeout

// I use a modified BleKeyboard.cpp, see https://github.com/T-vK/ESP32-BLE-Keyboard/issues/312
#include <BleKeyboard.h>
BleKeyboard g_bleKeyboard;

#include <FastLED.h>
#define NUM_LEDS 3
#define LEDDATA_PIN 23
CRGB g_leds[NUM_LEDS]; // Array of leds
CRGB g_lastLeds[NUM_LEDS]; // Last value for leds
int g_vBatmV; // Current battery voltage

// My IR remote control is a Sony RM-ED011
#define DECODE_SONY
#include <IRremote.hpp> 
#define IRRX_PIN 4
#define IRDEADTIMEMS 200

// Momentary switch buttons
#define SW1_PIN 17
#define SW2_PIN 16
#define SWITCHDEBOUCETIMEMS 200

// Buzzer pin
#define BUZZER_PIN 5

// Analog input pin to measure the battery voltage
#define VBAT_PIN 33

int g_lastBatteryLevel = -1;

enum beepTypes // Beep types 
{ 
  DEFAULTBEEP, 
  SHORTBEEP, 
  LONGBEEP, 
  HIGHSHORTBEEP, 
  LASER 
};
 
// Beep with passive buzzer
void beep(int type=DEFAULTBEEP) 
{
  /* I had problems with ledc (At least with arduino-esp32 3.0.5 to 3.1.1):
   * Sometimes there was not output on BUZZER_PIN (ledcAttach and ledcWrite returns true)
   * A workaround seems to be to put ledcAttach and ledcWrite between
   * portENTER_CRITICAL() and portEXIT_CRITICAL()
   */
  portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;

  switch(type) 
  {
    case DEFAULTBEEP: // 500 Hz for 200ms
      portENTER_CRITICAL(&mutex);
      ledcAttach(BUZZER_PIN,500,8);
      ledcWrite(BUZZER_PIN, 128);
      portEXIT_CRITICAL(&mutex);
      delay(200);
      ledcWrite(BUZZER_PIN, 0);
      ledcDetach(BUZZER_PIN);
      break;
    case SHORTBEEP: // 1 kHz for 100ms
    {
      portENTER_CRITICAL(&mutex);
      ledcAttach(BUZZER_PIN,1000,8);
      ledcWrite(BUZZER_PIN, 128);
      portEXIT_CRITICAL(&mutex);
      delay(100);
      ledcWrite(BUZZER_PIN, 0);
      ledcDetach(BUZZER_PIN);
      break;
    }
    case LONGBEEP: // 250 Hz for 400ms
      portENTER_CRITICAL(&mutex);
      ledcAttach(BUZZER_PIN,250,8);
      ledcWrite(BUZZER_PIN, 128);
      portEXIT_CRITICAL(&mutex);
      delay(400);
      ledcWrite(BUZZER_PIN, 0);
      ledcDetach(BUZZER_PIN);
      break;
    case HIGHSHORTBEEP: // High and short beep
    {
      portENTER_CRITICAL(&mutex);
      ledcAttach(BUZZER_PIN,5000,8);
      ledcWrite(BUZZER_PIN, 128);
      portEXIT_CRITICAL(&mutex);
      delay(100);
      ledcWrite(BUZZER_PIN, 0);
      ledcDetach(BUZZER_PIN);
      break;
    }
    case LASER: { // Laser like sound
      int i = 5000; // Start frequency in Hz (goes down to 300 Hz)
      int j = 300; // Start duration in microseconds (goes up to 5000 microseconds)
      ledcAttach(BUZZER_PIN,i,8);
      while (i>300) {
        i -=50;
        j +=50;
        ledcWriteTone(BUZZER_PIN,i);
        delayMicroseconds(j+1000);
      }
      ledcDetach(BUZZER_PIN);
      break;
    }
  }
  delay(100);
}

// Checks low battery
bool isLowBattery()
{
  return (g_vBatmV < 3000);
}

// Show LED
void showLED(bool lowBattery, bool connected)
{
  #define CURRENTLED 2

  if (connected) g_leds[CURRENTLED] = CRGB::Blue; else g_leds[CURRENTLED] = CRGB::Green;
  if (lowBattery && ((millis() / 1000) % 2 == 0)) g_leds[CURRENTLED] = CRGB::Red;

  // Update LEDs only when at least one color has changed
  bool ledChanged = false;
  for (int i = 0;i < NUM_LEDS;i++)
  {
    if (g_lastLeds[i] != g_leds[i]) 
    {
      ledChanged = true;
      g_lastLeds[i] = g_leds[i];
    }
  }
  if (ledChanged) FastLED.show();  
}

// Read average voltage for battery, when powered by battery (When powered by USB this is the battery loader output voltage)
void updateVbat() {
  #define MAXSAMPLES 10
  static int samples[MAXSAMPLES];
  static int currentSample = -1;
  long sum = 0;
  int count = 0;
  static unsigned long lastReadMS = 0;

  if ((lastReadMS == 0) || (millis()-lastReadMS > 500)) {
    if (currentSample == -1) {
      for (int i=0;i<MAXSAMPLES;i++) samples[i] = -1;
      currentSample = 0;
    }
    samples[currentSample] = analogRead(VBAT_PIN);
    currentSample++;
    if (currentSample >= MAXSAMPLES) currentSample = 0;

    for (int i=0;i<MAXSAMPLES;i++) {
      if (samples[i] != -1) {
        sum += samples[i];
        count++;
      }
    }
    g_vBatmV = map(round((float) sum/count),2704, 2835, 3290, 3440);
    lastReadMS = millis();
  }
}

// Get battery level in percent
byte getBatteryLevel()
{
  // Approx. to "Technical Report of INR18650-35E...INR18650-35E Standard Discharge profile"
  #define LEVELSTEPS 10
  const int approxValues[LEVELSTEPS+1] = {
    4150, // 100%
    4000, // 90%
    3925, // 80%
    3800, // 70%
    3775, // 60%
    3650, // 50%
    3600, // 40%
    3500, // 30%
    3400, // 20%
    3250, // 10%
    2950 // 0%
  };

  int batteryLevel = 0;
  for (int i=0; i < LEVELSTEPS;i++) 
  {
    if (g_vBatmV > approxValues[i+1]) {
      batteryLevel = map(g_vBatmV, approxValues[i+1], approxValues[i], (LEVELSTEPS-i-1)*(1000/LEVELSTEPS), (LEVELSTEPS-i)*(1000/LEVELSTEPS));
      break;
    }
  }
  batteryLevel = (int) round(batteryLevel/10.0f);
  if (batteryLevel < 0) batteryLevel = 0;
  if (batteryLevel > 100) batteryLevel = 100;
  return batteryLevel;
}

void setup()
{
  // RTC WTD
  rtc_wdt_protect_off(); // Disable RTC WDT write protection
  // Set stage 0 to trigger a system reset after timeout
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
  rtc_wdt_set_time(RTC_WDT_STAGE0, RTCWDTTIMEOUTMS); // Timeout in milliseconds
  rtc_wdt_enable(); // Enable RTC WDT timer
  rtc_wdt_protect_on(); // Enable RTC WDT write protection

  // Reduce clock to save power
  setCpuFrequencyMhz(80);

  // Get battery voltage
  updateVbat();

  // WS2812B
  FastLED.addLeds<WS2812B, LEDDATA_PIN, GRB>(g_leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(3.3,100); // Limit current
  FastLED.setBrightness(255);
  FastLED.clear();
  for (int i = 0;i < NUM_LEDS;i++) g_lastLeds[i] = g_leds[i];
  FastLED.show();
  showLED(isLowBattery(),false);

  // Startup sound
  beep(LASER);

  // Serial init
  Serial.begin(115200);
  
  // Pin modes
  pinMode(SW1_PIN,INPUT_PULLUP);
  pinMode(VBAT_PIN,INPUT);

  // IR init
  IrReceiver.begin(IRRX_PIN);

  // BLE
  Serial.println("Starting BLE work!");
  g_bleKeyboard.setName("JuliaD26");
  g_bleKeyboard.begin();
}

void loop()
{
  static unsigned long lastSwitchMS = 0; 
  static unsigned long lastIRSignalMS = 0;
  static bool isBLEConnected = false;

  updateVbat();

  // Check BLE connection change
  if (g_bleKeyboard.isConnected() != isBLEConnected) 
  {
    isBLEConnected = g_bleKeyboard.isConnected();
    Serial.print("BLE changed: ");
    if (isBLEConnected) // New connection 
    {
      Serial.println("connected");
      beep(HIGHSHORTBEEP); 
    } else { // Disconneced
      Serial.println("disconnected");
      beep();
    }
  }
  showLED(isLowBattery(),g_bleKeyboard.isConnected());

  // Check IR signals
  if (IrReceiver.decode()) 
  {
    IrReceiver.resume(); // Enable receiving of the next value
    if (millis()-lastIRSignalMS > IRDEADTIMEMS) // Debounce
    {
      word received = (IrReceiver.decodedIRData.address << 8) + IrReceiver.decodedIRData.command;
      switch (received) 
      {
        case 0x0112: // >> Volume up
          Serial.println("IR volume up received");
          g_bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
          break;
        case 0x0113: // >> Volume down
          Serial.println("IR volume down received");
          g_bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
          break;
        default:
          Serial.print("Unknown IR received 0x");
          Serial.println(received,HEX);      
      }
      lastIRSignalMS = millis();
    }
  }  

  // Check momentary switch buttons
  if(g_bleKeyboard.isConnected()) 
  {
    if (digitalRead(SW1_PIN)==LOW) {
      if (millis()-lastSwitchMS > SWITCHDEBOUCETIMEMS) { // Debounce
        Serial.println("Print screen button pressed");
        g_bleKeyboard.write(KEY_PRTSC);
        lastSwitchMS = millis();
      }
    }
  }

  // Send level when changed at least 5% (prevents unnecessary BLE notifications)
  byte currentBatteryLevel = getBatteryLevel();
  if (abs(g_lastBatteryLevel - currentBatteryLevel)>5) {
    g_bleKeyboard.setBatteryLevel(currentBatteryLevel/5*5); // Level in 5% steps
    g_lastBatteryLevel = currentBatteryLevel;
  }

  rtc_wdt_feed(); // Reset WDT  
}
