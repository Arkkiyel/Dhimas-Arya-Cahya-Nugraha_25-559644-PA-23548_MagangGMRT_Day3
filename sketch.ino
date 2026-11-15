/*
Nama: Dhimas Arya Cahya Nugraha
NIM: 25/559644/PA/23548

Asumsi:
1. MPU6050 dipakai sebagai pengganti BNO055 karena BNO055 tidak tersedia di Wokwi.
2. Arah positif roll, pitch, dan yaw mengikuti aturan tangan kanan sesuai diagram sumbu.
3. Roll dan pitch dihitung dari accelerometer, yaw dari kecepatan putar gyro sumbu Z.
4. Servo 1-4 berputar sebesar sudut roll/pitch (1:1), dibatasi -90 sampai 90 derajat.
5. Servo 5 bergerak ke 90 derajat (yaw positif) atau -90 derajat (yaw negatif).
6. Yaw dianggap berputar jika kecepatannya lebih dari 5 derajat/detik.
7. Selama Servo 5 menunggu 1 detik, input sensor diabaikan.
8. PIR terpicu saat sinyal berubah dari LOW ke HIGH.
9. Saat PIR terpicu, semua servo ke 45 derajat selama 1 detik, lalu kembali ke 0 derajat.
10. PIR memiliki prioritas tertinggi, input IMU diabaikan selama PIR aktif.
11. Pada implementasi fisik, servo sebaiknya memakai catu daya terpisah dengan GND disatukan.
*/


#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <ESP32Servo.h>

// Hardware
Adafruit_MPU6050 imu;
const int pinServo[5] = {27, 14, 17, 18, 13};
Servo servo[5];

// State for yaw servo (Servo 5)
unsigned long timeStop = 0;
#define NOT_MOVING 0
#define MOVING 1
#define GOING_TO_STOP 2
#define YAW_THRESHOLD 5
unsigned short servo_5_state = NOT_MOVING;

// PIR Setup
#define PIR_PIN 34
#define PIR_IDLE 0
#define PIR_ACTIVE 1
unsigned short pir_state = PIR_IDLE;

#define PIR_TARGET_DEG 45     
#define PIR_HOLD_MS 1000      
unsigned long pirStart = 0;   
int pirPrev = LOW;            

// Converting degree to microseconds so the servo could understand it
int degree2Micro(float sudut){
  int pulse = round(1500 + sudut * 1000.0 / 180.0);
  return pulse;
}


// Move Servo 1--4 according to roll and pitch
void move_servo(float roll, float pitch){
  roll = constrain(roll, -90, 90);
  pitch = constrain(pitch, -90, 90);
  int pulse12 = degree2Micro(-roll);
  int pulse34 = degree2Micro(pitch);

  servo[0].writeMicroseconds(pulse12);
  servo[1].writeMicroseconds(pulse12);
  servo[2].writeMicroseconds(pulse34);
  servo[3].writeMicroseconds(pulse34);

  // Print only when the pulse changes (for Test Case 1)
  static int lastPulse12 = -1, lastPulse34 = -1;
  if (pulse12 != lastPulse12 || pulse34 != lastPulse34) {
    Serial.printf("Roll: %.1f | Pitch: %.1f | S1: %d us | S2: %d us | S3: %d us | S4: %d us\n",
                  roll, pitch, pulse12, pulse12, pulse34, pulse34);
    lastPulse12 = pulse12;
    lastPulse34 = pulse34;
  }
}

