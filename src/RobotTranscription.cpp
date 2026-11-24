#include "RobotTranscription.h"
#include "RobotAudio.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#if __has_include("LocalConfig.h")
#include "LocalConfig.h"
#endif

namespace
{
#if defined(ROBOT_WIFI_SSID) && defined(ROBOT_WIFI_PASSWORD) && defined(ROBOT_TRANSCRIPTION_BASE_URL)
  constexpr bool kHasConfiguration = true;
  constexpr char kSsid[] = ROBOT_WIFI_SSID;
  constexpr char kPassword[] = ROBOT_WIFI_PASSWORD;
  constexpr char kBaseUrl[] = ROBOT_TRANSCRIPTION_BASE_URL;
#else
  constexpr bool kHasConfiguration = false;
  constexpr char kSsid[] = "";
  constexpr char kPassword[] = "";
  constexpr char kBaseUrl[] = "";
#endif

  constexpr uint32_t kConnectTimeoutMs = 1000;
  constexpr uint32_t kIoIdleTimeoutMs = 5000;
  // Local inference may be silent for its full 20-second backend deadline.
  constexpr uint32_t kFirstResponseTimeoutMs = 25000;
  constexpr uint32_t kReconnectIntervalMs = 10000;
  constexpr uint32_t kWorkerStackBytes = 6144;

  bool elapsed(uint32_t now, uint32_t start, uint32_t limit)
  {
    return static_cast<uint32_t>(now - start) >= limit;
  }

  void pauseWorker()
  {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  bool wouldBlock()
  {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
  }

  // The only socket owner. No explicit drain, request retry, or task deletion.
  // SDK close can wait beyond the logical deadline; acknowledgment follows it.
  struct Socket
  {
    int fd = -1;

    ~Socket()
    {
      while (fd >= 0)
      {
        if (lwip_close(fd) == 0 || errno == EBADF)
        {
          fd = -1;
        }
        else
        {
          // lwip_close only frees its socket after successful prepare-delete.
          // Retain ownership after an exceptional close failure. Never publish
          // release while a descriptor is still owned; a main-task timeout is
          // allowed to finish the interaction but cannot grant a new capture.
          pauseWorker();
        }
      }
    }
  };
}

struct RobotTranscription::Runtime
{
  struct Request
  {
    const int16_t* samples;
    size_t sampleCount;
    uint32_t requestId;
    uint32_t submittedAt;
  };

  RobotTranscription* owner = nullptr;
  TranscriptionProtocol::Endpoint endpoint;
  std::atomic<bool> wifiReady{false};
  QueueHandle_t requests = nullptr;
  QueueHandle_t results = nullptr;
  TaskHandle_t worker = nullptr;
  StaticQueue_t requestQueue;
  StaticQueue_t resultQueue;
  uint8_t requestStorage[sizeof(Request)];
  uint8_t resultStorage[sizeof(uint8_t)];
  Result result;

