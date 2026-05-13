// 8 Channel Transmitter
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
const uint64_t pipeOut = 0xE9E8F0F0E1LL;
RF24 radio(9, 10); // CE,CSN pin
// Nút nhấn
#define BTN1 A2  // Servo 1 trái
#define BTN2 A3  // Servo 1 phải
#define BTN3 A4  // Servo 2 trái
#define BTN4 A5  // Servo 2 phải

struct Signal {
  byte throttle_a;
  byte roll_a;
  byte servo1;
  byte servo2;
};
Signal data;
void ResetData() {
  data.throttle_a = 127;
  data.roll_a     = 127;
  data.servo1     = 127;
  data.servo2     = 127;
}
void setup()
{
//Start everything up
radio.begin();
radio.openWritingPipe(pipeOut);
radio.stopListening(); 
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);
ResetData();
}
// Joystick center and its borders
int mapJoystickValues(int val, int lower, int middle, int upper, bool reverse)
{
val = constrain(val, lower, upper);
if ( val < middle )
val = map(val, lower, middle, 0, 128);
else
val = map(val, middle, upper, 128, 255);
return ( reverse ? 255 - val : val );
}

byte getButtonValue(bool left, bool right) {
  if (left && !right) return 0;     // trái
  if (right && !left) return 255;   // phải
  return 127;                       // giữa
}

void loop()
{
data.throttle_a = mapJoystickValues( analogRead(A0), 12, 524, 1015, false );
data.roll_a = mapJoystickValues( analogRead(A1), 12, 524, 1020, true ); 
  // đọc nút (LOW = nhấn)
  bool s1_left  = !digitalRead(BTN1);
  bool s1_right = !digitalRead(BTN2);
  bool s2_left  = !digitalRead(BTN3);
  bool s2_right = !digitalRead(BTN4);
  data.servo1 = getButtonValue(s1_left, s1_right);
  data.servo2 = getButtonValue(s2_left, s2_right);
radio.write(&data, sizeof(Signal));
}
