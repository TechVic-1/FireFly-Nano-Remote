#include "remote.h"
#include <math.h>
//#include "button.cpp"
//#include"try.cpp"
Adafruit_SSD1306 display(DISPLAY_RST);
Smoothed <double> batterySensor;
Smoothed <float> smoothedThrottle;

//#include <pthread.h>

//TaskHandle_t TaskHandle1;
// Adafruit_SSD1306 display(64, 128, &Wire, DISPLAY_RST, 700000, 700000);

// -------useful things--------
//#include <array>
#define ARRAYLEN(ar) (sizeof(ar) / sizeof(ar[0]))

/************ Radio Setup ***************/
#define drawVLine(x, y, l) display.drawLine (x, y, x, y + l, WHITE); // display.drawVerticalLine (x, y, l);
#define drawHLine(x, y, l)  display.drawLine (x, y, x + l, y, WHITE);
#define drawBox(x, y, w, h) display.fillRect(x, y, w, h, WHITE);
#define drawFrame(x, y, w, h) display.drawRect(x, y, w, h, WHITE);
#define drawPixel(x, y)  display.drawPixel (x, y, WHITE);
#define drawStr(x, y, s) display.drawString(x, y, s);

// Feather M0 w/Radio
#ifdef ARDUINO_SAMD_ZERO // Feather M0 w/Radio

  #include <RH_RF69.h>
  #include <FlashStorage.h>

  // Singleton instance of the radio driver
  RH_RF69 radio(RF_CS, RF_DI0);

  FlashStorage(flash_settings, RemoteSettings);

#elif ESP32
  Preferences preferences; // https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences
#endif

#include "radio.h"

#ifdef ESP32
static void rtc_isr(void* arg){
    uint32_t status = REG_READ(RTC_CNTL_INT_ST_REG);
    if (status & RTC_CNTL_BROWN_OUT_INT_ENA_M) {

      digitalWrite(PIN_LED, HIGH);
      REG_WRITE(RTC_CNTL_BROWN_OUT_REG, 0);

    }
    REG_WRITE(RTC_CNTL_INT_CLR_REG, status);
}

#define BROWNOUT_DET_LVL 0

void brownoutInit() {

  // enable brownout detector
  REG_WRITE(RTC_CNTL_BROWN_OUT_REG,
          RTC_CNTL_BROWN_OUT_ENA /* Enable BOD */
          | RTC_CNTL_BROWN_OUT_PD_RF_ENA /* Automatically power down RF */
          /* Reset timeout must be set to >1 even if BOR feature is not used */
          | (2 << RTC_CNTL_BROWN_OUT_RST_WAIT_S)
          | (BROWNOUT_DET_LVL << RTC_CNTL_DBROWN_OUT_THRES_S));

  // install ISR
  REG_WRITE(RTC_CNTL_INT_ENA_REG, 0);
  REG_WRITE(RTC_CNTL_INT_CLR_REG, UINT32_MAX);
  esp_err_t err = esp_intr_alloc(ETS_RTC_CORE_INTR_SOURCE, 0, &rtc_isr, NULL, &s_rtc_isr_handle);
  REG_SET_BIT(RTC_CNTL_INT_ENA_REG, RTC_CNTL_BROWN_OUT_INT_ENA_M);
}
#endif

void setup() {

    startupTime = millis();

    #ifdef DEBUG
        Serial.begin(115200);
    #endif

    // while (!Serial) { ; }

    loadSettings();

    #ifdef PIN_VIBRO
        pinMode(PIN_VIBRO, OUTPUT);
        digitalWrite(PIN_VIBRO, LOW);
    #endif
    #ifdef PIN_BATTERY
        pinMode(PIN_BATTERY, INPUT);
        #ifdef ESP32
            // enable battery probe
            pinMode(VEXT, OUTPUT);
            digitalWrite(VEXT, LOW);
            adcAttachPin(PIN_BATTERY);
            // analogSetClockDiv(255);
        #endif
    #endif

    pinMode(PIN_TRIGGER, INPUT_PULLUP);
    pinMode(PIN_PWRBUTTON, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);

    digitalWrite(PIN_LED, LOW);

    #ifdef ARDUINO_SAMD_ZERO // Feather M0 w/Radio
        initRadio(radio);
        // config throttle
        pinMode(PIN_THROTTLE, INPUT);
    #elif ESP32
        brownoutInit(); // avoid low voltage boot loop
        initRadio();
        // config throttle
        adc1_config_width(ADC_WIDTH_BIT_10);
        adc1_config_channel_atten(ADC_THROTTLE, ADC_ATTEN_DB_2_5);
    #endif

    // 10 values average
    batterySensor.begin(SMOOTHED_AVERAGE, 10);
    smoothedThrottle.begin(SMOOTHED_AVERAGE, 3);

    #ifdef ESP32
        xTaskCreatePinnedToCore(
            coreTask,   /* Function to implement the task */
                "coreTask", /* Name of the task */
                10000,      /* Stack size in words */
                NULL,       /* Task input parameter */
                configMAX_PRIORITIES - 1,  /* Priority of the task. 0 is slower */
                NULL,       /* Task handle. */
                0);  /* Core where the task should run */

        xTaskCreatePinnedToCore(
            vibeTask,   /* Function to implement the task */
                "vibeTask", /* Name of the task */
                1000,      /* Stack size in words */
                NULL, //(void*)&vibeMode,       /* Task input parameter */
                0, //configMAX_PRIORITIES - 2,  /* Priority of the task. 0 is slower */
                NULL,       /* Task handle. */
                1);  /* Core where the task should run */

    #endif
}

#ifdef ESP32
    void coreTask(void * pvParameters){ // core 0
        while (1) {
            radioLoop();
            vTaskDelay(1);
            //vTaskDelay(10 / portTICK_PERIOD_MS); //delay specified in milliseconds instead of ticks
            // -- ----- ---------- --
        }
    }

    void vibeTask(void * pvParameters){ // core 1 : low priority task
        //int myMode = *((int*)pvParameters;
        while(1){
            if (vibeMode != 0){
                if (vibeMode == 1){vibrate(50); delay(25); vibrate(50); delay(25); vibrate(50); delay(25); vibrate(50);}
                if (vibeMode == 2){vibrate(100); delay(50); vibrate(100);}
                if (vibeMode == 3){vibrate(80);}
                if (vibeMode == 4){vibrate(50); delay(25); vibrate(50); delay(25); vibrate(50);}
                if (vibeMode == 5){vibrate(50);}
                if (vibeMode == 6){vibrate(100);}
                if (vibeMode == 7){vibrate(200);}
                if (vibeMode > 7){vibrate(vibeMode);}
                vibeMode = 0;
            }
            vTaskDelay(50);
            #ifdef DEBUG
                debugTaskSize = uxTaskGetStackHighWaterMark(NULL);
            #endif
        }
        //vTaskDelete( NULL );
    }


#endif

/*
pthread_t threads[4];
int pthread_param;

void * vibe_pThread(void * duration) {
    if (vibeMode != 0){
        if (vibeMode == 1){vibrate(45); delay(5); vibrate(15); delay(5); vibrate(15);}
        if (vibeMode == 2){vibrate(40); delay(8); vibrate(16);}
        if (vibeMode == 3){vibrate(35); delay(6); vibrate(15); delay(7); vibrate(15);}
        if (vibeMode == 4){vibrate(50); delay(25); vibrate(50); delay(25); vibrate(50);}
        vibeMode=0;
    }
   //vibrate((int)duration);
}
*/
//int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
//pthread_create(&threads_1, NULL, vibe_pThread, (void *)param);



void loop() { // core 1
    #ifdef ARDUINO_SAMD_ZERO
        radioLoop();
    #endif
    checkBatteryLevel();
    handleButtons();
    if (displayOn) updateMainDisplay();     // Call function to update display
    vTaskDelay(1);    // free time for other tasks

    if (retrieveAllOptParamFromReceiverAtStartup == true){
        retrieveAllOptParamFromReceiver();
    }
    //pthread_create(&threads[1], NULL, vibe_pThread);
}

void radioLoop() {  //Calculate THROTTLE and transmit a packet - every 50ms
  // Transmit once every 50 millisecond
  if (millisSince(lastTransmission) < REMOTE_RADIOLOOP_DELAY) return;

  // mark start
  lastTransmission = millis();

  calculateThrottle();
  transmitToReceiver();
}

void checkBatteryLevel() { //manages OLED display depending on remote battery Level and update smoothed battery value with batterySensor.add(voltage);

  batteryLevel = getBatteryLevel();

  if (batteryLevel >= DISPLAY_BATTERY_MIN) {
    if (!displayOn) {
      displayOn = true;
      // turn on
      display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
      display.setRotation(DISPLAY_ROTATION);
      display.powerOn();
    }
  } else { // battery low
    if (displayOn) {
      displayOn = false;
      // turn off to save power
      display.powerOff();
    }
  }
}

void keepAlive() {
  lastInteraction = millis();
}

void calculateThrottle() {

  int position = readThrottlePosition();

  switch (state) {

  case PAIRING:
  case CONNECTING:
    throttle = position; // show debug info
    break;

  case IDLE: //
    if (position < default_throttle) { // breaking
      throttle = position; // brakes always enabled
    } else { // throttle >= 0
      if (triggerActive()) {
        // dead man switch activated
        state = NORMAL;
        throttle = position;
        stopTime = millis();
        debug("dead man switch activated");
      } else {
        // locked, ignore
        throttle = default_throttle;
      }
    }
    // sleep timer
    if (stopped && secondsSince(lastInteraction) > REMOTE_SLEEP_TIMEOUT) sleep();
    break;

  case NORMAL:
    throttle = position;

    // activate cruise mode?
    if (triggerActive() && throttle == default_throttle && speed() > 3) {
      cruiseSpeed = speed();
      // cruiseThrottle = throttle;
      state = CRUISE;
    }

    // activate deadman switch
    if (stopped && throttle == default_throttle) { // idle
      if (secondsSince(stopTime) > REMOTE_LOCK_TIMEOUT) {
        // lock remote
        state = IDLE;
        debug("locked");
      }
    }
    break;

  case MENU: // navigate menu
    // idle
    throttle = default_throttle;

    if (position != default_throttle) {
      menuWasUsed = true;
    }
    break;

  case ENDLESS: break;

  case CRUISE:
    // exit mode if trigger released or throttle changed
    if (!triggerActive() || position != default_throttle) {
      state = NORMAL;
      throttle = position;
      debug("trigger released");
    }
    break;
  }

  // wheel was used
  if (position != default_throttle) keepAlive();
}

// int cruiseControl() {
//
//   if (speed() == 0) return throttle;
//
//   debug("cruise @" + String(cruiseSpeed));
//
//   // speed difference
//   float diff = cruiseSpeed - speed(); // 10 kmh - 5 kmh = 5 km/h
//
//   debug("speed: " + String(speed()) + ", diff: " + String(diff));
//
// }


void isr() { } // Interrupt Service Routine

