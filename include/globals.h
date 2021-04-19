#ifndef _CONST_H
#define _CONST_H

#include <Arduino.h>
#include <datatypes.h>

// ********** OPTIONAL FEATURES ***********************************************

//#define FAKE_UART                         // Comment out after pairing the remote and connecting VESC
//#define DEBUG                             // Uncomment DEBUG if you need to debug the remote / receiver
//const uint32_t boardAddress = 0xA9BF713C;
//#include <analogWrite.h>
#define ROADLIGHT_CONNECTED                 // FRONT LIGHT and BACKLIGHT option. Reconfigure 2 pins on the receiver side for FRONTLIGHT and BACKLIGHT PWM signal
//#define OUTPUT_PPM_THROTTLE               // include receiver functions to be able to output a THROTTLE PPM/PWM signal on PIN_PPM_THROTTLE (Menu:Receiver->AppMode)
//#define EXPERIMENTAL                      // Implements experimental functionalities that might contain some bugs
//#define BT_ENABLED

// ********** * * * * * * * * * ***********************************************
static bool inverse_speed_direction = false;   //change if speed is negative when going forwards

enum VescThrottleMode{
    VTM_NUNCHUCK_UART,
    VTM_PPM_PIN_OUT,
    VTM_CURRENT_UART,
    VTM_RPM_UART,
    VTM_DUTY_UART,
    VTM_REGEN_UART,
    VTM_HANDBRAKE_UART,
    VTM_POS_UART,
    VTM_ENUM_END
};

const COMM_PACKET_ID VESC_COMMAND = COMM_GET_VALUES; // VESC
// const COMM_PACKET_ID VESC_COMMAND = COMM_GET_UNITY_VALUES; // Enertion Unity

//static int RECEIVER_VESC_COMMAND = VESC_COMMAND;
/*
typedef enum {
	COMM_GET_VALUES = 4,
	COMM_SET_DUTY = 5,
	COMM_SET_CURRENT = 6,
	COMM_SET_CURRENT_BRAKE = 7,
	COMM_SET_HANDBRAKE = 10,
	COMM_REBOOT = 29,
	COMM_ALIVE = 30,
	COMM_GET_DECODED_PPM = 31,
  COMM_GET_UNITY_VALUES = 38
} COMM_PACKET_ID;
*/

/* AUTOCRUISE SETTINGS
  Endless ride - when remote is off and speed is over 12 km/h for 3 seconds,
  cruise control will be activated when speed drops below 12 km/h.

  Slide the board backwards while standing on it or foot brake
  to produce a spike in the current and stop the board.
*/
static bool  AUTO_CRUISE_ON = false;     // disabled by default
static float PUSHING_SPEED = 10.0;       // km/h
static float PUSHING_TIME = 3.0;         // seconds
static float CRUISE_CURRENT_SPIKE = 5.0; // Amps

// boad will stop after 30s if current is low
static float AUTO_CRUISE_TIME = 30.0;    // seconds
static float CRUISE_CURRENT_LOW = 5.0;   // Amps

// auto stop if remote is off and speed is over 20 km/h
static float MAX_PUSHING_SPEED = 15.0;   // km/h

// Auto stop (in seconds)
static float AUTO_BRAKE_TIME = 10;    // time to apply the full brakes
static int AUTO_BRAKE_RELEASE = 3;     // time to release brakes after the full stop
static float AUTO_BRAKE_ABORT_MAXSPEED = 3; // speed under which it's safe to abort auto brake procedure

