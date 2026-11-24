#include "TranscriptionUpload.h"

#include <cstdio>

namespace
{
  constexpr char kPrefix[] = "280\r\n"; // 640 PCM bytes per frame.
  constexpr char kSuffix[] = "\r\n";
  constexpr char kFinal[] = "0\r\n\r\n";
  using Error = RobotTranscription::Error;
  using ParseStatus = TranscriptionProtocol::ParseStatus;
}

TranscriptionUpload::TranscriptionUpload(RobotAudio& audio,
    const TranscriptionProtocol::Endpoint& endpoint,
    TranscriptionProtocol::ResponseParser& parser, uint32_t now)
    : audio(audio), parser(parser), lastWriteProgress(now)
{
  static_assert(RobotAudio::kFrameSamples * sizeof(int16_t) == 0x280,
      "Chunk prefix must match the PCM frame size");
  const auto* ip = endpoint.address;
  const int count = std::snprintf(header, sizeof(header),
      "POST /v1/transcriptions HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\n"
      "Content-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n"
      "Connection: close\r\n\r\n", ip[0], ip[1], ip[2], ip[3], endpoint.port);
  if (count <= 0 || static_cast<size_t>(count) >= sizeof(header))
  {
    finish(Error::IO_FAILED);
    return;
  }
  segment(Part::HEADER, header, count, now);
}

void TranscriptionUpload::finish(Error error)
{
  currentError = error;
  complete = true;
}

void TranscriptionUpload::segment(Part next, const void* data, size_t count, uint32_t now)
{
  part = next;
  bytes = static_cast<const uint8_t*>(data);
  length = count;
  offset = 0;
  lastWriteProgress = now;
}

bool TranscriptionUpload::receive(uint32_t now, Io& io)
{
  uint8_t incoming[256];
  const int count = io.read(incoming, sizeof(incoming));
  if (count > 0)
  {
    // Preserve arrival phase even if a fragmented status line is recognized
    // only after the request's final chunk has been accepted by the socket.
    if (part != Part::RESPONSE)
    {
      responseBeganEarly = true;
    }
    parser.feed(incoming, count);
    lastReadProgress = now;
    receivedAny = true;
    switch (parser.status())
    {
      case ParseStatus::HTTP_ERROR: finish(Error::HTTP_REJECTED); break;
      case ParseStatus::TOO_LARGE: finish(Error::RESPONSE_TOO_LARGE); break;
      case ParseStatus::INVALID_UTF8: finish(Error::INVALID_UTF8); break;
      case ParseStatus::MALFORMED: finish(Error::MALFORMED_RESPONSE); break;
      default:
        if (responseBeganEarly && parser.httpStatus() == 200)
        {
          finish(Error::MALFORMED_RESPONSE);
        }
        else if (parser.status() == ParseStatus::COMPLETE)
        {
          finish(Error::NONE);
        }
        break;
    }
  }
  else if (count == 0)
  {
    finish(Error::IO_FAILED);
  }
  else if (count != -1)
  {
    finish(Error::IO_FAILED);
  }
  return complete;
}

void TranscriptionUpload::update(uint32_t now, Io& io)
{
  if (complete)
  {
    return;
  }
  if (audio.streamStatus() == RobotAudio::StreamStatus::CANCELLED)
  {
    finish(Error::CANCELLED); // Close without a final chunk, including partial PCM.
    return;
  }
  if (part == Part::RESPONSE)
  {
    // Expiry wins over readable data, including a complete response at the
    // boundary. A late read must not reset the clock or return a transcript.
    const uint32_t start = receivedAny ? lastReadProgress : responseStartedAt;
    if (static_cast<uint32_t>(now - start) >= (receivedAny ? 5000u : 25000u))
    {
      finish(Error::TIMED_OUT);
      return;
    }
  }
  // Recognize rejection/EOF during capture or stalled writes.
  if (receive(now, io))
  {
    return;
  }
  if (part == Part::RESPONSE)
  {
    return;
  }

  // Bound work per poll while allowing all three frame segments in one pass.
  for (unsigned step = 0; step < 16; ++step)
  {
    if (audio.streamStatus() == RobotAudio::StreamStatus::CANCELLED)
    {
      finish(Error::CANCELLED);
      return;
    }
    if (part == Part::FRAME)
    {
      frame = audio.streamFrame();
      if (frame)
      {
        segment(Part::PREFIX, kPrefix, sizeof(kPrefix) - 1, now);
      }
      else if (audio.streamStatus() == RobotAudio::StreamStatus::FINISHED)
      {
        // Acquire FINISHED before rechecking the final watermark: a concurrent
        // last-frame publication must never be mistaken for a drained stream.
        if (!audio.streamDrained())
        {
          continue;
        }
        segment(Part::FINAL, kFinal, sizeof(kFinal) - 1, now);
      }
      else
      {
        return; // Waiting for capture is not stalled socket I/O.
      }
    }
    if (static_cast<uint32_t>(now - lastWriteProgress) >= 5000u)
    {
      finish(Error::TIMED_OUT);
      return;
    }
    const int count = io.write(bytes + offset, length - offset);
    if (count == -1 || count == 0)
    {
      return;
    }
    if (count < 0 || static_cast<size_t>(count) > length - offset)
    {
      finish(Error::IO_FAILED);
      return;
    }
    offset += count;
    lastWriteProgress = now;
    if (offset != length)
    {
      continue;
    }
    switch (part)
    {
      case Part::HEADER: part = Part::FRAME; break;
      case Part::PREFIX:
        segment(Part::PCM, frame, RobotAudio::kFrameSamples * sizeof(int16_t), now);
        break;
      case Part::PCM:
        // send() copied all PCM. The suffix owns no ring memory; no response or
        // backend acknowledgment is needed before this slot is reusable.
        audio.releaseStreamFrame();
        frame = nullptr;
        segment(Part::SUFFIX, kSuffix, sizeof(kSuffix) - 1, now);
        break;
      case Part::SUFFIX: part = Part::FRAME; break;
      case Part::FINAL:
        part = Part::RESPONSE;
        responseStartedAt = now;
        return;
      default: finish(Error::IO_FAILED); return;
    }
  }
}
