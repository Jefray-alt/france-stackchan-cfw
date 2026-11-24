#pragma once

#include <cstddef>
#include <cstdint>

namespace TranscriptionProtocol
{
  constexpr size_t kMaxTranscriptBytes = 2048;
  constexpr size_t kMaxHeaderBytes = 1024;
  constexpr size_t kMaxLineBytes = 255;

  struct Endpoint
  {
    uint8_t address[4] = {};
    uint16_t port = 80;
  };

  bool parseBaseUrl(const char* url, Endpoint& endpoint);

  enum class ParseStatus
  {
    READING,
    COMPLETE,
    HTTP_ERROR,
    MALFORMED,
    TOO_LARGE,
    INVALID_UTF8
  };

  // The caller supplies kMaxTranscriptBytes + 1 bytes, alive for this parser.
  class ResponseParser
  {
  public:
    explicit ResponseParser(char* text);
    ParseStatus feed(const uint8_t* bytes, size_t count);
    ParseStatus status() const { return currentStatus; }
    uint16_t httpStatus() const { return statusCode; }
    size_t textLength() const { return bodyBytes; }

  private:
    char* text;
    char line[kMaxLineBytes + 1] = {};
    size_t lineBytes = 0;
    size_t headerBytes = 0;
    size_t contentLength = 0;
    size_t bodyBytes = 0;
    uint16_t statusCode = 0;
    ParseStatus currentStatus = ParseStatus::READING;
    bool sawCr = false;
    bool inBody = false;
    bool haveLength = false;
    bool haveType = false;

    void finishLine();
  };
}
