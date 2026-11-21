#include <M5Unified.h>
#include "RobotController.h"

RobotController robot;

void setup()
{
  auto config = M5.config();
  M5.begin(config);

  Serial.begin(115200);

  robot.begin();

  Serial.println("Robot initialized");
}

void loop()
{
  M5.update();

  robot.update();

  delay(10);
}