//double handleButtonTimestamp = 0;
void handleButtons() { //executes action depending on PWR_BUTTON state ( CLICK - DBL_CLICK - HOLD - LONG_HOLD )

    //powerButton->update();
    //triggerButton->update();

    powerButton.update();
    triggerButton.update();

    switch (powerButton.getState()) { //checks what PWR_BUTTON is doing and return it's state ( CLICK - DBL_CLICK - HOLD - LONG_HOLD )

        case RELEASED:
        break;

        case CLICK:
            keepAlive();
            switch (state) { //state is an AppState() type - (Remote control state)
                case CONNECTING:
                    state = PAIRING; // switch to pairing
                break;

                case PAIRING:
                    state = CONNECTING; // switch to connecting
                break;

                default:
                    if (page == PAGE_MENU) { // in menu
                        //if (quitMainMenu == true){
                        //quitMainMenu = false;
                            if (menuPage != MENU_MAIN) {
                                display.setRotation(DISPLAY_ROTATION); // back to vertical
                                calibrationStage = CALIBRATE_CENTER;
                                return backToMainMenu();
                            }
                            // exit menu
                        //}
                        //else{quitMainMenu = true;}
                        state = menuWasUsed ? IDLE : NORMAL;
                    }

                // switch pages
                page = static_cast<ui_page>((page + 1) % PAGE_MAX);
            }
        break;

        case DBL_CLICK:
        break;

        case HOLD: // start shutdown
        break;

        case LONG_HOLD: // shutdown confirmed
            sleep();
        return;
    }

    switch (triggerButton.getState()) { //checks what PWR_BUTTON is doing and return it's state ( CLICK - DBL_CLICK - HOLD - LONG_HOLD )
        case RELEASED:
        break;
        case CLICK:
        break;
        case DBL_CLICK:
            //keepAlive();
            speedLimiter(!speedLimiterState);//switches the speed limiter on or off
            
            switch (state) { //state is an AppState() type - (Remote control state)
            //IDLE,       // remote is not connected
            //NORMAL,
            //PUSHING,
            //CRUISE,
            //ENDLESS,
            //CONNECTED,  // riding with connected remote
            //CONNECTING,
            //MENU,
            //STOPPING,   // emergency brake when remote has disconnected
            //STOPPED,
            //PAIRING,
            //UPDATE,     // update over WiFi
            //COASTING    // waiting for board to slowdown
                case CONNECTING:
                break;
                case PAIRING:
                break;
                default:
                break;
            }
        break;
        case HOLD: // start shutdown
        break;
        case LONG_HOLD: // shutdown confirmed
        return;
    }

}

void sleep() {  // manages the remote POWER ON / POWER OFF via PWR_BUTTON
    if (power == false) { return; }

    // turn off screen
    display.powerOff();
    digitalWrite(PIN_LED, LOW); //write on PIN_LED PIN

    power = false;

    #ifdef ARDUINO_SAMD_ZERO

        // interrupt
        attachInterrupt (digitalPinToInterrupt(PIN_PWRBUTTON), isr, LOW);  // attach interrupt handler

        radio.sleep();


        USBDevice.standby();

        delay(200);

        // Set sleep mode to deep sleep
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

        //Enter sleep mode and wait for interrupt (WFI)
        __DSB();
        __WFI();

    #elif ESP32

        // wait for button release ( vTaskDelay (ms) from ESP32 )
        while (pressed(PIN_PWRBUTTON)) vTaskDelay(10);

        // keep RTC on
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
        gpio_pullup_en((gpio_num_t)PIN_PWRBUTTON);
        gpio_pulldown_dis((gpio_num_t)PIN_PWRBUTTON);

        // wake up when the top button is pressed
        const uint64_t ext_wakeup_pin_1_mask = 1ULL << PIN_PWRBUTTON;
        esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);

        // turn off radio
        LoRa.end();
        LoRa.sleep();
        delay(100);

        // setup the peripherals state in deep sleep
        pinMode(DISPLAY_SCL, INPUT);
        pinMode(DISPLAY_RST,INPUT);

        // rtc_gpio_hold_en((gpio_num_t)RF_MOSI);
        // rtc_gpio_hold_en((gpio_num_t)RF_RST);
        // 20k pull-up resistors on Mosi, Miso, SS and CLK

        gpio_num_t gpio_num = (gpio_num_t)RF_RST;
        rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(gpio_num);
        rtc_gpio_pullup_dis(gpio_num);
        rtc_gpio_hold_en(gpio_num);
        //
        // gpio_num = (gpio_num_t)RF_MOSI;
        // rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
        // rtc_gpio_pulldown_en(gpio_num);
        // rtc_gpio_pullup_dis(gpio_num);
        // rtc_gpio_hold_en(gpio_num);

        pinMode(PIN_VIBRO, INPUT);

        pinMode(RF_MISO, INPUT);
        pinMode(RF_DI0, INPUT);
        pinMode(RF_MOSI, INPUT);

        pinMode(RF_SCK, INPUT);
        pinMode(14,INPUT);
        pinMode(RF_CS, INPUT);

        // disable battery probe
        pinMode(VEXT, OUTPUT);
        digitalWrite(VEXT, HIGH);

        // Enter sleep mode and wait for interrupt
        esp_deep_sleep_start();

        // CPU will be reset here
    #endif

    // After waking the code continues
    // to execute from this point.
    detachInterrupt(digitalPinToInterrupt(PIN_PWRBUTTON));

    #ifdef ARDUINO_SAMD_ZERO
        SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
        USBDevice.attach();
    #endif

    digitalWrite(PIN_LED, HIGH);

    display.powerOn();
    power = true;

    // in case of board change
    needConfig = true;

    keepAlive();

    //end of sleep() function
}


/* TODO sleep2() function
void sleep2()
{
  if (power == false) {
    return;
  }

  // turn off screen
  display.powerOff();
  power = false;

  // interrupt
  attachInterrupt (digitalPinToInterrupt(PIN_PWRBUTTON), isr, LOW);  // attach interrupt handler

  digitalWrite(PIN_LED, LOW);

  CPU::sleep(PIN_PWRBUTTON);

  // After waking the code continues
  // to execute from this point.

  detachInterrupt(digitalPinToInterrupt(PIN_PWRBUTTON));
  digitalWrite(PIN_LED, HIGH);
  power = true;

}
*/

bool pressed(int button) { 
  return (digitalRead(button) == LOW);
}

void waitRelease(int button) {
  while (true) {
    if (!pressed(button)) return;
  }
}

void loadSettings() {

  #ifdef ESP32
    // esp_err_t err = nvs_flash_init();
    // if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    //   // NVS partition was truncated and needs to be erased
    //   // Retry nvs_flash_init
    //   ESP_ERROR_CHECK(nvs_flash_erase());
    //   err = nvs_flash_init();
    //   ESP_ERROR_CHECK(err);
    // }

    preferences.begin("FireFlyNano", false);
    settings.minHallValue = preferences.getShort("MIN_HALL",  MIN_HALL);
    settings.centerHallValue = preferences.getShort("CENTER_HALL", CENTER_HALL);
    settings.maxHallValue = preferences.getShort("MAX_HALL", MAX_HALL);
    settings.boardID = preferences.getLong("BOARD_ID", 0);
    // **************************************** ROADLIGHTS IMPLEMENTATION TODO *****************************

    // **************************************** ROADLIGHTS IMPLEMENTATION TODO *****************************

    preferences.end();

  #elif ARDUINO_SAMD_ZERO

    settings = flash_settings.read();

    if (settings.valid == false) {
      settings.minHallValue = MIN_HALL;
      settings.centerHallValue = CENTER_HALL;
      settings.maxHallValue = MAX_HALL;
      debug("defaults loaded");
    }

  #endif

  remPacket.version = VERSION;

  debug("board id: "+ String(settings.boardID));

  if (settings.boardID == 0) { state = PAIRING; }
}

// only after chamge
void saveSettings() {
  debug("saving settings...");

  #ifdef ESP32

    preferences.begin("FireFlyNano", false);
    preferences.putShort("MIN_HALL",  settings.minHallValue);
    preferences.putShort("CENTER_HALL", settings.centerHallValue);
    preferences.putShort("MAX_HALL", settings.maxHallValue);
    preferences.putLong("BOARD_ID", settings.boardID);
    // **************************************** ROADLIGHTS IMPLEMENTATION TODO *****************************

    // **************************************** ROADLIGHTS IMPLEMENTATION TODO *****************************

    preferences.end();

  #elif ARDUINO_SAMD_ZERO

    settings.valid = true;
    flash_settings.write(settings);

  #endif
}


/*   Check if an integer is within a min and max value */
bool inRange(short val, short minimum, short maximum) {
  return ((minimum <= val) && (val <= maximum));
}

/* Return true if trigger is activated, false otherwise */
bool triggerActive() {
  bool active = digitalRead(PIN_TRIGGER) == LOW;
  if (active) keepAlive();
  return active;
}

void onTelemetryChanged() {

  switch (state) {

    case NORMAL: //
    case IDLE:
      if (telemetry.getSpeed() != 0 || throttle != default_throttle)  {
        // moving
        stopped = false;
      } else {
        if (!stopped) { // just stopped
          stopTime = millis();
          stopped = true;
          debug("stopped");
        }
      }
      break;
  }

}

/* TODO triggerActiveSafe() Returns true if trigger is activated with no/low throttle only
   Return true if trigger is activated with no/low throttle only

bool triggerActiveSafe() {

  bool active = triggerActive();
  if (!active) return false;

  // still on
  if (remPacket.trigger) return true;

  // changed (off >> on)
  if (throttle < 150) {
    // low throttle
    return true;
  } else {
    // unsafe start
    vibrate(60);
    return false;
  }
}

*/

bool sendData() { // Transmit the remPacket

    byte sz = sizeof(remPacket) + CRC_SIZE; // crc
    uint8_t buf[sz];
    memcpy (buf, &remPacket, sizeof(remPacket));

    // calc crc
    buf[sz-CRC_SIZE] = CRC8(&remPacket, sizeof(remPacket));

    bool sent = false;

    // debug("sending command: " + String(remPacket.command)
    //       + ", data: " + String(remPacket.data)
    //       + ", counter: " + String(remPacket.counter)
    //    );

    #ifdef ESP32
        LoRa.beginPacket(sz);
        int t = 0;
        t = LoRa.write(buf, sz);
        LoRa.endPacket();
        // LoRa.receive(PACKET_SIZE + CRC_SIZE);
        sent = t == sz;
    #elif ARDUINO_SAMD_ZERO

    sent = radio.send(buf, sz);
    if (sent) radio.waitPacketSent();

    #endif

  return sent;
}

bool receiveData() {

  uint8_t len =  PACKET_SIZE + CRC_SIZE;
  uint8_t buf[len];

  // receive a packet and check crc
  if (!receivePacket(buf, len)) return false;

  // parse header
  memcpy(&recvPacket, buf, sizeof(recvPacket));
  if (recvPacket.chain != remPacket.counter) {
    debug("Wrong chain value!");
    return false;
  }

  // monitor board state:
  receiverState = static_cast<AppState>(recvPacket.state);

  // response type
  switch (recvPacket.type) {
    case ACK_ONLY:
      // debug("Ack: chain " + String(recvPacket.chain));
      return true;

    case TELEMETRY:
      memcpy(&telemetry, buf, PACKET_SIZE);

      // debug("Telemetry: battery " + String(telemetry.getVoltage())
      //   + ", speed " + String(telemetry.getSpeed())
      //   + ", rpm " + String(telemetry.rpm)
      //   + ", chain " + String(telemetry.header.chain)
      // );

      onTelemetryChanged();

      return true;

    case CONFIG:
      memcpy(&boardConfig, buf, PACKET_SIZE);

      // check chain and CRC
      debug("ConfigPacket: max speed " + String(boardConfig.maxSpeed));

      needConfig = false;
      return true;

    case BOARD_ID:
      memcpy(&boardInfo, buf, PACKET_SIZE);

      // check chain and CRC
      debug("InfoPacket: board ID " + String(boardInfo.id));

      // add to list
      settings.boardID = boardInfo.id;
      saveSettings();

      // pairing done
      state = NORMAL;
      return true;

    case OPT_PARAM_RESPONSE: //recvPacket.type
        // debug("OPT_PARAM_RESPONSE: chain " + String(recvPacket.chain));
        memcpy(&optParamPacket, buf, PACKET_SIZE);
       // if (optParamPacket.optParamCommand == SET_OPT_PARAM_VALUE){}
        switch (optParamPacket.optParamCommand) {
            case SET_OPT_PARAM_VALUE:
                setOptParamValue(optParamPacket.optParamIndex, optParamPacket.unpackOptParamValue()); 
            break;
            case GET_OPT_PARAM_VALUE:
                remPacket.command = OPT_PARAM_MODE;
                remPacket.optParamCommand = SET_OPT_PARAM_VALUE;
                remPacket.optParamIndex = optParamPacket.optParamIndex;
                //remPacket.optParamValue(getOptParamValue(optParamPacket.optParamIndex));
                remPacket.packOptParamValue(getOptParamValue(optParamPacket.optParamIndex));
                requestSendOptParamPacket = true;
            break;
        } // end switch
      return true;
  }

  debug("Unknown response");
  return false;
}

