#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Servo.h>
#define IN1 2
#define IN2 4
#define IN3 7
#define IN4 8
#define ENA 5
#define ENB 6

Servo servo1;
Servo servo2;

struct Signal {
  byte throttle_a;
  byte roll_a;
  byte servo1;
  byte servo2;
};
Signal data;
const uint64_t pipeIn = 0xE9E8F0F0E1LL;
RF24 radio(9, 10);
unsigned long lastRecvTime = 0;
float current_us1 = 1500;
float current_us2 = 1500;
void ResetData()
{
  data.throttle_a = 127;
  data.roll_a     = 127;
  data.servo1     = 127;
  data.servo2     = 127;
}

void setup()
{
  // Motor
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  // Servo
  servo1.attach(3);
  servo2.attach(A0);
  servo1.writeMicroseconds(1500);
  servo2.writeMicroseconds(1500);
  ResetData();
  radio.begin();
  radio.openReadingPipe(1, pipeIn);
  radio.startListening();
}

void recvData()
{
  while (radio.available()) {
    radio.read(&data, sizeof(Signal));
    lastRecvTime = millis();
  }
}

// ===== MOTOR =====
void setMotor(int left, int right)
{
  if (left > 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    left = -left;
  }

  if (right > 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    right = -right;
  }

  left  = constrain(left, 0, 255);
  right = constrain(right, 0, 255);

  analogWrite(ENA, left);
  analogWrite(ENB, right);
}
int getServoTarget(byte val) {
  if (val > 100 && val < 150) return 1500; // giữa
  if (val <= 100) return 1000;             // trái
  return 2000;                             // phải
}

void loop()
{
  recvData();
  if (millis() - lastRecvTime > 1000) {
    ResetData();
  }

  // ===== MOTOR =====
  int throttle = map(data.throttle_a, 0, 255, -255, 255);
  int steering = map(data.roll_a,     0, 255, -255, 255);
  if (abs(throttle) < 20) throttle = 0;
  if (abs(steering) < 20) steering = 0;
  int leftMotor  = throttle + steering;
  int rightMotor = throttle - steering;
  leftMotor  = constrain(leftMotor,  -255, 255);
  rightMotor = constrain(rightMotor, -255, 255);
  setMotor(leftMotor, rightMotor);
  int target1 = getServoTarget(data.servo1);
  current_us1 += (target1 - current_us1) * 0.1;
  servo1.writeMicroseconds((int)current_us1);
  int target2 = getServoTarget(data.servo2);
  current_us2 += (target2 - current_us2) * 0.1;
  servo2.writeMicroseconds((int)current_us2);
}
