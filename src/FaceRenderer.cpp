#include "FaceRenderer.h"

void FaceRenderer::begin()
{
  lastBlink = millis();
  draw();
}

void FaceRenderer::setState(RobotState newState)
{
  if (state == newState)
  {
    return;
  }

  state = newState;
  draw();
}

void FaceRenderer::update()
{
  unsigned long now = millis();

  if (state == RobotState::SLEEPING)
  {
    return;
  }

  if (!eyesClosed && now - lastBlink >= 3000)
  {
    eyesClosed = true;
    lastBlink = now;
    draw();
  }
  else if (eyesClosed && now - lastBlink >= 150)
  {
    eyesClosed = false;
    lastBlink = now;
    draw();
  }
}

void FaceRenderer::draw()
{
  M5.Display.fillScreen(TFT_BLACK);

  int cx = M5.Display.width() / 2;
  int cy = M5.Display.height() / 2;

  int mouthHeight = 12;
  int eyeHeight = 55;

  switch (state)
  {
  case RobotState::IDLE:
    color = TFT_WHITE;
    break;

  case RobotState::LISTENING:
    color = TFT_CYAN;
    eyeHeight = 65;
    mouthHeight = 5;
    break;

  case RobotState::THINKING:
    color = TFT_YELLOW;
    eyeHeight = 25;
    mouthHeight = 8;
    break;

  case RobotState::SPEAKING:
    color = TFT_GREEN;
    mouthHeight = 30;
    break;

  case RobotState::SLEEPING:
    eyesClosed = true;
    eyeHeight = 5;
    break;
  }

  if (eyesClosed)
  {
    eyeHeight = 5;
  }

  int radius = min(18, eyeHeight / 2);

  // Left eye
  M5.Display.fillRoundRect(
      cx - 80, cy - 30 - eyeHeight / 2,
      40, eyeHeight, radius, color);

  // Right eye
  M5.Display.fillRoundRect(
      cx + 40, cy - 30 - eyeHeight / 2,
      40, eyeHeight, radius, color);

  // Mouth
  M5.Display.fillRoundRect(
      cx - 18, cy + 35,
      36, mouthHeight,
      min(18, mouthHeight / 2), color);
}