bool receivePacket(uint8_t* buf, uint8_t len) {

    uint8_t expected = len;
    long ms = millis();

    // Should be a message for us now
    if (!responseAvailable(len)) return false;

    #ifdef ARDUINO_SAMD_ZERO

        if (!radio.recv(buf, &len)) return false;

        // signal
        lastRssi = radio.lastRssi();
        signalStrength = constrain(map(lastRssi, -77, -35, 0, 100), 0, 100);

    #elif ESP32
        int i = 0;
        while (LoRa.available()) {
            buf[i] = LoRa.read();
            i++;
        };

        len = i;
        lastRssi = LoRa.packetRssi();
        signalStrength = constrain(map(LoRa.packetRssi(), -100, -50, 0, 100), 0, 100);
    #endif

    // check length
    if (len != expected) {
        debug("Wrong packet length!");
        return false;
    }

    // check crc
    if (CRC8(buf, len - CRC_SIZE) != buf[len - CRC_SIZE]) {
        debug("CRC mismatch!");
        return false;
    }

    return true;
}


bool responseAvailable(uint8_t len) {

    #ifdef ARDUINO_SAMD_ZERO
        return radio.waitAvailableTimeout(REMOTE_RX_TIMEOUT);

    #elif ESP32
        long ms = millis();
        while (true) {
            if (LoRa.parsePacket(len) > 0) return true;
            if (millis() - ms > REMOTE_RX_TIMEOUT) return false; // timeout
        }
    #endif
}

void preparePacket() {

    // speed control
    switch (state) {//state is an AppState() type - (Remote control state)

        case CRUISE: // Set cruise mode
            remPacket.command = SET_CRUISE;
            remPacket.data = round(cruiseSpeed);
        break;

        case CONNECTING:
            if (needConfig) {
                // Ask for board configuration
                remPacket.command = GET_CONFIG;
                debug("send GET_CONFIG");
                break;
            }
            // else falltrough

        case IDLE:
        case NORMAL: // Send throttle to the receiver.
            if (requestSendOptParamPacket){
                debug("requestSendOptParamPacket");
                remPacket.command = OPT_PARAM_MODE;
                requestSendOptParamPacket = false;
                break;
            }        
            if (requestSpeedLimiter){   //activate speed limiter while riding
                debug("requestSpeedLimiter");
                remPacket.command = SPEED_LIMITER;
                remPacket.data = speedLimiterState;
                requestSpeedLimiter = false;
                break;
            }
            else{
                remPacket.command = SET_THROTTLE;
                remPacket.data = round(throttle);
            }
        break;

        case MENU:
            if (requestUpdate) {
                debug("requestUpdate");
                remPacket.command = SET_STATE;
                remPacket.data = UPDATE; //type : AppState{}
                requestUpdate = false;
                break;
            }

            if (requestBT) {
                debug("reguestBT");
                remPacket.command = SET_STATE;
                remPacket.data = BTCOM;
                requestBT = false;
                break;
            }

            // **************************************** LED ROADLIGHTS *****************************
            if (requestSwitchLight) {
                //vibe(4);
                //xTaskCreate(vibeTask, "vibeTask", 100, NULL, 2, &TaskHandle1);
                debug("requestSwitchLight");
                remPacket.command = SET_LIGHT;
                remPacket.data = myRoadLightState;
                requestSwitchLight = false;
                break;
            }
            // **************************************** LED ROADLIGHTS *****************************

            //***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
            if (requestSendOptParamPacket){   //also in MENU mode
                debug("requestSendOptParamPacket");
                remPacket.command = OPT_PARAM_MODE;
                requestSendOptParamPacket = false;
                break;
            }
            //***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
            else {
                remPacket.command = SET_THROTTLE;
                remPacket.data = default_throttle;
            }
        break;

        case PAIRING:
            debug("send PAIRING");
            remPacket.command = SET_STATE;
            remPacket.data = PAIRING;
        break;
    }

    remPacket.address = settings.boardID; // todo: cycle
    remPacket.counter = counter++;
}
/*
   Function used to transmit the remPacket and receive auto acknowledgement.
*/
void transmitToReceiver() {

  // send packet
  digitalWrite(PIN_LED, HIGH);
  preparePacket();

  if (sendData()) {
    // Listen for an acknowledgement reponse and return of uart data
    if (receiveData()) {
      // transmission time
      lastDelay = millisSince(lastTransmission);
      digitalWrite(PIN_LED, LOW);

      // Transmission was a success (2 seconds after remote startup)
      switch (state) {
        case CONNECTING:
          state = IDLE; // now connected
          if (secondsSince(startupTime) > 2) vibrate(50);
          break;
      }
      failCount = 0;
    } else {
      // debug("No reply");
      failCount++;
    }

  } else { // Transmission was not a succes

    failCount++;
    debug("Failed transmission");
  }

  // If lost more than 10 transmissions, we can assume that connection is lost.
  if (failCount > 10) {
    switch (state) {
      case PAIRING: break; // keep pairing mode
      case CONNECTING: break;
      default: // connected
        debug("Disconnected");
        state = CONNECTING;
        vibrate(50);
    }
  }

}

/*
   Measure the hall sensor output and calculate throttle posistion
*/
int readThrottlePosition() {

    int position = default_throttle;
    // Hall sensor reading can be noisy, lets make an average reading.
    uint32_t total = 0;
    uint8_t samples = 20;

    #ifdef ESP32
        // adc1_config_width(ADC_WIDTH_BIT_10);
        // adc1_config_channel_atten(ADC_THROTTLE, ADC_ATTEN_DB_2_5);

        // // Calculate ADC characteristics i.e. gain and offset factors
        // esp_adc_cal_characteristics_t characteristics;
        // esp_adc_cal_get_characteristics(V_REF, ADC_ATTEN_11db, ADC_WIDTH_12Bit, &characteristics);
        //
        // // Read ADC and obtain result in mV
        // uint32_t voltage = adc1_to_voltage(ADC1_CHANNEL_6, &characteristics);
        // printf("%d mV\n",voltage);

        for ( uint8_t i = 0; i < samples; i++ ){
            //        total += adc1_get_raw(ADC_THROTTLE);
            total += adc1_get_voltage(ADC_THROTTLE); //seems to give better results
        }
    #elif ARDUINO_SAMD_ZERO
        for ( uint8_t i = 0; i < samples; i++ ){
            total += analogRead(PIN_THROTTLE);
        }
    #endif

    hallValue = total / samples;
    // debug(hallValue);
    // map values 0..1023 >> 0..255
    if (hallValue >= settings.centerHallValue + hallNoiseMargin) {    // 127 > 150
        //      position = constrain(map(hallValue, settings.centerHallValue + hallNoiseMargin, settings.maxHallValue, 127, 255), 127, 255);
        position = constrain(map(hallValue, settings.centerHallValue, settings.maxHallValue, 127, 255), 127, 255);
    }
    else if (hallValue <= settings.centerHallValue - hallNoiseMargin) {
        //      position = constrain(map(hallValue, settings.minHallValue, settings.centerHallValue - hallNoiseMargin, 0, 127), 0, 127);
        position = constrain(map(hallValue, settings.minHallValue, settings.centerHallValue, 0, 127), 0, 127);
    }
    else {
        // Default value if stick is in deadzone
        position = default_throttle;
    }
    // removing center noise, todo: percent
    //  if (abs(throttle - default_throttle) < hallCenterMargin) {
    if (abs(position - default_throttle) < hallCenterMargin) {
        position = default_throttle;
    }

    smoothedThrottle.add(position); //average over 3 readThrottlePosition() function calls
    return smoothedThrottle.get();
}

/*
   Calculate the remotes battery voltage
*/
float batteryLevelVolts() {
    // read battery sensor every seconds
    if (secondsSince(lastBatterySample) > 1 || lastBatterySample == 0) {
        lastBatterySample = millis();
        uint16_t total = 0;
        uint8_t samples = 10;
        // read raw value
        for (uint8_t i = 0; i < samples; i++) {
            total += analogRead(PIN_BATTERY);
            //total += analogRead(BATTERY_PROBE);
        }
        // calculate voltage
        float voltage;
        #ifdef ARDUINO_SAMD_ZERO
            voltage = ( (float)total / (float)samples ) * 2 * refVoltage / 1024.0;
        #elif ESP32
            //double reading = (double)total / (double)samples;
            //voltage = -0.000000000000016 * pow(reading,4) + 0.000000000118171 * pow(reading,3)- 0.000000301211691 * pow(reading,2)+ 0.001109019271794 * reading + 0.034143524634089;
            //voltage = voltage * 2.64;
        // -------------------- battery voltage bugfix -----------------------------------------
            //voltage = reading;
            //voltage = reading / 512; 
            voltage = ( (float)total / (float)samples ) * 2 * adjVoltage / 1024.0;
        // -------------------- battery voltage bugfix -----------------------------------------
        #endif
        // don't smooth at startup
        if (secondsSince(startupTime) < 3) {
            batterySensor.clear();
        }
        // add to array
        batterySensor.add(voltage);
        lastBatterySample = millis();
    }
    // smoothed value
    return batterySensor.get();
}

/*
   Calculate the remotes battery level
*/
float getBatteryLevel() {
    float voltage = batteryLevelVolts();
    if (voltage <= minVoltage) {
        return 0;
    } else if (voltage >= maxVoltage) {
        return 100;
    }
    return (voltage - minVoltage) * 100 / (maxVoltage - minVoltage);
}


/*
   Calculate the battery level of the board based on the telemetry voltage
*/
float batteryPackPercentage( float voltage ) {

    float maxCellVoltage = 4.2;
    float minCellVoltage;

    if (boardConfig.batteryType == 0) { // Li-ion
        minCellVoltage = 2.8;
    }
    else { // Li-po
        minCellVoltage = 3.0;
    }

    float percentage = (100 - ( (maxCellVoltage - voltage / boardConfig.batteryCells) / ((maxCellVoltage - minCellVoltage)) ) * 100);

    if (percentage > 100.0) {
        return 100.0;
    }
    else{
        if (percentage < 0.0) {
            return 0.0;
        }
    }

    return percentage;
}

// --------------------------------

/*
   Update the OLED for each loop
*/
void updateMainDisplay(){   //LOOP() task on core 1 runs thins function continuously after checkBatteryLevel() and handleButtons()


    display.clearDisplay();
    display.setTextColor(WHITE);

    if (isShuttingDown()){
         drawShutdownScreen();
    }
    else {
        switch (state) {

            case CONNECTING:
                drawConnectingScreen();
                drawThrottle();
            break;
            case PAIRING:
                drawPairingScreen();
                drawThrottle();
            break;

            default: // connected

                switch (page) {

                    case PAGE_MAIN:
                        drawBatteryLevel(); // 2 ms
                        drawMode();
                        drawSignal(); // 1 ms
                        drawMainPage();
                    break;

                    case PAGE_EXT:
                        drawExtPage();
                    break;

                    case PAGE_MENU:
                        drawSettingsMenu();
                    break;

                    case PAGE_DEBUG:
                        drawDebugPage();
                    break;

                    case PAGE_LIGHT_SETTINGS:   //entering light settings submenu
                        //drawDebugPage(); //TEST
                    break;

                  }
        }
    }

    display.display();
}// end updateMainDisplay()

