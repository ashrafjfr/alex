#include <stdarg.h>
#include <serialize.h>
#include <math.h>
#include <avr/sleep.h>
/*
  #include <avr/io.h>
  #include <avr/interrupt.h>
*/
#include "packet.h"
#include "constants.h"

#include "Adafruit_TCS34725.h"
#include <Wire.h>

/*
   Alex's configuration constants
*/
typedef enum
{
  STOP = 0,
  FORWARD = 1,
  BACKWARD = 2,
  LEFT = 3,
  RIGHT = 4
} TDirection;

volatile TDirection dir = STOP;

// Number of ticks per revolution from the
// wheel encoder.

#define COUNTS_PER_REV      175

// Wheel circumference in cm.
// We will use this to calculate forward/backward distance traveled
// by taking revs * WHEEL_CIRC

#define WHEEL_CIRC          20.4

// Motor control pins. You need to adjust these till
// Alex moves in the correct direction
#define LF                  6   // Left forward pin
#define LR                  5   // Left reverse pin
#define RF                  10  // Right forward pin
#define RR                  11  // Right reverse pin

// Alex Dimensions
#define ALEX_LENGTH 21
#define ALEX_BREADTH 12
//#define PI 3.141592654

// Mask Definitions
#define PRR_TWI_MASK 0b10000000
#define PRR_SPI_MASK 0b00000100
#define ADCSRA_ADC_MASK 0b10000000
#define PRR_ADC_MASK 0b00000001
#define PRR_TIMER2_MASK 0b01000000
#define PRR_TIMER0_MASK 0b00100000
#define PRR_TIMER1_MASK 0b00001000
#define SMCR_SLEEP_ENABLE_MASK 0b00000001
#define SMCR_IDLE_MODE_MASK 0b11110001

// Alex's diagonal and turning circumference. Computed and stored once.
float AlexDiagonal = 0.0;
float AlexCirc = 0.0;

/*
      Alex's State Variables
*/

// Store the ticks from Alex's left and
// right encoders.
volatile unsigned long leftForwardTicks;
volatile unsigned long rightForwardTicks;
volatile unsigned long leftReverseTicks;
volatile unsigned long rightReverseTicks;

volatile unsigned long leftForwardTicksTurns;
volatile unsigned long rightForwardTicksTurns;
volatile unsigned long leftReverseTicksTurns;
volatile unsigned long rightReverseTicksTurns;

// Store the revolutions on Alex's left
// and right wheels
volatile unsigned long leftRevs;
volatile unsigned long rightRevs;

// Forward and backward distance traveled
volatile unsigned long forwardDist;
volatile unsigned long reverseDist;

// Variables to keep track of whether we have moved a comanded distance
unsigned long deltaDist;
unsigned long newDist;

// Variables to keep track of our turning angles.
unsigned long deltaTicks;
unsigned long targetTicks;

/*
    Alex's Environmental Info.
*/
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_16X);
//data type depends
volatile unsigned long color;
unsigned long flame;
unsigned long gas;

/*
   Alex Communication Routines.
*/

TResult readPacket(TPacket *packet)
{
  // Reads in data from the serial port and
  // deserializes it.Returns deserialized
  // data in "packet".

  char buffer[PACKET_SIZE];
  int len;

  len = readSerial(buffer);

  if (len == 0)
    return PACKET_INCOMPLETE;
  else
    return deserialize(buffer, len, packet);

}

void WDT_off(void)
{
  /* Global interrupt should be turned OFF here if not
    already done so */
  /* Clear WDRF in MCUSR */
  MCUSR &= ~(1 << WDRF);
  /* Write logical one to WDCE and WDE */
  /* Keep old prescaler setting to prevent unintentional
    time-out */
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  /* Turn off WDT */
  WDTCSR = 0x00;
  /* Global interrupt should be turned ON here if
    subsequent operations after calling this function DO
    NOT require turning off global interrupt */
}

void setupPowerSaving()
{
  //Turn off Watchdog Timer
  WDT_off();

  //Modify PRR to shut down TWI
  //PRR |= PRR_TWI_MASK;

  //Modify PRR to shut down SPI
  PRR |= PRR_SPI_MASK;

  //Modify ADCSRA to disable ADC,
  //then modify PRR to shut down ADC
  ADCSRA |= ADCSRA_ADC_MASK;
  PRR |= PRR_ADC_MASK;

  //Set the SMCR to choose the IDLE sleep mode
  //DO not set the Sleep Enable (SE) bit yet
  SMCR &= 0b11110001;

  //Set Port B Pin 5 as output pin, then write a logic LOW
  // to it so that the LED tied to Arduino's Pin 13 is OFF.
  DDRB |= 0b00100000;
  PORTB &= 0b11011111;
}