  void run();
  void perform(const Request& request);
  Error exchange(const Request& request, TranscriptionProtocol::ResponseParser& parser);
};

RobotTranscription::Error RobotTranscription::begin()
{
  if (began)
  {
    return initializationError;
  }
  began = true;
  if (!kHasConfiguration)
  {
    return initializationError = Error::NOT_CONFIGURED;
  }

  TranscriptionProtocol::Endpoint endpoint;
  if (sizeof(kSsid) <= 1 || sizeof(kSsid) > 33 || sizeof(kPassword) > 64
      || !TranscriptionProtocol::parseBaseUrl(kBaseUrl, endpoint))
  {
    return initializationError = Error::INVALID_CONFIGURATION;
  }

  // Queues and the one response buffer occupy static storage, even if task
  // allocation fails. No second PCM allocation and no per-request queue/task.
  static Runtime storage;
  if (storage.owner)
  {
    return initializationError = Error::WORKER_UNAVAILABLE;
  }
  storage.owner = this;
  storage.endpoint = endpoint;
  storage.requests = xQueueCreateStatic(1, sizeof(Runtime::Request),
      storage.requestStorage, &storage.requestQueue);
  storage.results = xQueueCreateStatic(1, sizeof(uint8_t),
      storage.resultStorage, &storage.resultQueue);
  if (!storage.requests || !storage.results
      || xTaskCreate(workerEntry, "transcription", kWorkerStackBytes, &storage,
          tskIDLE_PRIORITY + 1, &storage.worker) != pdPASS)
  {
    return initializationError = Error::WORKER_UNAVAILABLE;
  }

  runtime = &storage;
  return initializationError = Error::NONE;
}

RobotTranscription::Readiness RobotTranscription::readiness() const
{
  if (hasPendingRequest())
  {
    return Readiness::BUSY;
  }
  switch (initializationError)
  {
    case Error::NOT_CONFIGURED: return Readiness::UNCONFIGURED;
    case Error::INVALID_CONFIGURATION: return Readiness::INVALID_CONFIGURATION;
    case Error::NONE:
      return runtime && runtime->wifiReady.load()
          ? Readiness::READY : Readiness::CONNECTING;
    default: return Readiness::WORKER_UNAVAILABLE;
  }
}

RobotTranscription::Error RobotTranscription::submit(RobotAudio& audio, uint32_t requestId)
{
  if (initializationError != Error::NONE)
  {
    return initializationError;
  }
  if (hasPendingRequest())
  {
    return Error::BUSY;
  }
  if (!runtime || !runtime->wifiReady.load())
  {
    return Error::WIFI_DISCONNECTED;
  }

  RobotAudio::Clip clip;
  if (!audio.borrowCompletedClip(clip))
  {
    return Error::INVALID_CLIP;
  }
  if (!clip.samples || clip.sampleCount < RobotAudio::kMinimumActivitySamples
      || clip.sampleCount > RobotAudio::kMaxSampleCount)
  {
    audio.releaseCompletedClip();
    return Error::INVALID_CLIP;
  }
  const Runtime::Request request{clip.samples, clip.sampleCount, requestId, millis()};
  if (xQueueSend(runtime->requests, &request, 0) != pdTRUE)
  {
    audio.releaseCompletedClip();
    return Error::BUSY;
  }
  borrowedAudio = &audio;
  return Error::NONE;
}

bool RobotTranscription::poll(Result& result)
{
  uint8_t acknowledged;
  if (!hasPendingRequest()
      || xQueueReceive(runtime->results, &acknowledged, 0) != pdTRUE)
  {
    return false;
  }

  // The worker publishes only after exchange() has closed its socket. Queue
  // synchronization also publishes the result bytes; it never touches them
  // again until a subsequent request, submitted after this copy/release.
  result = runtime->result;
  borrowedAudio->releaseCompletedClip();
  borrowedAudio = nullptr;
  return true;
}

void RobotTranscription::workerEntry(void* context)
{
  static_cast<Runtime*>(context)->run();
}

void RobotTranscription::Runtime::run()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  bool attempted = false;
  uint32_t lastAttempt = 0;
  for (;;)
  {
    const uint32_t now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;
    wifiReady.store(connected);
    if (!connected && (!attempted || elapsed(now, lastAttempt, kReconnectIntervalMs)))
    {
      // begin() starts association; there is no wait-for-connection loop.
      // A single worker schedules retries, including after a lost connection.
      WiFi.disconnect(false, false);
      WiFi.begin(kSsid, kPassword);
      attempted = true;
      lastAttempt = now;
    }

    Request request;
    if (xQueueReceive(requests, &request, pdMS_TO_TICKS(50)) == pdTRUE)
    {
      perform(request);
      const uint8_t acknowledged = 1;
      // Capacity is one; main cannot submit again before consuming this result.
      xQueueOverwrite(results, &acknowledged);
    }
  }
}

void RobotTranscription::Runtime::perform(const Request& request)
{
  result = Result{};
  result.requestId = request.requestId;
  TranscriptionProtocol::ResponseParser parser(result.text);
  if (WiFi.status() != WL_CONNECTED)
  {
    wifiReady.store(false);
    result.error = Error::WIFI_DISCONNECTED;
  }
  else
  {
    result.error = exchange(request, parser);
  }
  result.httpStatus = parser.httpStatus();
  result.textLength = result.error == Error::NONE ? parser.textLength() : 0;
  if (result.error != Error::NONE)
  {
    result.text[0] = '\0';
  }
}