void drawShutdownScreen(){

    drawString("Turning off...", -1, 60, fontMicro);
    // shrinking line
    long ms_left = powerButton.longHoldTime - (millisSince(powerButton.downTime));
    int w = map(ms_left, 0, powerButton.longHoldTime - powerButton.holdTime, 0, 32);
    drawHLine(32 - w, 70, w * 2); // top line

}

void drawPairingScreen() {
    display.setRotation(DISPLAY_ROTATION);
    // blinking icon
    if (millisSince(lastSignalBlink) > 500) {
        signalBlink = !signalBlink;
        lastSignalBlink = millis();
    }
    int y = 17; int x = (display.width()-12)/2;

    if (signalBlink) {
        display.drawXBitmap(x, y, connectedIcon, 12, 12, WHITE);
    } else {
        display.drawXBitmap(x, y, noconnectionIcon, 12, 12, WHITE);
    }

    y += 38;
    drawString("FF_Remote", -1, y, fontMicro);
    drawString(String(RF_FREQ, 0) + " Mhz", -1, y + 12, fontMicro);

    y += 12 + 12*2;
    drawString("Pairing...", -1, y, fontMicro);

    // hall + throttle
    y += 14;
    drawString((triggerActive() ? "T " : "0 ") + String(hallValue) + " " + String(throttle, 0), -1, y, fontMicro);

}

void drawConnectingScreen() {
    display.setRotation(DISPLAY_ROTATION);
    int y = 8;
    display.drawXBitmap((64-24)/2, y, logo, 24, 24, WHITE);
    y += 38;
    drawString("FF_Remote", -1, y, fontMicro);
    drawString(String(RF_FREQ, 0) + " Mhz", -1, y + 12, fontMicro);
    // blinking icon
    if (millisSince(lastSignalBlink) > 500) {
        signalBlink = !signalBlink;
        lastSignalBlink = millis();
    }
    y = 68;
    if (signalBlink == true) {
        display.drawXBitmap((64-12)/2, y, connectedIcon, 12, 12, WHITE);
    } else {
        display.drawXBitmap((64-12)/2, y, noconnectionIcon, 12, 12, WHITE);
    }
    y += 12 + 12;
    drawString("Connecting...", -1, y, fontMicro);
    // hall + throttle
    y += 14;
    drawString((triggerActive() ? "T " : "0 ") + String(hallValue) + " " + String(throttle, 0), -1, y, fontMicro);
    // remote battery
    int level = batteryLevel;
    if (level > 20 || signalBlink) { // blink
        y += 12;
        drawString(String(level) + "% " + String(batteryLevelVolts(), 2) + "v", -1, y, fontMicro);
    }
}

void drawThrottle() {
    if (throttle > 127) {
        // right side - throttle
        int h = map(throttle - 127, 0, 127, 0, 128);
        drawVLine(63, 128 - h, h); // nose
    }
    if (throttle < 127) {
        // left side - brake
        int h = map(throttle, 0, 127, 128, 0);
        drawVLine(0, 0, h); // nose
    }
}

void calibrateScreen() {
    int padding = 10;
    int tick = 5;
    int w = display.width() - padding*2;
    int position = readThrottlePosition();

    switch (calibrationStage) {
        case CALIBRATE_CENTER:
            tempSettings.centerHallValue = hallValue;
            tempSettings.minHallValue = hallValue - 100;
            tempSettings.maxHallValue = hallValue + 100;
            calibrationStage = CALIBRATE_MAX;
        break;

        case CALIBRATE_MAX:
            if (hallValue > tempSettings.maxHallValue) {
                tempSettings.maxHallValue = hallValue;
            } else if (hallValue < tempSettings.minHallValue) {
                calibrationStage = CALIBRATE_MIN;
            }
        break;

        case CALIBRATE_MIN:
            if (hallValue < tempSettings.minHallValue) {
                tempSettings.minHallValue = hallValue;
            } else if (hallValue == tempSettings.centerHallValue) {
                calibrationStage = CALIBRATE_STOP;
            }
        break;

        case CALIBRATE_STOP:
        if (triggerButton.getState() == CLICK) {
            // apply calibration values
            settings.centerHallValue = tempSettings.centerHallValue;
            settings.minHallValue = tempSettings.minHallValue;
            settings.maxHallValue = tempSettings.maxHallValue;

            backToMainMenu();
            display.setRotation(DISPLAY_ROTATION);
            saveSettings();

            return;
        }
    }

    display.setRotation(DISPLAY_ROTATION_90);

    int center = map(tempSettings.centerHallValue, tempSettings.minHallValue, tempSettings.maxHallValue, display.width() - padding, padding);

    int y = 8;
    drawString(String(tempSettings.maxHallValue), 0, y, fontDesc);
    drawString(String(tempSettings.centerHallValue), center-10, y, fontDesc);
    drawString(String(tempSettings.minHallValue), 128-20, y, fontDesc);

    // line
    y = 16;
    drawHLine(padding, y, w);
    // ticks
    drawVLine(padding, y-tick, tick);
    drawVLine(center, y-tick, tick);
    drawVLine(w + padding, y-tick, tick);

    // current throttle position
    int th = map(hallValue, tempSettings.minHallValue, tempSettings.maxHallValue, display.width() - padding, padding);
    drawVLine(constrain(th, 0, display.width()-1), y, tick);

    y = 32;
    drawString(String(hallValue), constrain(th-10, 0, w), y, fontDesc); // min

    y = 48;
    switch (calibrationStage) {
        case CALIBRATE_MAX:
        case CALIBRATE_MIN:
            drawString("Press throttle fully", -1, y, fontDesc);
            drawString("forward and backward", -1, y+14, fontDesc);
        break;
        case CALIBRATE_STOP:
            drawString("Calibration completed", -1, y, fontDesc);
            drawString("Trigger: Save", -1, y+14, fontDesc);
    }

}

void backToMainMenu() {
    menuPage = MENU_MAIN;
    currentMenu = 0;
    //subMenuItem = 0;
    initFlag_PSL = 1;
    initFlag_PVS = 1;
}
void backToMenuLight() {

   }