// Move Servo 5
/*
  State machine Servo 5 (yaw)

  Alur:
    NOT_MOVING --(yaw berputar)--> MOVING --(yaw berhenti)--> GOING_TO_STOP --(1 detik)--> NOT_MOVING

  NOT_MOVING
    Arti      : Sistem tidak sedang berotasi pada bidang yaw.
    Servo 5   : Diam di posisi awal.
    Transisi  : Jika |yaw| > YAW_THRESHOLD, Servo 5 diputar, lalu ke MOVING

  MOVING
    Arti      : Sistem sedang berotasi pada bidang yaw.
    Servo 5   : Berada di +90 atau -90 sesuai arah rotasi. 
    Transisi  : Jika |yaw| <= YAW_THRESHOLD (rotasi selesai), rotasi berhenti dan pindah ke GOING_TO_STOP.

  GOING_TO_STOP
    Arti      : Rotasi sudah selesai, sistem sedang menunggu 1 detik.
    Servo 5   : Tetap diam di posisi terakhirnya.
    Transisi  : Sensor diabaikan pada keadaan ini. Jika waktu sudah melebihi 1 detik, baru berganti 
                ke NOT_MOVING
*/
void move_servo_yaw(float yaw){
  int pulse;
  if (yaw >= 0) pulse = degree2Micro(90);
  else pulse = degree2Micro(-90);
  if (servo_5_state == NOT_MOVING){
    if(abs(yaw) > YAW_THRESHOLD){
      servo[4].writeMicroseconds(pulse);
      servo_5_state = MOVING;
    }
  }
  else if (servo_5_state == MOVING){
    if(abs(yaw) <= YAW_THRESHOLD){
      timeStop = millis();
      servo_5_state = GOING_TO_STOP;
      Serial.printf("[X] Servo 5 is stopping at %lu ms\n", timeStop);
    }
    else{
      servo[4].writeMicroseconds(pulse);
    }
  }
  else if(servo_5_state == GOING_TO_STOP){
    if (millis() - timeStop >= 1000){
      servo_5_state = NOT_MOVING;
      servo[4].writeMicroseconds(degree2Micro(0));
      Serial.printf("[X] Servo 5 back to 0 at %lu ms\n", millis());
    }
  }
}

void handle_pir(){
  // Detection of PIR changing from LOW to HIGH
  int pirNow = digitalRead(PIR_PIN);
  bool newMotion = (pirNow == HIGH && pirPrev == LOW);
  pirPrev = pirNow;

  if (pir_state == PIR_IDLE){
    if (newMotion){
      for (int i = 0; i < 5; i++) servo[i].writeMicroseconds(degree2Micro(PIR_TARGET_DEG));
      pirStart = millis();
      pir_state = PIR_ACTIVE;
      Serial.println("[PIR] PIR is detected, every servo is going to the directed position");
    }
  }
  else if (pir_state == PIR_ACTIVE){
    if (millis() - pirStart >= PIR_HOLD_MS){
      // Every servo is going to 0
      for (int i = 0; i < 5; i++) servo[i].writeMicroseconds(degree2Micro(0));
      servo_5_state = NOT_MOVING;   // reset Servo 5 state
      pir_state = PIR_IDLE;
      Serial.println("[PIR] Done, every servo went back to 0");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!imu.begin(0x68)) {
    Serial.println("MPU6050 tidak ditemukan");
    while (true) delay(10);
  }
  for (int i = 0; i < 5; i++) {
    servo[i].setPeriodHertz(50);              // period 20 ms
    servo[i].attach(pinServo[i], 1000, 2000); // min = 1000 us, max = 2000 us
    servo[i].writeMicroseconds(1500);         // start from 0 deg
  }
  pinMode(PIR_PIN, INPUT);
  delay(1000);
}


void loop() {
  // Getting roll, pitch, yaw
  sensors_event_t aksel, gyro, temp;
  imu.getEvent(&aksel, &gyro, &temp);

  float ax = aksel.acceleration.x;
  float ay = aksel.acceleration.y;
  float az = aksel.acceleration.z;

  float roll     = atan2(ay, az) * 180.0 / PI;
  float pitch    = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  float yawSpeed = gyro.gyro.z * 180.0 / PI;

  // PIR has the highest priority
  handle_pir();

  // Move servos according to IMU only when PIR is not active
  if (pir_state == PIR_IDLE){
    move_servo(roll, pitch);
    move_servo_yaw(yawSpeed);
  }
  delay(50);
}