RobotTranscription::Error RobotTranscription::Runtime::exchange(
    const Request& request, TranscriptionProtocol::ResponseParser& parser)
{
  Socket socket;
  socket.fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket.fd < 0)
  {
    return Error::CONNECT_FAILED;
  }

  const int flags = fcntl(socket.fd, F_GETFL, 0);
  const timeval timeout{static_cast<time_t>(kIoIdleTimeoutMs / 1000), 0};
  // MSG_DONTWAIT enforces nonblocking I/O; these socket timeouts are explicit
  // fallbacks. This SDK disables SO_LINGER, so do not set an unsupported option.
  if (flags < 0 || fcntl(socket.fd, F_SETFL, flags | O_NONBLOCK) < 0
      || setsockopt(socket.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0
      || setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
  {
    return Error::IO_FAILED;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  const auto* ip = endpoint.address;
  address.sin_addr.s_addr = htonl((static_cast<uint32_t>(ip[0]) << 24)
      | (static_cast<uint32_t>(ip[1]) << 16) | (static_cast<uint32_t>(ip[2]) << 8) | ip[3]);
  const uint32_t connectStartedAt = millis();
  const int connected = ::connect(socket.fd,
      reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  if (connected < 0 && errno != EINPROGRESS)
  {
    return Error::CONNECT_FAILED;
  }
  while (connected < 0)
  {
    const uint32_t now = millis();
    if (elapsed(now, request.submittedAt, kDeadlineMs)
        || elapsed(now, connectStartedAt, kConnectTimeoutMs))
    {
      return Error::TIMED_OUT;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(socket.fd, &writable);
    timeval immediate{};
    const int selected = select(socket.fd + 1, nullptr, &writable, nullptr, &immediate);
    if (selected < 0 && errno != EINTR)
    {
      return Error::CONNECT_FAILED;
    }
    if (selected > 0)
    {
      int error = 0;
      socklen_t length = sizeof(error);
      if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
      {
        return Error::CONNECT_FAILED;
      }
      break;
    }
    pauseWorker();
  }

  const size_t pcmBytes = request.sampleCount * sizeof(int16_t);
  char header[256];
  const int headerLength = std::snprintf(header, sizeof(header),
      "POST /v1/transcriptions HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\n"
      "Content-Type: application/octet-stream\r\nContent-Length: %u\r\n"
      "Connection: close\r\n\r\n", ip[0], ip[1], ip[2], ip[3], endpoint.port,
      static_cast<unsigned>(pcmBytes));
  if (headerLength <= 0 || static_cast<size_t>(headerLength) >= sizeof(header))
  {
    return Error::IO_FAILED;
  }
  const uint8_t* parts[] = {reinterpret_cast<const uint8_t*>(header),
      reinterpret_cast<const uint8_t*>(request.samples)};
  const size_t lengths[] = {static_cast<size_t>(headerLength), pcmBytes};
  uint32_t lastProgress = millis();
  for (unsigned part = 0; part < 2; ++part)
  {
    size_t sent = 0;
    while (sent < lengths[part])
    {
      const uint32_t now = millis();
      if (elapsed(now, request.submittedAt, kDeadlineMs)
          || elapsed(now, lastProgress, kIoIdleTimeoutMs))
      {
        return Error::TIMED_OUT;
      }
      const size_t remaining = lengths[part] - sent;
      const size_t chunk = remaining > 1024 ? 1024 : remaining;
      const int count = ::send(socket.fd, parts[part] + sent, chunk, MSG_DONTWAIT);
      if (count > 0)
      {
        sent += count;
        lastProgress = millis();
      }
      else if (count < 0 && !wouldBlock())
      {
        return Error::IO_FAILED;
      }
      else
      {
        pauseWorker();
      }
    }
  }

  lastProgress = millis();
  bool receivedAny = false;
  uint8_t incoming[256];
  while (parser.status() == TranscriptionProtocol::ParseStatus::READING)
  {
    const uint32_t now = millis();
    if (elapsed(now, request.submittedAt, kDeadlineMs)
        || elapsed(now, lastProgress,
            receivedAny ? kIoIdleTimeoutMs : kFirstResponseTimeoutMs))
    {
      return Error::TIMED_OUT;
    }
    const int count = ::recv(socket.fd, incoming, sizeof(incoming), MSG_DONTWAIT);
    if (count > 0)
    {
      parser.feed(incoming, count);
      receivedAny = true;
      lastProgress = millis();
    }
    else if (count == 0)
    {
      return Error::MALFORMED_RESPONSE;
    }
    else if (!wouldBlock())
    {
      return Error::IO_FAILED;
    }
    else
    {
      pauseWorker();
    }
  }

  switch (parser.status())
  {
    case TranscriptionProtocol::ParseStatus::COMPLETE: return Error::NONE;
    case TranscriptionProtocol::ParseStatus::HTTP_ERROR: return Error::HTTP_REJECTED;
    case TranscriptionProtocol::ParseStatus::TOO_LARGE: return Error::RESPONSE_TOO_LARGE;
    case TranscriptionProtocol::ParseStatus::INVALID_UTF8: return Error::INVALID_UTF8;
    default: return Error::MALFORMED_RESPONSE;
  }
}

const char* RobotTranscription::errorMessage(Error error)
{
  switch (error)
  {
    case Error::NONE: return "none";
    case Error::NOT_CONFIGURED: return "transcription is unconfigured (include/LocalConfig.h)";
    case Error::INVALID_CONFIGURATION: return "invalid Wi-Fi or numeric IPv4 HTTP base URL";
    case Error::WORKER_UNAVAILABLE: return "transcription worker could not start";
    case Error::WIFI_DISCONNECTED: return "Wi-Fi is disconnected; retry after connection";
    case Error::BUSY: return "previous transcription has not released its clip";
    case Error::INVALID_CLIP: return "no completed recording to transcribe";
    case Error::CONNECT_FAILED: return "could not connect to transcription backend";
    case Error::IO_FAILED: return "transcription socket I/O failed";
    case Error::TIMED_OUT: return "transcription transport timed out";
    case Error::HTTP_REJECTED: return "transcription backend rejected request";
    case Error::MALFORMED_RESPONSE: return "invalid transcription response framing or media";
    case Error::RESPONSE_TOO_LARGE: return "transcription response exceeded storage limit";
    case Error::INVALID_UTF8: return "transcription response is not valid UTF-8";
  }
  return "unknown transcription error";
}
