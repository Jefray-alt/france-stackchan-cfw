#include "RobotTranscription.h"
#include "RobotAudio.h"
#include "TranscriptionUpload.h"

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
    RobotAudio* audio;
    uint32_t requestId;
  };

  RobotTranscription* owner = nullptr;
  TranscriptionProtocol::Endpoint endpoint;
  std::atomic<bool> wifiReady{false};
  std::atomic<Error> earlyError{Error::NONE};
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

RobotTranscription::Error RobotTranscription::startStream(RobotAudio& audio, uint32_t requestId)
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
  if (!audio.borrowStream())
  {
    return Error::INVALID_CLIP;
  }
  runtime->earlyError.store(Error::NONE, std::memory_order_release);
  const Runtime::Request request{&audio, requestId};
  if (xQueueSend(runtime->requests, &request, 0) != pdTRUE)
  {
    audio.releaseStream();
    return Error::BUSY;
  }
  borrowedAudio = &audio;
  return Error::NONE;
}

RobotTranscription::Error RobotTranscription::activeError() const
{
  return borrowedAudio ? runtime->earlyError.load(std::memory_order_acquire) : Error::NONE;
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
  borrowedAudio->releaseStream();
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
    earlyError.store(result.error, std::memory_order_release);
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
  const auto report = [this](Error error)
  {
    if (error != Error::NONE && error != Error::CANCELLED)
    {
      earlyError.store(error, std::memory_order_release);
    }
    return error;
  };
  Socket socket;
  socket.fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket.fd < 0)
  {
    return report(Error::CONNECT_FAILED);
  }

  const int flags = fcntl(socket.fd, F_GETFL, 0);
  const timeval timeout{static_cast<time_t>(kIoIdleTimeoutMs / 1000), 0};
  // MSG_DONTWAIT enforces nonblocking I/O; these socket timeouts are explicit
  // fallbacks. This SDK disables SO_LINGER, so do not set an unsupported option.
  if (flags < 0 || fcntl(socket.fd, F_SETFL, flags | O_NONBLOCK) < 0
      || setsockopt(socket.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0
      || setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
  {
    return report(Error::IO_FAILED);
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
    return report(Error::CONNECT_FAILED);
  }
  while (connected < 0)
  {
    const uint32_t now = millis();
    if (request.audio->streamStatus() == RobotAudio::StreamStatus::CANCELLED)
    {
      return Error::CANCELLED;
    }
    if (elapsed(now, connectStartedAt, kConnectTimeoutMs))
    {
      return report(Error::TIMED_OUT);
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(socket.fd, &writable);
    timeval immediate{};
    const int selected = select(socket.fd + 1, nullptr, &writable, nullptr, &immediate);
    if (selected < 0 && errno != EINTR)
    {
      return report(Error::CONNECT_FAILED);
    }
    if (selected > 0)
    {
      int error = 0;
      socklen_t length = sizeof(error);
      if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
      {
        return report(Error::CONNECT_FAILED);
      }
      break;
    }
    pauseWorker();
  }

  struct SocketIo : TranscriptionUpload::Io
  {
    explicit SocketIo(int fd) : fd(fd)
    {
    }
    int fd;
    int write(const uint8_t* bytes, size_t count) override
    {
      const int sent = ::send(fd, bytes, count, MSG_DONTWAIT);
      return sent >= 0 ? sent : (wouldBlock() ? -1 : -2);
    }
    int read(uint8_t* bytes, size_t count) override
    {
      const int received = ::recv(fd, bytes, count, MSG_DONTWAIT);
      return received >= 0 ? received : (wouldBlock() ? -1 : -2);
    }
  } io(socket.fd);
  TranscriptionUpload upload(*request.audio, endpoint, parser, millis());
  while (!upload.done())
  {
    upload.update(millis(), io);
    if (!upload.done())
    {
      pauseWorker();
    }
  }
  return report(upload.error());
}

const char* RobotTranscription::errorMessage(Error error)
{
  switch (error)
  {
    case Error::NONE: return "none";
    case Error::CANCELLED: return "stream cancelled";
    case Error::NOT_CONFIGURED: return "transcription is unconfigured (include/LocalConfig.h)";
    case Error::INVALID_CONFIGURATION: return "invalid Wi-Fi or numeric IPv4 HTTP base URL";
    case Error::WORKER_UNAVAILABLE: return "transcription worker could not start";
    case Error::WIFI_DISCONNECTED: return "Wi-Fi is disconnected; retry after connection";
    case Error::BUSY: return "previous transcription has not released its stream";
    case Error::INVALID_CLIP: return "no active recording stream to transcribe";
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