void putArduinoToIdle()
{
  // Modify PRR to shut down TIMER 0, 1, and 2
  PRR |= PRR_TIMER2_MASK;
  PRR |= PRR_TIMER0_MASK;
  PRR |= PRR_TIMER1_MASK;

  // Modify SE bit in SMCR to enable (i.e., allow) sleep
  SMCR |= SMCR_SLEEP_ENABLE_MASK;

  // The following function puts ATmega328P’s MCU into sleep;
  // it wakes up from sleep when USART serial data arrives
  sleep_cpu();
  
  // Modify SE bit in SMCR to disable (i.e., disallow) sleep
  SMCR &= SMCR_IDLE_MODE_MASK;
  
  // Modify PRR to power up TIMER 0, 1, and 2
  PRR &= ~PRR_TIMER2_MASK;
  PRR &= ~PRR_TIMER0_MASK;
  PRR &= ~PRR_TIMER1_MASK;
}

void sendStatus()
{
  // Implement code to send back a packet containing key
  // information like leftTicks, rightTicks, leftRevs, rightRevs
  // forwardDist and reverseDist
  // Use the params array to store this information, and set the
  // packetType and command files accordingly, then use sendResponse
  // to send out the packet. See sendMessage on how to use sendResponse.
  //
  TPacket statusPacket;
  statusPacket.packetType = PACKET_TYPE_RESPONSE;
  statusPacket.command = RESP_STATUS;
  statusPacket.params[0] = leftForwardTicks;
  statusPacket.params[1] = rightForwardTicks;
  statusPacket.params[2] = leftReverseTicks;
  statusPacket.params[3] = rightReverseTicks;
  statusPacket.params[4] = leftForwardTicksTurns;
  statusPacket.params[5] = rightForwardTicksTurns;
  statusPacket.params[6] = leftReverseTicksTurns;
  statusPacket.params[7] = rightReverseTicksTurns;
  statusPacket.params[8] = forwardDist;
  statusPacket.params[9] = reverseDist;
  //  getColor();
  //  statusPacket.params[10] = color;
  //flame = probablyA5
  //gas = probablydigitalsomething
  //statusPacket.params[11] = flame;
  //statusPacket.params[12] = gas;
  sendResponse(&statusPacket);
}

void sendColor()
{
  TPacket statusPacket;
  statusPacket.packetType = PACKET_TYPE_RESPONSE;
  statusPacket.command = RESP_COLOR;
  statusPacket.params[0] = color;
  statusPacket.params[1] = 0;
  statusPacket.params[2] = 0;
  statusPacket.params[3] = 0;
  statusPacket.params[4] = 0;
  statusPacket.params[5] = 0;
  statusPacket.params[6] = 0;
  statusPacket.params[7] = 0;
  statusPacket.params[8] = 0;
  statusPacket.params[9] = 0;
  //flame = probablyA5
  //gas = probablydigitalsomething
  //statusPacket.params[11] = flame;
  //statusPacket.params[12] = gas;
  sendResponse(&statusPacket);
}

void sendMessage(const char *message)
{
  // Sends text messages back to the Pi. Useful
  // for debugging.

  TPacket messagePacket;
  messagePacket.packetType = PACKET_TYPE_MESSAGE;
  strncpy(messagePacket.data, message, MAX_STR_LEN);
  sendResponse(&messagePacket);
}

void dbprint(char *format, ...)
{
  va_list args;
  char buffer[128];

  va_start(args, format);
  vsprintf(buffer, format, args);
  sendMessage(buffer);
}

void sendBadPacket()
{
  // Tell the Pi that it sent us a packet with a bad
  // magic number.

  TPacket badPacket;
  badPacket.packetType = PACKET_TYPE_ERROR;
  badPacket.command = RESP_BAD_PACKET;
  sendResponse(&badPacket);

}

void sendBadChecksum()
{
  // Tell the Pi that it sent us a packet with a bad
  // checksum.

  TPacket badChecksum;
  badChecksum.packetType = PACKET_TYPE_ERROR;
  badChecksum.command = RESP_BAD_CHECKSUM;
  sendResponse(&badChecksum);
}