void drawSettingsMenu() {   //LOOP() task on core 1 runs this function continuously via updateMainDisplay() when PAGE state == PAGE_MENU
    //  display.drawFrame(0,0,64,128);
    int y = 10;
    // check speed
    if (state != MENU) {
        if (telemetry.getSpeed() != 0) {
            drawString("Stop", -1, y=50, fontDesc);
            drawString("to use", -1, y+=14, fontDesc);
            drawString("menu", -1, y+=14, fontDesc);
            return;
        }
        else { // enable menu
            state = MENU;
        }
    }

    // wheel = up/down
    int position = readThrottlePosition();
    int deadBand = 15;
    int lastMenuIndex = round(currentMenu);
    int nextMenuIndex = lastMenuIndex;

    switch (menuPage) {

        case MENU_MAIN:
            // --------- mainMenu wheel control navigation---------------------
            if (position < default_throttle - deadBand) {
                if (currentMenu < ARRAYLEN(MENUS)-1){currentMenu += 0.25;}
            }
            if (position > default_throttle + deadBand) {
                if (currentMenu > 0){currentMenu -= 0.25;}
            }
            nextMenuIndex = round(currentMenu);
            if (lastMenuIndex != nextMenuIndex){vibrate(50);}   //short vibration each time we change the selected menu item
            // ----------------------------------------------------------------

            //header
            drawString("- Menu -", -1, y, fontDesc);
            y += 20;
            for (int i = 0; i < (ARRAYLEN(MENUS)); i++) {
                drawString(MENUS[i][0], -1, y, fontDesc);
                // draw cursor
                if (i == round(currentMenu)){
                        drawFrame(0, y-10, 64, 14);
                }
                //lastMenuIndex = nextMenuIndex;
                y += 16;
            }
            //drawString(".", -1, y, fontDesc);

            if (triggerButton.getState() == CLICK) {
                menuPage = MENU_SUB;
                subMenu = round(currentMenu);
                currentMenu = 0;
                //waitRelease(PIN_TRIGGER);
                vibe(3);   //short vibrations when pressing the trigger
            }
        break;

        case MENU_SUB:
            // --------- subMenus wheel control navigation---------------------
            if (position < default_throttle - deadBand) {
            if (currentMenu < ARRAYLEN(MENUS[subMenu])-2){currentMenu += 0.25;}
                //   if (currentMenu < (_countof (*MENUS)) -2){currentMenu += 0.25;}
            // if (currentMenu < (sizeof(MENUS[subMenu])/sizeof(*MENUS[subMenu][0])) -2){currentMenu += 0.25;}
            // if (currentMenu < (sizeof(MENUS[subMenu])/sizeof(*MENUS[subMenu])) -2){currentMenu += 0.25;}
               // if (currentMenu < ARRAYLEN(MENUS[subMenu])-2){currentMenu += 0.25;}
            }
            if (position > default_throttle + deadBand) {
                if (currentMenu > 0) currentMenu -= 0.25;
            }
            nextMenuIndex = round(currentMenu);
            if (lastMenuIndex != nextMenuIndex){vibrate(50);}   //short vibration each time we change the selected menu item
            // ----------------------------------------------------------------

            // header
            drawString("-" + MENUS[subMenu][0] + "-", -1, y, fontDesc);
            y += 20;
            for (int i = 0; i < ARRAYLEN(MENUS[subMenu]) -1; i++) {
                drawString(MENUS[subMenu][i+1], -1, y, fontDesc);
                // draw cursor
                if (i == round(currentMenu)) {
                    drawFrame(0, y-10, 64, 14);
                }
                y += 16;
            }
            drawString("- - - -", -1, y, fontDesc);

            if (triggerButton.getState() == CLICK) {
                menuPage = MENU_ITEM;
                subMenuItem = round(currentMenu);
                //waitRelease(PIN_TRIGGER);
                vibe(3);    //short vibrations when pressing the trigger

                // handle commands
                switch (subMenu) {
                    case MENU_INFO:
                        switch (subMenuItem) {
                            case INFO_DEBUG:
                            break;
                            case INFO_SETTINGS:
                                initFlag_PSL=1;
                            break;
                        }                    
                    break;

                    case MENU_REMOTE:
                        switch (subMenuItem) {
                            case REMOTE_PAIR:
                                state = PAIRING;
                                backToMainMenu(); // exit menu
                            break;
                            case REMOTE_SLEEP_TIMER:
                                loadOptParamFromReceiver(IDX_REMOTE_SLEEP_TIMEOUT);
                            break;
                        }
                    break;

                    case MENU_BOARD:
                        switch (subMenuItem) {
                            case BOARD_UPDATE:
                                requestUpdate = true;
                            break;
                        #ifdef BT_ENABLED
                            case BOARD_BT:
                                requestBT = true;
                                backToMainMenu();
                            break;
                        #endif
                            case BOARD_MENU_BATTERY_CELLS:
                                loadOptParamFromReceiver(IDX_BATTERY_CELLS);
                            break;
                            case BOARD_MENU_MOTOR_POLES:
                                loadOptParamFromReceiver(IDX_MOTOR_POLES);
                            break; 
                            case BOARD_MENU_WHEEL_DIAMETER:
                                loadOptParamFromReceiver(IDX_WHEEL_DIAMETER);
                            break; 
                            case BOARD_MENU_WHEEL_PULLEY:
                                loadOptParamFromReceiver(IDX_WHEEL_PULLEY);
                            break;
                            case BOARD_MENU_MOTOR_PULLEY:
                                loadOptParamFromReceiver(IDX_MOTOR_PULLEY);
                            break;                                 
                        }
                    break;

                    case MENU_LIGHT:
                        switch (subMenuItem){
                            case SWITCH_LIGHT_ON:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = ON;
                                backToMainMenu();                                
                            break;
                            case SWITCH_LIGHT_OFF:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = OFF;
                                backToMainMenu();
                            break;
                            case SWITCH_LIGHT_SIDE:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = SIDE;
                                backToMainMenu();
                            break;

                            case SWITCH_LIGHT_SIDE_THROTTLE:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = SIDE_THROTTLE;
                                backToMainMenu();
                            break;

                            case SWITCH_LIGHT_SIDE_RAINBOW:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = SIDE_RAINBOW;
                                backToMainMenu();
                            break;

                            case SWITCH_LIGHT_BRAKES_ONLY:
                                vibe(2);
                                requestSwitchLight = true;
                                myRoadLightState = BRAKES_ONLY;
                                backToMainMenu();
                            break;
                            case ROADLIGHT_SETTINGS:
                                //download 3 current values from receiver:
                               loadOptParamFromReceiver(IDX_LED_SIDE_COLOR);
                               loadOptParamFromReceiver(IDX_LED_BRIGHTNESS);
                               loadOptParamFromReceiver(IDX_LED_BRIGHTNESS_BRAKE);
                            break;
                        }
                    break;

                    case MENU_RECEIVER:
                        switch (subMenuItem){
                            case SUBM_THROTTLE_MODE:
                                loadOptParamFromReceiver(IDX_THROTTLE_MODE);
                                //currentParamAdjValue = getOptParamValue(IDX_AUTO_BRAKE_RELEASE);
                            break;
                            case SUBM_LIMITED_SPEED_MAX:
                                loadOptParamFromReceiver(IDX_LIMITED_SPEED_MAX);
                            break;  
                        }
                    break;

                    case MENU_AUTO_CRUISE:
                        switch (subMenuItem){
                            case CRUISE_MENU_AUTO_CRUISE:
                                loadOptParamFromReceiver(IDX_AUTO_CRUISE_ON);
                            break;
                            case CRUISE_MENU_PUSHING_SPEED:
                                loadOptParamFromReceiver(IDX_PUSHING_SPEED);
                            break;
                            case CRUISE_MENU_PUSHING_TIME:
                                loadOptParamFromReceiver(IDX_PUSHING_TIME);
                            break;
                            case CRUISE_MENU_CRUISE_CURRENT_SPIKE:
                                loadOptParamFromReceiver(IDX_CRUISE_CURRENT_SPIKE);
                            break;
                            case CRUISE_MENU_AUTO_CRUISE_TIME:
                                loadOptParamFromReceiver(IDX_AUTO_CRUISE_TIME);
                            break;
                            case CRUISE_MENU_CRUISE_CURRENT_LOW:
                                loadOptParamFromReceiver(IDX_CRUISE_CURRENT_LOW);
                            break;
                        }
                    break;

                }
            } // END if (pressed(PIN_TRIGGER))
        /*
        //  PWR_BUTTON action specific for each subMenu 
            if (PwrButtonState == ButtonState::CLICK) { //what to do when PWRBUTTON is clicked
                //menuPage = MENU_ITEM;
                //subMenuItem = round(currentMenu);
                //waitRelease(PIN_TRIGGER);
                vibe(3);    //short vibrations when pressing the trigger

                // handle commands
                switch (subMenu) {
                    case MENU_INFO:
                        switch (subMenuItem) {
                            case INFO_DEBUG:
                            //subMenuItem=99;
                            backToMainMenu();
                            break;
                            case INFO_SETTINGS:
                            break;
                           // case 99:
                           // backToMainMenu();
                           // break;
                        }                    
                    break;

                    case MENU_REMOTE:
                        //backToMainMenu();
                        switch (subMenuItem) {
                            case REMOTE_PAIR:
                            break;
                        }
                    break;

                    case MENU_BOARD:
                        //backToMainMenu();
                        switch (subMenuItem) {
                            case BOARD_UPDATE:
                            break;

                            case BOARD_MENU_MOTOR_MIN:
                            break;
                            case BOARD_MENU_MOTOR_MAX:
                            break; 
                            case BOARD_MENU_BATTERY_MIN:
                            break; 
                            case BOARD_MENU_BATTERY_MAX:
                            break;
                            case BOARD_MENU_TEST:
                            break;                                 
                        }
                    break;

                    case MENU_LIGHT:
                    backToMainMenu();
                        switch (subMenuItem){
                            case SWITCH_LIGHT_ON:
                            break;
                            case SWITCH_LIGHT_OFF:
                            break;
                            case SWITCH_LIGHT_BRAKES_ONLY:
                            case ROADLIGHT_SETTINGS:
                            break;
                        }
                    break;

                    case MENU_RECEIVER:
                        switch (subMenuItem){
                            case SUBM_THROTTLE_MODE:
                            break;
                        }
                    break;

                    case MENU_AUTO_CRUISE:
                        switch (subMenuItem){
                            case CRUISE_MENU_AUTO_CRUISE:
                            break;
                            case CRUISE_MENU_PUSHING_SPEED:
                            break;
                            case CRUISE_MENU_PUSHING_TIME:
                            break;
                            case CRUISE_MENU_CRUISE_CURRENT_SPIKE:
                            break;
                            case CRUISE_MENU_AUTO_CRUISE_TIME:
                            break;
                            case CRUISE_MENU_CRUISE_CURRENT_LOW:
                            break;
                        }
                    break;
                }
            } // END if (checkButton()==CLICK) ---- PWRBUTTON CLICK -------      
        */
        break; //MENU_SUB

        case MENU_ITEM: //Here we can display a specific page after handling commands, and stay on it (similar to debugPage)
            switch (subMenu) {
                case MENU_INFO:
                    switch (subMenuItem) {
                        case INFO_DEBUG:
                            drawDebugPage();
                        break;
                        case INFO_SETTINGS:
                            #ifdef EXPERIMENTAL
                                paramSelectorList(myParamSelectorIndexArray1);
                            #endif
                            #ifndef EXPERIMENTAL 
                                backToMainMenu();
                            #endif
                        break;                        
                    }
                break;

                case MENU_REMOTE:
                    switch (subMenuItem) {
                        case REMOTE_CALIBRATE:
                            calibrateScreen();
                        break;
                        case REMOTE_SLEEP_TIMER:
                            paramValueSelector(IDX_REMOTE_SLEEP_TIMEOUT, "Remote\nauto-off\ntimeout", 60, 300, 5, 0, "s");
                        break;                        
                    }
                break;

                case MENU_BOARD:
                    switch (subMenuItem) {
                        case BOARD_UPDATE:
                            backToMainMenu();
                        break;
                    #ifdef BT_ENABLED
                        case BOARD_BT:
                        break;
                    #endif
                        case BOARD_MENU_BATTERY_CELLS:
                            paramValueSelector(IDX_BATTERY_CELLS, "Battery\ncells", 1,16,1,0,"S");
                        break;
                        case BOARD_MENU_MOTOR_POLES:
                            paramValueSelector(IDX_MOTOR_POLES, "Motor\npoles nb", 2,48,2,0,"p");
                        break;
                        case BOARD_MENU_WHEEL_DIAMETER:
                            paramValueSelector(IDX_WHEEL_DIAMETER, "Wheel\ndiameter", 40,400,1,0,"mm");
                        break;
                        case BOARD_MENU_WHEEL_PULLEY:
                            paramValueSelector(IDX_WHEEL_PULLEY, "Wheel\npulley size", 1,100,1,0,"");
                        break;                                                
                        case BOARD_MENU_MOTOR_PULLEY:
                            paramValueSelector(IDX_MOTOR_PULLEY, "Motor\npulley size", 1,100,1,0,"");
                        break;                                                   
                    }
                break;

                case MENU_LIGHT: //if we want to display a specific page and stay on it for some menu items
                    switch (subMenuItem){
                        case SWITCH_LIGHT_ON:
                            //nothing to display
                        break;
                        case SWITCH_LIGHT_OFF:
                            //nothing to display
                        break;
                        case SWITCH_LIGHT_SIDE:

                        break;
                        case SWITCH_LIGHT_SIDE_THROTTLE:
                            //nothing to display
                        break;
                        case SWITCH_LIGHT_SIDE_RAINBOW:
                            //nothing to display
                        break;
                        case SWITCH_LIGHT_BRAKES_ONLY:
                            //nothing to display
                        break;
                        case ROADLIGHT_SETTINGS:
                            drawLightSettingsPage();
                        break;
                    }
                break;

                case MENU_RECEIVER: //if we want to display a specific page and stay on it for some menu items
                    switch (subMenuItem){
                        case SUBM_THROTTLE_MODE:
                            //drawThrottleModePage();
                            #ifdef EXPERIMENTAL
                                paramValueSelector(IDX_THROTTLE_MODE, "App mode:", 0,(VTM_ENUM_END-1),1,0, " ", VescThrottleMode_label[(int)currentParamAdjValue]);// 2+ : for testing purpose via other VescUART commands
                            #endif
                            #ifndef EXPERIMENTAL 
                                paramValueSelector(IDX_THROTTLE_MODE, "App mode:", 0,1,1,0, " ", VescThrottleMode_label[(int)currentParamAdjValue]);
                            #endif                            
                        break;
                        case SUBM_LIMITED_SPEED_MAX:
                            paramValueSelector(IDX_LIMITED_SPEED_MAX, "Max speed\nlimit", 5, 50, 0.5, 1, "kmh");
                        break;
                    }
                break;

                case MENU_AUTO_CRUISE: //if we want to display a specific page and stay on it for some menu items
                    switch (subMenuItem){
                        case CRUISE_MENU_AUTO_CRUISE:
                            //nothing to display
                            paramValueSelector(IDX_AUTO_CRUISE_ON, "Activate\nAuto-cruise\nmode?", 0,+1,1,0," ");
                        break;
                        case CRUISE_MENU_PUSHING_SPEED:
                            paramValueSelector(IDX_PUSHING_SPEED, "PushSpeed", 5,18,0.5,1,"kmh");
                        break;
                        case CRUISE_MENU_PUSHING_TIME:
                            paramValueSelector(IDX_PUSHING_TIME, "PushTime", 1,6,0.5,1,"s");
                        break;
                        case CRUISE_MENU_CRUISE_CURRENT_SPIKE:
                            paramValueSelector(IDX_CRUISE_CURRENT_SPIKE, "Current\nSpike", 1,10,0.2,1,"Amp");
                        break;                        
                        case CRUISE_MENU_AUTO_CRUISE_TIME:
                            paramValueSelector(IDX_AUTO_CRUISE_TIME, "Cruise\nTime", 10,60,1,0,"s");
                        break;  
                        case CRUISE_MENU_CRUISE_CURRENT_LOW:
                            paramValueSelector(IDX_CRUISE_CURRENT_LOW, "Current\nLow", 2,8,0.5,1,"Amp");
                        break;   
                    }
                break;

            }//end switch

        break;

    } //end switch

}//END drawSettingsMenu()