// UART
static int UART_SPEED = 115200;
//const int UART_SPEED = 9600;
static uint16_t uartPullInterval = 25;
static int UART_TIMEOUT = 10; // 10ms for 115200 bauds, 100ms for 9600 bauds (-->VescUart::setTimeout())
static int REMOTE_RX_TIMEOUT = 25; //ms (about 10-15ms needed to parse a packet)
static int REMOTE_RADIOLOOP_DELAY = 50; //ms sending THROTTLE each xx millisecond to the receiver
static int REMOTE_LOCK_TIMEOUT = 10; // seconds to lock throttle when idle
static int REMOTE_SLEEP_TIMEOUT = 180; // seconds to go to sleep mode
// turn off display if battery < 15%
static int DISPLAY_BATTERY_MIN = 15;// #### change to 0 if remote screen doesnt turn ON, then calibrate battery voltage reading ####
// VESC current, for graphs only
static int MOTOR_MIN = -60;
static int MOTOR_MAX = 60;
static int BATTERY_MIN = -40;
static int BATTERY_MAX = 40;
// default board configuration
static int MAX_SPEED = 40;       // KM/H
static int MAX_RANGE = 25;       // KM
static int BATTERY_CELLS = 6;
static int BATTERY_TYPE = 1;     // 0: LI-ION | 1: LIPO
static int MOTOR_POLES = 14;
static int WHEEL_DIAMETER = 100;
static int WHEEL_PULLEY = 50;
static int MOTOR_PULLEY = 11;
//LED roadlights
static int LED_SIDE_COLOR = 160;
static int SIDE_THROTTLE_COLOR = 0;
static int Rainbow_speed = 0; 
static int LED_BRIGHTNESS = 20;
static int LED_BRIGHTNESS_BRAKE = 255;
static int LED_BRIGHTNESS_OFF = 0;
static int LED_ROADLIGHT_MODE = 0;
//Remote APP
static int THROTTLE_MODE = VTM_NUNCHUCK_UART;   //Default 0=UART.Nunchuck / 1=PPM 
static double LIMITED_SPEED_MAX = 40.0;   //kmh

enum RoadLightState{
    OFF,
    ON,
    SIDE,
    SIDE_THROTTLE,
    SIDE_RAINBOW,
    BRAKES_ONLY
};

#define VERSION 3
// Remote > receiver
struct RemotePacket {
    uint32_t address;   //LoRa packets : max payload = 255bytes 
    // --------------   // keep 4 byte alignment!
    uint8_t  version;
    uint8_t  command;   // e.g. SET_THROTTLE
    uint8_t  data;      // e.g. throttle value
    uint8_t  counter;
    // --------------

    //***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
    int8_t  optParamCommand;
    int8_t  optParamIndex;
    int16_t optParamValue;

    int16_t f2wi(float f) { return f * 100; } // pack float
    float w2fi(int16_t w) { return float(w) / 100; }; // unpack float

    float unpackOptParamValue() { return w2fi(optParamValue); }
    void packOptParamValue(float f) { optParamValue = f2wi(f); }
}; //end struct declaration

// RemotePacket.COMMANDS :
enum RemotePacketCommand {
    SET_THROTTLE,
    SET_CRUISE,
    GET_CONFIG,
    SET_STATE,
    SET_LIGHT,
    OPT_PARAM_MODE,
    SPEED_LIMITER
};

// RemotePacket.optParamCommand :
enum OptionParamCommand {
    SET_OPT_PARAM_VALUE,
    GET_OPT_PARAM_VALUE
};

//***********  OPT_PARAM - local array to store settings as (INDEX# and VALUE)pair  ***********
const uint8_t optionParamArrayLength = 64;
static float localOptParamValueArray[optionParamArrayLength];


