#pragma once

#include "RobotAudio.h"
#include "RobotTranscription.h"

// Incremental worker-side exchange. The socket adapter and native fixture use
// this same state machine; every I/O call is nonblocking and retains offsets.
class TranscriptionUpload
{
public:
  struct Io
  {
    virtual ~Io() = default;
    // Positive: accepted bytes. -1: would block. -2: I/O error. Read 0: EOF.
    virtual int write(const uint8_t* bytes, size_t count) = 0;
    virtual int read(uint8_t* bytes, size_t capacity) = 0;
  };

  TranscriptionUpload(RobotAudio& audio, const TranscriptionProtocol::Endpoint& endpoint,
      TranscriptionProtocol::ResponseParser& parser, uint32_t now);
  void update(uint32_t now, Io& io);
  bool done() const { return complete; }
  RobotTranscription::Error error() const { return currentError; }

private:
  enum class Part
  {
    HEADER,
    FRAME,
    PREFIX,
    PCM,
    SUFFIX,
    FINAL,
    RESPONSE
  };
  RobotAudio& audio;
  TranscriptionProtocol::ResponseParser& parser;
  Part part = Part::HEADER;
  const uint8_t* bytes = nullptr;
  size_t length = 0;
  size_t offset = 0;
  const int16_t* frame = nullptr;
  char header[256];
  uint32_t lastWriteProgress;
  uint32_t responseStartedAt = 0;
  uint32_t lastReadProgress = 0;
  bool receivedAny = false;
  bool responseBeganEarly = false;
  bool complete = false;
  RobotTranscription::Error currentError = RobotTranscription::Error::NONE;

  void finish(RobotTranscription::Error error);
  bool receive(uint32_t now, Io& io);
  void segment(Part next, const void* data, size_t count, uint32_t now);
};