void drawDebugPage() {
    //  display.drawFrame(0,0,64,128);
    int y = 10;
    drawString(String(settings.boardID, HEX), -1, y, fontDesc);
    y = 20;
    //    drawStringCenter(String(lastDelay), " ms", y);
    drawString(String(lastDelay) + " ms", 0, y, fontMicro);
    y += 10;//25;
    //    drawStringCenter(String(lastRssi, 0), " db", y);
    drawString(String(lastRssi) + " db", 0, y, fontMicro);
    y += 10;
    //    drawStringCenter(String(readThrottlePosition()), String(hallValue), y);
    drawString(String(readThrottlePosition()),0,y,fontMicro); drawString(" / " + String(hallValue), 16, y, fontMicro);
    y += 15;
    if (pressed(PIN_PWRBUTTON)) {
        drawString("PWRBUTT", 0, y, fontMicro);
    }
    if (powerButton.getState() == CLICK) {
        drawString("PW_CLICK", 0, y, fontMicro);
    }
    y += 10;
    switch(powerButton.getState()){
        case RELEASED:
            drawString("-", 0, y, fontMicro);
        break;
        case CLICK:
            drawString("CLICK", 0, y, fontMicro);
        break;
        case DBL_CLICK:
            drawString("DBL_CLICK", 0, y, fontMicro);
        break;
        case HOLD:
            drawString("HOLD", 0, y, fontMicro);
        break;
        case LONG_HOLD:
            drawString("LONG_HOLD", 0, y, fontMicro);
        break;
    }
    y += 10;
    if (pressed(PIN_TRIGGER)) {
        drawString("T", 0, y, fontMicro);
    }
    switch (triggerButton.getState()) {
        case RELEASED:
            drawString(" ", 0, y, fontMicro);
        break;
        case CLICK:
            drawString("Click", 0, y, fontMicro);
        break;
        case DBL_CLICK:
            drawString("Dbl_Click", 0, y, fontMicro);
        break;
        case HOLD:
            drawString("Hold", 0, y, fontMicro);
            speedLimiter(!speedLimiterState);
        break;
        case LONG_HOLD:
            drawString("Long_Hold", 0, y, fontMicro);
            vibe(10000);
        break;
    }
    drawString("SL" + String(speedLimiterState, 10), 45, y, fontMicro);
    #ifdef DEBUG
        y += 10;
        drawString("Stack -" + String(debugTaskSize, 10), 0, y, fontMicro);
    #endif
}

void debugButtons() {   //displays button state on lower right screen corner
    int y = 100;
    int x = 55;
    if (pressed(PIN_PWRBUTTON)) {
        drawString("P", x, y, fontDesc);
    }
    switch(powerButton.getState()){
        case RELEASED:
            drawString("-", x, y, fontDesc);
        break;
        case CLICK:
            drawString("C", x, y, fontDesc);
        break;
        case DBL_CLICK:
            drawString("D", x, y, fontDesc);
        break;
        case HOLD:
            drawString("H", x, y, fontDesc);
        break;
        case LONG_HOLD:
            drawString("L", x, y, fontDesc);
        break;
    }
    y += 14;
    if (pressed(PIN_TRIGGER)) {
        drawString("T", x, y, fontDesc);
    }
    switch (triggerButton.getState()) {
        case RELEASED:
            drawString("-", x, y, fontDesc);
        break;
        case CLICK:
            drawString("C", x, y, fontDesc);
        break;
        case DBL_CLICK:
            drawString("D", x, y, fontDesc);
        break;
        case HOLD:
            drawString("H", x, y, fontDesc);
        break;
        case LONG_HOLD:
            drawString("L", x, y, fontDesc);
        break;
    }
}

int getStringWidth(String s) {
    int16_t x1, y1;
    uint16_t w1, h1;
    display.getTextBounds(s, 0, 0, &x1, &y1, &w1, &h1);
    return w1;
}

void drawString(String string, int x, int y, const GFXfont *font) {
    display.setFont(font);
    if (x == -1) {
        x = (display.width() - getStringWidth(string)) / 2;
    }
    display.setCursor(x, y);
    display.print(string);
}

float speed() {
    return telemetry.getSpeed();
}

void drawMode() {
    String m = "?";
    switch (state) {

        case IDLE:
            m = "!";
        break;

        case NORMAL:
            m = "N";
        break;

        case ENDLESS:
            m = "E";
        break;

        case CRUISE:
            m = "C";
        break;
    }

    // top center
    drawString(m, -1, 10, fontPico);

}//drawMode()

void drawBars(int x, int y, int bars, String caption, String s) {

    const int width = 14;

    drawString(caption, x + 4, 10, fontDesc);

    if (bars > 0) { // up
        for (int i = 0; i <= 10; i++)
            if (i <= bars) drawHLine(x, y - i*3, width);
    }
    else { // down
        for (int i = 0; i >= -10; i--)
            if (i >= bars) drawHLine(x, y - i*3, width);
    }

    // frame
    drawHLine(x, y-33, width);
    drawHLine(x, y+33, width);
    // values
    drawString(s, x, y + 48, fontPico);
}

void drawBars_2(int x, int y, int bars, String caption, String s, bool isHighlighted) {

    const int width = 14;

    drawString(caption, x + 4, 10, fontDesc);

    if (bars > 0) { // up
        for (int i = 0; i <= 10; i++)
            if (i <= bars) drawHLine(x, y - i*3, width);
    }
    else { // down
        for (int i = 0; i >= -10; i--)
            if (i >= bars) drawHLine(x, y - i*3, width);
    }

    // frame
    drawHLine(x, y-33, width);
    //drawHLine(x, y+33, width);
    // values
    display.setRotation(DISPLAY_ROTATION_90);
    if (isHighlighted==true){drawString((" - " + s + " - "), 5, x + width -2, fontPico);    }
    else{drawString(s, 10, x + width -2, fontDesc); }
    display.setRotation(DISPLAY_ROTATION);

}

/*
   Print the main page: Throttle, battery level and telemetry
*/
void drawExtPage() {
    const int gap = 20;

    int x = 5;
    int y = 48;
    float value;
    int bars;

    drawHLine(2, y, 64-2);

    // 1 - throttle
    value = throttle;  //telemetry.getInputCurrent
    bars = map(throttle, 0, 254, -10, 10);
    drawBars(x, y, bars, "T", String(bars));

    // motor current
    x += gap;
    bars = map(telemetry.getMotorCurrent(), MOTOR_MIN, MOTOR_MAX, -10, 10);
    drawBars(x, y, bars, "M", String(telemetry.getMotorCurrent(),0));

    // battery current
    x += gap;
    bars = map(telemetry.getInputCurrent(), BATTERY_MIN, BATTERY_MAX, -10, 10);
    drawBars(x, y, bars, "B", String(telemetry.getInputCurrent(), 0) );

    // FET & motor temperature
    drawString(String(telemetry.tempFET) + " C    "
    + String(telemetry.tempMotor) + " C", -1, 114, fontPico);
}

/*
   Print the main page: Throttle, battery level in longboard shape and telemetry
*/
void drawMainPage() {

    float value;
    String s;
    int x = 0;
    int y = 37;
    int h;
    //  display.drawFrame(0,0,64,128);
    // --- Speed ---
    value = speed();
    float speedMax = boardConfig.maxSpeed;
    String m = "km/h";

    drawStringCenter(String(value, 0), m, y);

    if (receiverState == CONNECTED) {
        // speedometer graph height array
        uint8_t a[16] = {3, 3, 4, 4, 5, 6, 7, 8, 10, 11, 13, 15, 17, 20, 24, 28};
        y = 48;

        for (uint8_t i = 0; i < 16; i++) {
            h = a[i];
            if (speedMax / 16 * i <= value) {
                drawVLine(x + i * 4 + 2, y - h, h);
            }
            else {
                drawPixel(x + i * 4 + 2, y - h);
                drawPixel(x + i * 4 + 2, y - 1);
            }
            if(speedLimiterState == 1 && speedMax/16*i > getOptParamValue(IDX_LIMITED_SPEED_MAX) && speedMax/16*i < getOptParamValue(IDX_LIMITED_SPEED_MAX) + 2){
                if(  remainder(floor(2*millis()/500), 2 ) == 0 ){
                    for(int z = 0; z < (h/4); z++){
                        drawVLine(x + i * 4 + 2, (y - h + 4*z), 0.5);
                    }
                }
            }
        }
    }
    else {
        switch (receiverState) {
            case STOPPING: m = "Stopping"; break;
            case STOPPED: m = "Stopped"; break;
            case PUSHING: m = "Pushing"; break;
            case COASTING: m = "Coasting"; break;
            case ENDLESS: m = "Cruise"; break;
            case UPDATE: m = "Update"; break;
            default: m = "?";
        }

        drawString(m, -1, 50, fontDesc);
    }

    // --- Battery ---
    value = batteryPackPercentage( telemetry.getVoltage() );

    y = 74;
    int battery = (int) value;
    drawStringCenter(String(battery), "%", y);
    drawString(String(telemetry.getVoltage(), 1), 44, 73, fontPico);

    y = 80;
    x = 1;

    // longboard body
    h = 12;
    uint8_t w = 41;
    drawHLine(x + 10, y, w); // top line
    drawHLine(x + 10, y + h, w); // bottom

    // nose
    drawHLine(x + 2, y + 3, 5); // top line
    drawHLine(x + 2, y + h - 3, 5); // bottom

    drawPixel(x + 1, y + 4);
    drawVLine(x, y + 5, 3); // nose
    drawPixel(x + 1, y + h - 4);

    display.drawLine(x + 6, y + 3, x + 9, y, WHITE);
    display.drawLine(x + 6, y + h - 3, x + 9, y + h, WHITE);

    // tail
    drawHLine(64 - 6 - 2, y + 3, 5); // top line
    drawHLine(64 - 6 - 2, y + h - 3, 5); // bottom

    drawPixel(64 - 3, y + 4);
    drawVLine(64 - 2, y + 5, 3); // tail
    drawPixel(64 - 3, y + h - 4);

    display.drawLine(64 - 6 - 3, y + 3, 64 - 6 - 6, y, WHITE);
    display.drawLine(64 - 6 - 3, y + h - 3, 64 - 6 - 6, y + h, WHITE);

    // longboard wheels
    drawBox(x + 3, y, 3, 2); // left
    drawBox(x + 3, y + h - 1, 3, 2);
    drawBox(64 - 7, y, 3, 2); // right
    drawBox(64 - 7, y + h - 1, 3, 2);

    // battery sections
    for (uint8_t i = 0; i < 14; i++) {
        if (round((100 / 14) * i) <= value) {
            drawBox(x + i * 3 + 10, y + 2, 1, h - 3);
        }
    }

    // --- Distance in km ---
    value = telemetry.getDistance();
    String km;

    y = 118;

    if (value >= 1) {
        km = String(value, 0);
        drawStringCenter(km, "km", y);
    }
    else {
        km = String(value * 1000, 0);
        drawStringCenter(km, "m", y);
    }

    // max distance
    int range = boardConfig.maxRange;
    if (value > range) {range = value;}

    drawString(String(range), 52, 118, fontPico);

    // dots
    y = 122;
    for (uint8_t i = 0; i < 16; i++) {
        drawBox(x + i * 4, y + 4, 2, 2);
    }

    // start end
    drawBox(x, y, 2, 6);
    drawBox(62, y, 2, 6);
    drawBox(30, y, 2, 6);

    // position
    drawBox(x, y + 2, value / range * 62, 4);
}

void drawStringCenter(String value, String caption, uint8_t y) {

    // draw digits
    int x = 0;

    display.setFont(fontDigital);

    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(WHITE);        // Draw white text

    display.setCursor(x, y);
    display.print(value);

    // draw caption km/%
    x += getStringWidth(value) + 4;
    y -= 9;

    display.setCursor(x, y);
    display.setFont(fontDesc);

    display.print(caption);

}

void drawSignal() { // Print the signal icon if connected, and flash the icon if not connected

    int x = 45;
    int y = 11;

    for (int i = 0; i < 9; i++) {
        if (round((100 / 9) * i) <= signalStrength){
            drawVLine(x + (2 * i), y - i, i);
        }
    }
}

void drawBatteryLevel() { // Print the remotes battery level as a battery on the OLED

    int x = 2;
    int y = 2;

    // blinking
    if (batteryLevel < 20) {
        if (millisSince(lastSignalBlink) > 500) {
            signalBlink = !signalBlink;
            lastSignalBlink = millis();
        }
    }
    else { signalBlink = false;
    }

    drawFrame(x, y, 18, 9);
    drawBox(x + 18, y + 2, 2, 5);

    // blink
    if (signalBlink) { return;
    }

    // battery level
    drawBox(x + 2, y + 2, batteryLevel * 14 / 100, 5);

    if (batteryLevel <= 19) { // < 10%
        drawString(String(batteryLevel), x + 7, y + 6, fontMicro);
    }

}