// GlobalSettingsIndex sort all parameters by index for Tx <-> Rx exchange and storage in flash memory via Preferences library
enum GlobalSettingsIndex {
    IDX_MIN_HALL,//remote-CONFIGpacket
    IDX_CENTER_HALL,//remote-CONFIGpacket
    IDX_MAX_HALL,//remote-CONFIGpacket
    IDX_BOARD_ID,//remote-CONFIGpacket
    IDX_AUTO_CRUISE_ON,
    IDX_PUSHING_SPEED,
    IDX_PUSHING_TIME,
    IDX_CRUISE_CURRENT_SPIKE,
    IDX_AUTO_CRUISE_TIME,
    IDX_CRUISE_CURRENT_LOW,
    IDX_MAX_PUSHING_SPEED,
    IDX_AUTO_BRAKE_TIME,
    IDX_AUTO_BRAKE_RELEASE,
    IDX_AUTO_BRAKE_ABORT_MAXSPEED,
    IDX_UART_SPEED,
    IDX_uartPullInterval,
    IDX_UART_TIMEOUT,
    IDX_REMOTE_RX_TIMEOUT, //remote
    IDX_REMOTE_RADIOLOOP_DELAY,//remote
    IDX_REMOTE_LOCK_TIMEOUT,//remote
    IDX_REMOTE_SLEEP_TIMEOUT,//remote
    IDX_DISPLAY_BATTERY_MIN,//remote
    IDX_MOTOR_MIN,//remote
    IDX_MOTOR_MAX,//remote
    IDX_BATTERY_MIN,//remote
    IDX_BATTERY_MAX,//remote
    IDX_MAX_SPEED,
    IDX_MAX_RANGE,
    IDX_BATTERY_CELLS,
    IDX_BATTERY_TYPE,
    IDX_MOTOR_POLES,
    IDX_WHEEL_DIAMETER,
    IDX_WHEEL_PULLEY,
    IDX_MOTOR_PULLEY,
    IDX_LED_SIDE_COLOR,
    IDX_LED_BRIGHTNESS,
    IDX_LED_BRIGHTNESS_BRAKE,
    IDX_LED_BRIGHTNESS_OFF,
    IDX_LED_ROADLIGHT_MODE,
    IDX_THROTTLE_MODE,
    IDX_LIMITED_SPEED_MAX,
    
    IDX_ENDOFARRAY
};

static String GlobalSettingsStringName[] = {
    "MIN_HALL",//remote-CONFIGpacket
    "CENTER_HALL",//remote-CONFIGpacket
    "MAX_HALL",//remote-CONFIGpacket
    "BOARD_ID",//remote-CONFIGpacket
    "AUTO_CRUISE_ON",
    "PUSHSPEED",
    "PUSHING_TIME",
    "CRUISE_CURRENT_SPIKE",
    "AUTO_CRUISE_TIME",
    "CRUISE_CURRENT_LOW",
    "MAX_PUSHING_SPEED",
    "AUTO_BRAKE_TIME",
    "AUTO_BRAKE_RELEASE",
    "AUTO_BRAKE_ABORT_MAXSPEED",
    "UART_SPEED",
    "uartPullInterval",
    "UART_TIMEOUT",
    "REMOTE_RX_TIMEOUT", //remote
    "REMOTE_RADIOLOOP_DELAY",//remote
    "REMOTE_LOCK_TIMEOUT",//remote
    "REMOTE_SLEEP_TIMEOUT",//remote
    "DISPLAY_BATTERY_MIN",//remote
    "MOTOR_MIN",//remote
    "MOTOR_MAX",//remote
    "BATTERY_MIN",//remote
    "BATTERY_MAX",//remote
    "MAX_SPEED",
    "MAX_RANGE",
    "BATTERY_CELLS",
    "BATTERY_TYPE",
    "MOTOR_POLES",
    "WHEEL_DIAMETER",
    "WHEEL_PULLEY",
    "MOTOR_PULLEY",
    "LED_SIDE_COLOR",
    "LED_BRIGHTNESS",
    "LED_BRIGHTNESS_BRAKE",
    "LED_BRIGHTNESS_OFF",
    "LED_ROADLIGHT_MODE",
    "THROTTLE_MODE",
    "LIMITED_SPEED_MAX"
};


// state machine
enum AppState {
    IDLE,       // remote is not connected
    NORMAL,
    PUSHING,
    CRUISE,
    ENDLESS,
    CONNECTED,  // riding with connected remote
    CONNECTING,
    MENU,
    STOPPING,   // emergency brake when remote has disconnected
    STOPPED,
    PAIRING,
    UPDATE,     // update over WiFi
    BTCOM,
    COASTING    // waiting for board to slowdown
}; //end enum declaration

// Receiver > remote  3 bytes
struct ReceiverPacket {
    uint8_t type;
    uint8_t chain;	// CRC from RemotePacket
    uint8_t state;   // Mode: Pairing, BT, ...
    uint8_t r2;

    // int32_t testBigPacket;//test transmission with biggerPackets

}; //end struct declaration