void sendBadCommand()
{
  // Tell the Pi that we don't understand its
  // command sent to us.

  TPacket badCommand;
  badCommand.packetType = PACKET_TYPE_ERROR;
  badCommand.command = RESP_BAD_COMMAND;
  sendResponse(&badCommand);

}

void sendBadResponse()
{
  TPacket badResponse;
  badResponse.packetType = PACKET_TYPE_ERROR;
  badResponse.command = RESP_BAD_RESPONSE;
  sendResponse(&badResponse);
}

void sendOK()
{
  TPacket okPacket;
  okPacket.packetType = PACKET_TYPE_RESPONSE;
  okPacket.command = RESP_OK;
  sendResponse(&okPacket);
}

void sendResponse(TPacket *packet)
{
  // Takes a packet, serializes it then sends it out
  // over the serial port.
  char buffer[PACKET_SIZE];
  int len;

  len = serialize(buffer, packet, sizeof(TPacket));
  writeSerial(buffer, len);
}


  
/*
   Setup and start codes for external interrupts and
   pullup resistors.

*/
// Enable pull up resistors on pins 2 and 3
void enablePullups()
{
  // Use bare-metal to enable the pull-up resistors on pins
  // 2 and 3. These are pins PD2 and PD3 respectively.
  // We set bits 2 and 3 in DDRD to 0 to make them inputs.

  DDRD &= 0b11110011;
  PIND |= 0b00001100;

}

// Functions to be called by INT0 and INT1 ISRs.
void leftISR()
{
  switch (dir) {
    case FORWARD:
      leftForwardTicks++;
      forwardDist = (unsigned long) ((float) leftForwardTicks / COUNTS_PER_REV * WHEEL_CIRC);
      break;
    case BACKWARD:
      leftReverseTicks++;
      reverseDist = (unsigned long) ((float) leftReverseTicks / COUNTS_PER_REV * WHEEL_CIRC);
      break;
    case LEFT:
      leftReverseTicksTurns++;
      break;
    case RIGHT:
      leftForwardTicksTurns++;
      break;
  }
}

void rightISR()
{
  switch (dir) {
    case FORWARD:
      rightForwardTicks++;
      break;
    case BACKWARD:
      rightReverseTicks++;
      break;
    case LEFT:
      rightForwardTicksTurns++;
      break;
    case RIGHT:
      rightReverseTicksTurns++;
      break;
  }
}

// Set up the external interrupt pins INT0 and INT1
// for falling edge triggered. Use bare-metal.

void setupEINT()
{
  // Use bare-metal to configure pins 2 and 3 to be
  // falling edge triggered. Remember to enable
  // the INT0 and INT1 interrupts.
  EIMSK |= 0b00000011;
  EICRA |= 0b00001010;
}

// Implement the external interrupt ISRs below.
// INT0 ISR should call leftISR while INT1 ISR
// should call rightISR.

ISR(INT0_vect) {
  leftISR();
}

ISR(INT1_vect) {
  rightISR();
}

// Implement INT0 and INT1 ISRs above.

/*
   Setup and start codes for serial communications

*/
// Set up the serial connection. For now we are using
// Arduino Wiring, you will replace this later
// with bare-metal code.

void setupSerial()
{
  // To replace later with bare-metal.
  Serial.begin(57600);
}

// Start the serial connection. For now we are using
// Arduino wiring and this function is empty. We will
// replace this later with bare-metal code.

void startSerial()
{
  // Empty for now. To be replaced with bare-metal code
  // later on.

}

// Read the serial port. Returns the read character in
// ch if available. Also returns TRUE if ch is valid.
// This will be replaced later with bare-metal code.

int readSerial(char *buffer)
{

  int count = 0;

  while (Serial.available())
    buffer[count++] = Serial.read();

  return count;
}

// Write to the serial port. Replaced later with
// bare-metal code

void writeSerial(const char *buffer, int len)
{
  Serial.write(buffer, len);
}


/*
   Alex's motor drivers.
*/

// Set up Alex's motors. Right now this is empty, but
// later you will replace it with code to set up the PWMs
// to drive the motors.
void setupMotors()
{
  /* Our motor set up is:
        A1IN - Pin 5, PD5, OC0B
        A2IN - Pin 6, PD6, OC0A
        B1IN - Pin 10, PB2, OC1B
        B2In - pIN 11, PB3, OC2A
  */
}

// Start the PWM for Alex's motors.
// We will implement this later. For now it is
// blank.
void startMotors()
{

}

// Convert percentages to PWM values
int pwmVal(float speed)
{
  if (speed < 0.0)
    speed = 0;

  if (speed > 100.0)
    speed = 100.0;

  return (int) ((speed / 100.0) * 255.0);
}

