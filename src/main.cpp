#include <M5Unified.h>

bool eyesClosed = false;
unsigned long lastBlink = 0;

void drawFace(bool closed)
{
  M5.Display.fillScreen(TFT_BLACK);

  int centerX = M5.Display.width() / 2;
  int centerY = M5.Display.height() / 2;

  int eyeY = centerY - 30;
  int leftEyeX = centerX - 60;
  int rightEyeX = centerX + 60;

  int eyeHeight = closed ? 5 : 55;

  // Left eye
  M5.Display.fillRoundRect(
      leftEyeX - 20,
      eyeY - eyeHeight / 2,
      40,
      eyeHeight,
      18,
      TFT_WHITE);

  // Right eye
  M5.Display.fillRoundRect(
      rightEyeX - 20,
      eyeY - eyeHeight / 2,
      40,
      eyeHeight,
      18,
      TFT_WHITE);

  // Mouth
  M5.Display.fillRoundRect(
      centerX - 18,
      centerY + 35,
      36,
      12,
      6,
      TFT_WHITE);
}

void setup()
{
  auto config = M5.config();
  M5.begin(config);

  Serial.begin(115200);

  drawFace(false);
  lastBlink = millis();
}

void loop()
{
  M5.update();

  unsigned long now = millis();

  if (!eyesClosed && now - lastBlink >= 3000)
  {
    eyesClosed = true;
    lastBlink = now;
    drawFace(true);
  }
  else if (eyesClosed && now - lastBlink >= 150)
  {
    eyesClosed = false;
    lastBlink = now;
    drawFace(false);
  }

  delay(10);
}