// responses type
const uint8_t ACK_ONLY  = 1;
const uint8_t TELEMETRY = 2;
const uint8_t CONFIG    = 3;
const uint8_t BOARD_ID  = 4;
const uint8_t OPT_PARAM_RESPONSE  = 5;

struct InfoPacket {  //extends ReceiverPacket
    ReceiverPacket header;
    // --------------  // keep 4 byte alignment!
    int32_t id;
    // --------------
    uint8_t r0;
    uint8_t r1;
    uint16_t r2;
    // --------------
    uint16_t r3;
    uint16_t r4;
  // --------------
}; //end struct declaration

const int PACKET_SIZE = sizeof(InfoPacket);
const int CRC_SIZE = 1;

// New VESC values 12 + 3
struct TelemetryPacket{ //extends ReceiverPacket

    ReceiverPacket header;
    // -----------------  // keep 4 byte alignment!
//  uint16_t speed;       // km/h * 100
    int16_t speed;       // km/h * 100
    uint8_t tempMotor;
    uint8_t tempFET;
    // -----------------
    uint16_t voltage;     // volts * 100
    uint16_t distance;    // km * 100 - 10m accuracy
    // -----------------
    int16_t motorCurrent; // motor amps * 100
    int16_t inputCurrent; // battery amps * 100
    // -----------------
    //
    uint16_t f2w(float f) { return f * 100; } // pack float
    float w2f(uint16_t w) { return float(w) / 100; }; // unpack float
    int16_t f2wi(float f) { return f * 100; } // pack float
    float w2fi(int16_t w) { return float(w) / 100; }; // unpack float

    float getSpeed() { return w2fi(speed); }
    void setSpeed(float f) { speed = f2wi(f); }
 
    float getVoltage() { return w2f(voltage); }
    void setVoltage(float f) { voltage = f2w(f); }

    float getDistance() { return w2f(distance); }
    void setDistance(float f) { distance = f2w(f); }

    float getMotorCurrent() { return w2fi(motorCurrent); }
    void setMotorCurrent(float f) { motorCurrent = f2wi(f); }

    float getInputCurrent() { return w2fi(inputCurrent); }
    void setInputCurrent(float f) { inputCurrent = f2wi(f); }

};//end struct declaration

// board setting
struct ConfigPacket {  //extends ReceiverPacket
    ReceiverPacket header;
    // -------------------  // keep 4 byte alignment!
    uint8_t  maxSpeed;	    // m/s
    uint8_t  maxRange;      // km
    uint8_t  batteryCells;
    uint8_t  batteryType;   // 0: Li-ion | 1: LiPo
    // -------------------
    uint8_t  motorPoles;
    uint8_t  wheelDiameter;
    uint8_t  wheelPulley;
    uint8_t  motorPulley;
    // -------------------
    int16_t r1;  // battery amps * 100
    int16_t r2;
    // -------------------

    float getMaxSpeed() { return (maxSpeed) / 100; }
    void setMaxSpeed(float f) { maxSpeed = f * 100; }
};  //end struct declaration

struct OptionParamPacket {  //extends ReceiverPacket
    ReceiverPacket header;
    // --------------  // keep 4 byte alignment!
    //***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
    uint8_t  optParamCommand;
    uint8_t  optParamIndex;
    int16_t optParamValue;
    //------------------
    int16_t xx1;
    int16_t xx2;
    //------------------
    int16_t zz1;
    int16_t zz2;
    //------------------
    int16_t f2wi(float f) { return f * 100; } // pack float
    float w2fi(int16_t w) { return float(w) / 100; }; // unpack float

    float unpackOptParamValue() { return w2fi(optParamValue); }
    void packOptParamValue(float f) { optParamValue = f2wi(f); }
    //***********  VERSION 3 : OPT_PARAM Tx <-> Rx  ***********
}; //end struct declaration

const int default_throttle = 127;


#ifdef DEBUG
    static UBaseType_t debugTaskSize = 0;
    #define debug(x) Serial.println (x)
#else
    #define debug(x)
#endif

#endif //_CONST_H