bool isShuttingDown() {
    // button held for more than holdTime
    return (powerButton.buttonValue == LOW) && powerButton.holdEventPast;
}

void vibrate(int ms) {
    #ifdef PIN_VIBRO
        digitalWrite(PIN_VIBRO, HIGH);
        vTaskDelay(ms/portTICK_PERIOD_MS);
        digitalWrite(PIN_VIBRO, LOW);
    #endif
}

void vibe(int vMode){    //vibrate() combos
    // if (vMode == 0){ }
    vibeMode = vMode;
}

//***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
    /* GENERAL USE  :

    1. temporary: - loadOptParamFromReceiver(IDX_MY_PARAM); //get the value from the receiver
                  - MY_PARAM = localOptParamValueArray[IDX_MY_PARAM]  //update local variables
    1. TODO - all values in localOptParamValueArray[] are retrieved at startup from the receiver's memory, or default hardcoded values

    2.  - myParam = getOptParamValue(IDX_MY_PARAM);
    3.  - modify myParam to new value 
    4.  - setOptParamValue(IDX_MY_PARAM, myParamValue);  //store the value locally
    5.  - sendOptParamToReceiver(IDX_MY_PARAM); //send to receiver which updates local value and saved into flash memory
    */
void setOptParamValue(uint8_t myGlobalSettingIndex, float value){ // Set a value of a specific setting by index in the local table.
   uint8_t arrayIndex = myGlobalSettingIndex;
   localOptParamValueArray[arrayIndex] = value;
}

float getOptParamValue(uint8_t myGlobalSettingIndex){ // Get settings value by index from the local table.
   float value;
   uint8_t arrayIndex = myGlobalSettingIndex;
   value = localOptParamValueArray[arrayIndex];
   return value;
}

// Send a parameter to receiver with the next packet. Receiver will update it's local localOptParamValueArray[] and save to flash automatically.
void sendOptParamToReceiver(uint8_t myGlobalSettingIndex){
    uint8_t arrayIndex = myGlobalSettingIndex;
    //setOptParamValue(myOptIndex, myLightSettingValue);  //store the value locally
    remPacket.command = OPT_PARAM_MODE; //prepare the next packet to update receiver's value
    remPacket.optParamCommand = SET_OPT_PARAM_VALUE;
    remPacket.optParamIndex = arrayIndex;
    remPacket.packOptParamValue(getOptParamValue(arrayIndex));
    requestSendOptParamPacket = true;    //send the value to the receiver
}

// Retrieve a parameter from the receiver. Wait until localOptParamValueArray[] is updated with the received value.
bool loadOptParamFromReceiver(uint8_t myGlobalSettingIndex){   //returns TRUE if the local OPT_PARAM is updated within 100ms
    uint8_t arrayIndex = myGlobalSettingIndex;
    setOptParamValue(myGlobalSettingIndex, -1);  //sets the local value to -1 and watch for update
    //setOptParamValue(myOptIndex, myLightSettingValue);  //store the value locally
    remPacket.command = OPT_PARAM_MODE; //prepare the next packet to update receiver's value
    remPacket.optParamCommand = GET_OPT_PARAM_VALUE;
    remPacket.optParamIndex = arrayIndex;
    remPacket.packOptParamValue(0); //(getOptParamValue(arrayIndex));
    requestSendOptParamPacket = true;    //send the value to the receiver
    for (int i=0; i<25 ; i++){  //waits until the local value has been updated. Abort if delay is more than 125ms.
        delay(5);
        if (getOptParamValue(myGlobalSettingIndex) != -1) {
            return true;
        }
    }
    requestSendOptParamPacket = true; //try again once if not received
    for (int i=0; i<40 ; i++){  //waits until the local value has been updated. Abort if delay is more than 200ms.
        delay(5);
        if (getOptParamValue(myGlobalSettingIndex) != -1) {
            return true;
        }
    }
    return false;
}


/*
//  TODO : LOAD ALL SETTINGS FROM RECEIVER FLASH MEMORY AT STARTUP and update localOptParamValueArray[] and local variables

// ******************* DOWNLOAD RECEIVER SETTINGS AT STARTUP *******************            
if (retrieveAllOptParamFromReceiverAtStartup == true){
retrieveAllOptParamFromReceiver();
retrieveAllOptParamFromReceiverAtStartup = false;}
// ******************* DOWNLOAD RECEIVER SETTINGS AT STARTUP *******************


// ******************* DOWNLOAD RECEIVER SETTINGS AT STARTUP *******************
if (requestSendOptParamPacket){   //only in MENU mode
    debug("requestSendOptParamPacket");
    remPacket.command = OPT_PARAM_MODE;
    requestSendOptParamPacket = false;
    break;
}
// ******************* DOWNLOAD RECEIVER SETTINGS AT STARTUP *******************
*/
void retrieveAllOptParamFromReceiver(){
    if(millisSince(loadParamTimestamp) > 300){ //will retrieve one parameter each 300ms
        loadParamTimestamp = millis();
        // we only need to sync these parameters when the remote is powered on : 
        switch(loadedParamCount){
            case 0:
                if (loadOptParamFromReceiver(IDX_REMOTE_RX_TIMEOUT)){
                    REMOTE_RX_TIMEOUT = localOptParamValueArray[IDX_REMOTE_RX_TIMEOUT];
                    loadedParamCount ++;}
            break;
            case 1:
                if (loadOptParamFromReceiver(IDX_REMOTE_RADIOLOOP_DELAY)){
                    REMOTE_RADIOLOOP_DELAY = localOptParamValueArray[IDX_REMOTE_RADIOLOOP_DELAY];
                    loadedParamCount ++;}
            break;
            case 2:
                if (loadOptParamFromReceiver(IDX_REMOTE_LOCK_TIMEOUT)){
                    REMOTE_LOCK_TIMEOUT = localOptParamValueArray[IDX_REMOTE_LOCK_TIMEOUT];
                    loadedParamCount ++;}
            break;
            case 3:
                if (loadOptParamFromReceiver(IDX_REMOTE_SLEEP_TIMEOUT)){
                    REMOTE_SLEEP_TIMEOUT = localOptParamValueArray[IDX_REMOTE_SLEEP_TIMEOUT];
                    loadedParamCount ++;}
            break;
            case 4:
                if (loadOptParamFromReceiver(IDX_DISPLAY_BATTERY_MIN)){
                    DISPLAY_BATTERY_MIN = localOptParamValueArray[IDX_DISPLAY_BATTERY_MIN];
                    loadedParamCount ++;}
            break;
            case 5:
                if (loadOptParamFromReceiver(IDX_MOTOR_MIN)){
                    MOTOR_MIN = localOptParamValueArray[IDX_MOTOR_MIN];
                    loadedParamCount ++;}
            break;
            case 6:
                if (loadOptParamFromReceiver(IDX_MOTOR_MAX)){
                    MOTOR_MAX = localOptParamValueArray[IDX_MOTOR_MAX];
                    loadedParamCount ++;}
            break;
            case 7:
                if (loadOptParamFromReceiver(IDX_BATTERY_MIN)){
                    BATTERY_MIN = localOptParamValueArray[IDX_BATTERY_MIN];
                    loadedParamCount ++;}
            break;
            case 8:
                if (loadOptParamFromReceiver(IDX_BATTERY_MAX)){
                    BATTERY_MAX = localOptParamValueArray[IDX_BATTERY_MAX];
                    loadedParamCount ++;}
            break;
            case 9:
                if (loadOptParamFromReceiver(IDX_LIMITED_SPEED_MAX)){
                    LIMITED_SPEED_MAX = localOptParamValueArray[IDX_LIMITED_SPEED_MAX];
                    loadedParamCount ++;}
            break;
        }
        //vibe(3);
        if(loadedParamCount<10){
            return;
        }
        else{
            retrieveAllOptParamFromReceiverAtStartup = false;
        }
    }
}


//***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********


// **************************************** LED ROADLIGHTS *****************************
void drawLightSettingsPage(){
    int deadBand = 15;
    uint8_t myOptIndex;
    int myLightSettingValue;
    int mySideLightColor = getOptParamValue(IDX_LED_SIDE_COLOR);
    int myLightBrightness = getOptParamValue(IDX_LED_BRIGHTNESS);
    int myBrakeLightBrightness = getOptParamValue(IDX_LED_BRIGHTNESS_BRAKE);

    switch (myRoadlightSetting_page_stage) {
        case ADJUSTING_SIDELIGHT_COLOR:
            myLightSettingValue = mySideLightColor;
            myOptIndex = IDX_LED_SIDE_COLOR;
        break;
        case ADJUSTING_LIGHT_BRIGHTNESS:
            myLightSettingValue = myLightBrightness;
            myOptIndex = IDX_LED_BRIGHTNESS;
        break;
        case ADJUSTING_BRAKELIGHT_BRIGHTNESS:
            myLightSettingValue = myBrakeLightBrightness;
            myOptIndex = IDX_LED_BRIGHTNESS_BRAKE;
        break;
    }

    int position = readThrottlePosition();
    int lastPositionIndex = myLightSettingValue;
    int nextPositionIndex = lastPositionIndex;
    // --------- wheel control ---------------------
    if (position > default_throttle + deadBand) {
        if (myLightSettingValue < 255){ myLightSettingValue = constrain((myLightSettingValue += 5),0,255);}
    }
    if (position < default_throttle - deadBand) {
        if (myLightSettingValue > 0){ myLightSettingValue = constrain((myLightSettingValue -= 5),0,255);}
    }
    nextPositionIndex = myLightSettingValue;
    if (lastPositionIndex != nextPositionIndex){
      vibrate(50); //short vibration each time we change the selected menu item
      setOptParamValue(myOptIndex, myLightSettingValue);  //store the value locally
      sendOptParamToReceiver(myOptIndex);
      }  
    // ---------------------------------------------
    //myLightSettingValue = nextPositionIndex;

    switch (myRoadlightSetting_page_stage) {
        case ADJUSTING_SIDELIGHT_COLOR:
            mySideLightColor = myLightSettingValue;
        break;
        case ADJUSTING_LIGHT_BRIGHTNESS:
            myLightBrightness = myLightSettingValue;
        break;
        case ADJUSTING_BRAKELIGHT_BRIGHTNESS:
            myBrakeLightBrightness = myLightSettingValue;
        break;
    }

    const int gap = 20;

    int x = 5;
    int y = 48;
    float value;
    int bars;
    //bool isHighlighted
    drawHLine(2, y, 64-2);
        bars = map(mySideLightColor, 0, 255, 0, 10);
        drawBars_2(x, y, bars, String(bars), "Color", (myRoadlightSetting_page_stage==ADJUSTING_SIDELIGHT_COLOR));

        x += gap;
        bars = map(myLightBrightness, 0, 255, 0, 10);
        drawBars_2(x, y, bars, String(bars), "Brightness", (myRoadlightSetting_page_stage==ADJUSTING_LIGHT_BRIGHTNESS));

        x += gap;
        bars = map(myBrakeLightBrightness, 0, 255, 0, 10);
        drawBars_2(x, y, bars, String(bars), "Brakes", (myRoadlightSetting_page_stage==ADJUSTING_BRAKELIGHT_BRIGHTNESS));


    if (triggerButton.getState() == CLICK) {
        // apply calibration values
        //waitRelease(PIN_TRIGGER);
        switch (myRoadlightSetting_page_stage) {
            case ADJUSTING_SIDELIGHT_COLOR:
                SIDELIGHT_COLOR = mySideLightColor;
                myRoadlightSetting_page_stage = ADJUSTING_LIGHT_BRIGHTNESS;
            break;
            case ADJUSTING_LIGHT_BRIGHTNESS:
                LIGHT_BRIGHTNESS = myLightBrightness;
                myRoadlightSetting_page_stage = ADJUSTING_BRAKELIGHT_BRIGHTNESS;
            break;
            case ADJUSTING_BRAKELIGHT_BRIGHTNESS:
                BRAKELIGHT_BRIGHTNESS = myBrakeLightBrightness;
                myRoadlightSetting_page_stage = ADJUSTING_SIDELIGHT_COLOR;   // OR backToMainMenu();
            break;

            //backToMainMenu();
            //display.setRotation(DISPLAY_ROTATION);
            //saveSettings()
            //return;
        }
    }


    // FET & motor temperature
    //    drawString(String(telemetry.tempFET) + " C    "
    //    + String(telemetry.tempMotor) + " C", -1, 114, fontPico);

}// **************************************** LED ROADLIGHTS *****************************