// Move Alex forward "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// move forward at half speed.
// Specifying a distance of 0 means Alex will
// continue moving forward indefinitely.

void forward(float dist, float speed)
{
  dir = FORWARD;
  if (dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;
  newDist = forwardDist + deltaDist;

  int val = pwmVal(speed);

  // For now we will ignore dist and move
  // forward indefinitely. We will fix this
  // in Week 9.

  // LF = Left forward pin, LR = Left reverse pin
  // RF = Right forward pin, RR = Right reverse pin
  // This will be replaced later with bare-metal code.

  analogWrite(LF, val);
  analogWrite(RF, val);
  analogWrite(LR, 0);
  analogWrite(RR, 0);
}

// Reverse Alex "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// reverse at half speed.
// Specifying a distance of 0 means Alex will
// continue reversing indefinitely.

void reverse(float dist, float speed)
{
  dir = BACKWARD;
  if (dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;
  newDist = reverseDist + deltaDist;
  int val = pwmVal(speed);

  // For now we will ignore dist and
  // reverse indefinitely. We will fix this
  // in Week 9.

  // LF = Left forward pin, LR = Left reverse pin
  // RF = Right forward pin, RR = Right reverse pin
  // This will be replaced later with bare-metal code.
  analogWrite(LR, val);
  analogWrite(RR, val);
  analogWrite(LF, 0);
  analogWrite(RF, 0);
}

unsigned long computeDeltaTicks (float ang)
{
  unsigned long ticks = (unsigned long) ((ang * AlexCirc * COUNTS_PER_REV) / (360.0 * WHEEL_CIRC));
  return ticks;
}

// Turn Alex left "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Alex to
// turn left indefinitely.

void left(float ang, float speed)
{

  int val = pwmVal(speed);
  dir = LEFT;

  if (ang == 0 )
    deltaTicks = 99999999;
  else {
    deltaTicks = computeDeltaTicks(ang);
  }
  targetTicks = leftReverseTicksTurns + deltaTicks;

  // For now we will ignore ang. We will fix this in Week 9.
  // We will also replace this code with bare-metal later.
  // To turn left we reverse the left wheel and move
  // the right wheel forward.
  analogWrite(LR, val);
  analogWrite(RF, val);
  analogWrite(LF, 0);
  analogWrite(RR, 0);
}

// Turn Alex right "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Alex to
// turn right indefinitely.

void right(float ang, float speed)
{
  int val = pwmVal(speed);
  dir = RIGHT;

  if (ang == 0 )
    deltaTicks = 99999999;
  else {
    deltaTicks = computeDeltaTicks(ang);
  }
  targetTicks = rightReverseTicksTurns + deltaTicks;

  // For now we will ignore ang. We will fix this in Week 9.
  // We will also replace this code with bare-metal later.
  // To turn right we reverse the right wheel and move
  // the left wheel forward.
  analogWrite(RR, val);
  analogWrite(LF, val);
  analogWrite(LR, 0);
  analogWrite(RF, 0);
}

// Stop Alex. To replace with bare-metal code later.

void stop()
{
  dir = STOP;
  analogWrite(LF, 0);
  analogWrite(LR, 0);
  analogWrite(RF, 0);
  analogWrite(RR, 0);
}

/*
   Alex's setup and run codes

*/

// Clears all our counters
void clearCounters()
{
  leftForwardTicks = 0;
  rightForwardTicks = 0;
  leftReverseTicks = 0;
  rightReverseTicks = 0;

  leftForwardTicksTurns = 0;
  rightForwardTicksTurns = 0;
  leftReverseTicksTurns = 0;
  rightReverseTicksTurns = 0;

  leftRevs = 0;
  rightRevs = 0;
  forwardDist = 0;
  reverseDist = 0;
}

// Clears one particular counter
void clearOneCounter(int which)
{
  clearCounters();
}
// Intialize Vincet's internal states

void initializeState()
{
  clearCounters();
}

void handleCommand(TPacket *command)
{
  switch (command->command)
  {
    // For movement commands, param[0] = distance, param[1] = speed.
    case COMMAND_FORWARD:
      sendOK();
      forward((float) command->params[0], (float) command->params[1]);
      break;
    case COMMAND_REVERSE:
      sendOK();
      reverse((float) command->params[0], (float) command->params[1]);
      break;
    case COMMAND_TURN_LEFT:
      sendOK();
      left((float) command->params[0], (float) command->params[1]);
      break;
    case COMMAND_TURN_RIGHT:
      sendOK();
      right((float) command->params[0], (float) command->params[1]);
      break;
    case COMMAND_STOP:
      sendOK();
      stop();
      break;
    case COMMAND_GET_STATS:
      sendOK();
      sendStatus();
      break;
    case COMMAND_CLEAR_STATS:
      sendOK();
      clearOneCounter(command->params[0]);
      break;
    case COMMAND_GET_COLOR:
      sendOK();
      getColor();
      sendColor();
      break;
    default:
      sendBadCommand();
  }
}

void waitForHello()
{
  int exit = 0;

  while (!exit)
  {
    TPacket hello;
    TResult result;

    do
    {
      result = readPacket(&hello);
    } while (result == PACKET_INCOMPLETE);

    if (result == PACKET_OK)
    {
      if (hello.packetType == PACKET_TYPE_HELLO)
      {
        sendOK();
        exit = 1;
      }
      else
        sendBadResponse();
    }
    else if (result == PACKET_BAD)
    {
      sendBadPacket();
    }
    else if (result == PACKET_CHECKSUM_BAD)
      sendBadChecksum();
  } // !exit
}

void getColor() {
    float r, g, b;
    tcs.getRGB(&r, &g, &b);
//    dbprint("Red: ");
//    dbprint((char)(r + '0'));
//    dbprint("\n");

    if (r > 100 && g < 80 && b < 70) {
        color = 2; //red
    } else if (r < 100 && g > 90 && b < 80) {
        color = 1; //green
    } else {
        color = 0;
    }
}

void setup() {
  // put your setup code here, to run once:

  // Compute the diagonal
  AlexDiagonal = sqrt((ALEX_LENGTH * ALEX_LENGTH) + (ALEX_BREADTH * ALEX_BREADTH));
  AlexCirc = PI * AlexDiagonal;

  cli();
  setupEINT();
  setupSerial();
  startSerial();
  setupMotors();
  startMotors();
  enablePullups();
  initializeState();
  setupPowerSaving();
  sei();

  //  Serial.begin(9600);
  //    if (tcs.begin()) {
  //      Serial.println("Found sensor");
  //    } else {
  //      Serial.println("No TCS34725 found ... check your connections");
  //      while (1);
  //    }
    
  //Motors
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(11, OUTPUT);
  
  //Color sensor
  /* Initialise with default values (int time = 2.4ms, gain = 1x) */
  
  Wire.begin();

  //  tcs.begin(); //not sure about this function
  //  tcs.disable();
}

void handlePacket(TPacket *packet)
{
  switch (packet->packetType)
  {
    case PACKET_TYPE_COMMAND:
      handleCommand(packet);
      break;

    case PACKET_TYPE_RESPONSE:
      break;

    case PACKET_TYPE_ERROR:
      break;

    case PACKET_TYPE_MESSAGE:
      break;

    case PACKET_TYPE_HELLO:
      break;
  }
}

void loop() {
  
  //getColor();

  // put your main code here, to run repeatedly:
  TPacket recvPacket; // This holds commands from the Pi

  TResult result = readPacket(&recvPacket);

  if (result == PACKET_OK) {
    handlePacket(&recvPacket);
  } else {
    if (result == PACKET_BAD)
    {
      sendBadPacket();
    }
    else if (result == PACKET_CHECKSUM_BAD)
    {
      sendBadChecksum();
    }
  }

  if (deltaDist > 0) {
    if (dir == FORWARD) {
      if (forwardDist > newDist) {
        deltaDist = 0;
        newDist = 0;
        stop();
      }
    } else if (dir == BACKWARD) {
      if (reverseDist > newDist) {
        deltaDist = 0;
        newDist = 0;
        stop();
      }
    } else if (dir == STOP) {
      deltaDist = 0;
      newDist = 0;
      stop();
    }
  }

  if (deltaTicks > 0)
  {
    if (dir == LEFT)
    {
      if (leftReverseTicksTurns >= targetTicks)
      {
        deltaTicks = 0;
        targetTicks = 0;
        stop();
      }
    }
    else if (dir == RIGHT) {
      if (rightReverseTicksTurns >= targetTicks)
      {
        deltaTicks = 0;
        targetTicks = 0;
        stop();
      }
    }
    else if (dir == STOP)
    {
      deltaTicks = 0;
      targetTicks = 0;
      stop();
    }
  }
  if (deltaDist == 0 || deltaTicks == 0) {
    putArduinoToIdle();
  }
}