// e.g. paramValueSelector(IDX_AUTO_BRAKE_RELEASE, "Auto brake delay", -1000,+1000,0.1,1,"s");
void paramValueSelector(uint8_t myGlobalSettingIndex, String paramName, double minAdjValue, double maxAdjValue, double adjIncrement, int decimalPlace, String unitStr, String label ){
    //debugButtons();
    if (initFlag_PVS == 1){
        //currentParamAdjValue = -99875.98354; //random value -> wait for change
        currentParamAdjValue = (double) (getOptParamValue(myGlobalSettingIndex));
        /*while(currentParamAdjValue == -99875.98354){
            vTaskDelay(10);
        }*/
        saveParamAdjValue = currentParamAdjValue;
        myPVSpage = ADJUST_PVS_VALUE;
        waitTimeMs = 0;
        initFlag_PVS = 0;
    }

    int deadBand = 20;
    long timestamp = millis();
    while (millisSince(timestamp) < waitTimeMs){
            /*if (triggerButton.getState() == CLICK) {
                //waitRelease(PIN_TRIGGER);
                waitTimeMs = 0;
                break;
            }
            if (powerButton.getState() == CLICK) {
                initFlag_PVS = 1;
                waitTimeMs = 0;
            backToMainMenu();                
                break;
            }*/
            vTaskDelay(5);
    }

    int position = readThrottlePosition();
    double lastPositionValue = currentParamAdjValue;
    double nextPositionValue = lastPositionValue;

    int y = 12;
    drawHLine(2, y, 64-2);
    drawString("Adj. setting", 0, y-2, fontMicro);
    y=28;
    drawString(paramName, 0, y, fontDesc);
    
    switch (myPVSpage){
        case ADJUST_PVS_VALUE:
            waitTimeMs = 0;
            // --------- wheel control ---------------------
            if (position > default_throttle + deadBand) {
                if (currentParamAdjValue < maxAdjValue){ currentParamAdjValue = constrain((currentParamAdjValue + adjIncrement), minAdjValue, maxAdjValue);}
                waitTimeMs = constrain( ( 3000 / pow( (double)(abs(position - default_throttle)/5), 1.8) - deadBand ), 0, 500);
            }
            if (position < default_throttle - deadBand) {
                if (currentParamAdjValue > minAdjValue){ currentParamAdjValue = constrain((currentParamAdjValue - adjIncrement), minAdjValue, maxAdjValue);}
                waitTimeMs = constrain( ( 3000 / pow( (double)(abs(position - default_throttle)/5), 1.8) - deadBand ), 0, 500);
            }
            saveParamAdjValue = currentParamAdjValue;
            drawString(label, 0, 44, fontMicro);
            y = 80;
            drawStringCenter(String(currentParamAdjValue, decimalPlace), unitStr, y);
            y = 105;
            drawString("SAVE", 2, y, fontPico);
            y = 115;
            drawString("CANCEL", 2, y, fontPico);

        break;
        case SAVE_PVS_VALUE:
            // --------- wheel control ---------------------
            if (position < default_throttle - deadBand) {
                myPVSpage = CANCEL_PVS_VALUE;
            }
            y = 80;
            drawStringCenter(String(saveParamAdjValue, decimalPlace), unitStr, y);
            y = 105;
            drawString("> SAVE", 0, y, fontPico);
            y = 115;
            drawString("CANCEL", 2, y, fontPico);
        break;
        case CANCEL_PVS_VALUE:
            if (position > default_throttle + deadBand) {
                myPVSpage = SAVE_PVS_VALUE;
            }
            y = 80;
            drawStringCenter(String(saveParamAdjValue, decimalPlace), unitStr, y);
            y = 105;
            drawString("SAVE", 2, y, fontPico);
            y = 115;
            drawString("> CANCEL", 0, y, fontPico);
        break;

    }

    nextPositionValue = currentParamAdjValue;
    if (lastPositionValue != nextPositionValue){
      vibrate(50); //short vibration each time we change the selected menu item
      }  

    if (triggerButton.getState() == CLICK) {
        waitTimeMs = 0;
        switch (myPVSpage){
            case ADJUST_PVS_VALUE:
                myPVSpage = SAVE_PVS_VALUE;
                keepAlive();
            break;
            case SAVE_PVS_VALUE:
                vibe(2);
                setOptParamValue(myGlobalSettingIndex, (float)saveParamAdjValue);  //store the value locally
                sendOptParamToReceiver(myGlobalSettingIndex);
                myPVSpage = ADJUST_PVS_VALUE;
                initFlag_PVS = 1;
                backToMainMenu();
            break;
            case CANCEL_PVS_VALUE:
                vibe(5);
                myPVSpage = ADJUST_PVS_VALUE;
                initFlag_PVS = 1;
                backToMainMenu();
            break;
        }
    }
    if (powerButton.getState() == CLICK) {
        vibe(5);
        waitTimeMs = 0;
        myPVSpage = ADJUST_PVS_VALUE;
        initFlag_PVS = 1;
        backToMainMenu();
        //currentMenu = 0;
    }   

}


// *********************************************************************************************************************************************************************
// *********************************************************************************************************************************************************************
// displays a list of parameters and scrolls with throttle input. Trigger click launch paramValueSelector to adjust currently selected parameter
void paramSelectorList(int *paramSelectorIndexArray){
    //(uint8_t myGlobalSettingIndex, String paramName, double minAdjValue, double maxAdjValue, double adjIncrement, int decimalPlace, String unitStr, String label ){
    int myArraySize = sizeof(paramSelectorIndexArray)/sizeof(paramSelectorIndexArray[0]);
    myArraySize = IDX_ENDOFARRAY;


    if (initFlag_PSL == 1){
        myPSLpage = ADJUST_PSL_VALUE;
        currentParamSelectorValue = 0;
        paramSelector_selected = 0;
        initFlag_PSL = 0;
        waitTimeMs_PSL = 0;
    }

    int adjIncrement = 1;
    int minAdjValue = 0;
    int maxAdjValue = myArraySize - 1;

    int deadBand = 20;
    long timestamp = millis();
    while (millisSince(timestamp) < waitTimeMs_PSL){
            /*if (triggerButton.getState() == CLICK) {
                //waitRelease(PIN_TRIGGER);
                waitTimeMs_PSL = 0;
                break;
            }
            if (powerButton.getState() == CLICK) {
                initFlag_PSL = 1;
                waitTimeMs_PSL = 0;
                break;
            }*/
        vTaskDelay(2);
    }

    int position = readThrottlePosition();
    double lastPositionValue = currentParamSelectorValue;
    double nextPositionValue = lastPositionValue;

    int x = 0;
    int y = 12;
    drawHLine(2, y, 64-2);
    drawString("Adj. setting", 0, y-2, fontMicro);

    switch (myPSLpage){
        case ADJUST_PSL_VALUE:
            waitTimeMs_PSL = 0;
            // --------- wheel control ---------------------
            if (position < default_throttle - deadBand) {
                if (currentParamSelectorValue < maxAdjValue){ currentParamSelectorValue = constrain((currentParamSelectorValue + adjIncrement), minAdjValue, maxAdjValue);}
                waitTimeMs_PSL = constrain( ( 3000 / pow( (double)(abs(position - default_throttle)/5), 1.8) - deadBand ), 0, 500);
            }
            if (position > default_throttle + deadBand) {
                if (currentParamSelectorValue > minAdjValue){ currentParamSelectorValue = constrain((currentParamSelectorValue - adjIncrement), minAdjValue, maxAdjValue);}
                waitTimeMs_PSL = constrain( ( 3000 / pow( (double)(abs(position - default_throttle)/5), 1.8) - deadBand ), 0, 500);
            }
            paramSelector_selected = currentParamSelectorValue;
            x = 0; 
            y = 20;
            //if (paramSelector_selected > 1){drawString(String(paramSelectorNameArrray[paramSelector_selected - 2]), x, y, fontDesc); }
            if (paramSelector_selected > 1){drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected - 2]]), x, y, fontMicro); }    
            y = 35;
            if (paramSelector_selected > 0){drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected - 1]]), x, y, fontMicro); }
            y = 55;
            drawString("> "+String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected]]), x, y, fontMicro);
            y = 75;
            if (paramSelector_selected < (myArraySize - 1) ){drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected + 1]]), x, y, fontMicro); }
            y = 90;
            if (paramSelector_selected < (myArraySize - 2) ){drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected + 2]]), x, y, fontMicro); }

            //x = 56; y = 115;
            //drawString(String(myArraySize), x, y, fontPico);   //DEBUG

            x = 2; y = 126;
            drawString("EDIT", x, y, fontPico);
            x = 28; y = 126;
            drawString("CANCEL", x, y, fontPico);
        break;
        case SAVE_PSL_VALUE:
            // --------- wheel control ---------------------
            if (position < default_throttle - deadBand) {
                myPSLpage = CANCEL_PSL_VALUE;
            }
            y = 60;
            drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected]]), 2, y, fontDesc);
            x = 0; y = 126;
            drawString("> EDIT", x, y, fontPico);
            x = 28; y = 126;
            drawString("CANCEL", x, y, fontPico);            
        break;
        case CANCEL_PSL_VALUE:
            if (position > default_throttle + deadBand) {
                myPSLpage = SAVE_PSL_VALUE;
            }
            y = 60;
            drawString(String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected]]), 2, y, fontDesc);
            x = 2; y = 126;
            drawString("EDIT", x, y, fontPico);
            x = 26; y = 126;
            drawString("> CANCEL", x, y, fontPico);            
        break;
        case DISPLAY_PVS_PAGE:
            waitTimeMs_PSL = 0;
            paramValueSelector(paramSelectorIndexArray[paramSelector_selected], String(GlobalSettingsStringName[paramSelectorIndexArray[paramSelector_selected]]), -255,255,0.5,1,"");           
        break;
    }

    nextPositionValue = currentParamSelectorValue;
    if (lastPositionValue != nextPositionValue){
      vibrate(50); //short vibration each time we change the selected menu item
      }  

    if (triggerButton.getState() == CLICK) {
        // apply calibration values
        //waitRelease(PIN_TRIGGER);
        waitTimeMs_PSL = 0;
        switch (myPSLpage){
            case ADJUST_PSL_VALUE:
                myPSLpage = SAVE_PSL_VALUE;
            break;
            case SAVE_PSL_VALUE:
                loadOptParamFromReceiver(paramSelectorIndexArray[paramSelector_selected]);
                vibe(5);
                myPSLpage = DISPLAY_PVS_PAGE;
            break;
            case CANCEL_PSL_VALUE:
                vibe(5);
                myPSLpage = ADJUST_PSL_VALUE;
            break;
            case DISPLAY_PVS_PAGE:
            break;
        }
    }
    if (powerButton.getState() == CLICK) {
        vibe(5);
        waitTimeMs_PSL = 0;
        myPSLpage = ADJUST_PSL_VALUE;
        initFlag_PSL = 1;
        backToMainMenu();
        //currentMenu = 0;
    }   

}

void speedLimiter(bool state){  //activate or deactivate the speed limiter
    speedLimiterState = state;
    requestSpeedLimiter = true;
    if (speedLimiterState){
        vibe(2);
    }
}