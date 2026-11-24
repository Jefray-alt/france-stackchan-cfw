#include <unity.h>
#include <cstring>
#include <string>
#include "TranscriptionProtocol.h"

using namespace TranscriptionProtocol;

void setUp()
{
}

void tearDown()
{
}

ParseStatus parse(const std::string& response, char* text, size_t split = 1)
{
  ResponseParser parser(text);
  for (size_t offset = 0; offset < response.size(); offset += split)
  {
    const size_t count = response.size() - offset < split ? response.size() - offset : split;
    parser.feed(reinterpret_cast<const uint8_t*>(response.data() + offset), count);
  }
  return parser.status();
}

std::string response(const std::string& text)
{
  return "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: "
      + std::to_string(text.size()) + "\r\n\r\n" + text;
}

void test_numeric_http_url_only()
{
  Endpoint endpoint;
  TEST_ASSERT_TRUE(parseBaseUrl("http://192.168.1.23:3000/", endpoint));
  const uint8_t expected[] = {192, 168, 1, 23};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, endpoint.address, 4);
  TEST_ASSERT_EQUAL_UINT16(3000, endpoint.port);
  TEST_ASSERT_TRUE(parseBaseUrl("http://10.0.0.5", endpoint));
  TEST_ASSERT_EQUAL_UINT16(80, endpoint.port);

  const char* invalid[] = {
    "", "https://10.0.0.5", "http://localhost:3000", "http://1.2.3.256",
    "http://1.2.3", "http://1.2.3.4:0", "http://1.2.3.4:65536",
    "http://1.2.3.4:999999999999999999", "http://user@1.2.3.4",
    "http://1.2.3.4/v1", "http://1.2.3.4?x=1", "http://[::1]",
    "http://1.2.3.4:", "http://1.2.3.4//", "http://1.2.3.4\r\nX: y"
  };
  for (const auto* url : invalid)
  {
    TEST_ASSERT_FALSE_MESSAGE(parseBaseUrl(url, endpoint), url);
  }
}

void test_fragmented_utf8_and_empty_success()
{
  char text[kMaxTranscriptBytes + 1];
  const std::string multilingual = "Hello, mundo \xE6\x97\xA5\xF0\x9F\x8C\x8F";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::COMPLETE),
      static_cast<int>(parse(response(multilingual), text)));
  TEST_ASSERT_EQUAL_MEMORY(multilingual.data(), text, multilingual.size());
  TEST_ASSERT_EQUAL_CHAR('\0', text[multilingual.size()]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::COMPLETE),
      static_cast<int>(parse(response(""), text, 256)));
}

void test_exact_body_length_including_nul_and_maximum()
{
  char text[kMaxTranscriptBytes + 1];
  const std::string body("a\0b", 3);
  const auto wire = response(body);
  ResponseParser parser(text);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::COMPLETE),
      static_cast<int>(parser.feed(reinterpret_cast<const uint8_t*>(wire.data()), wire.size())));
  TEST_ASSERT_EQUAL_UINT(3, parser.textLength());
  TEST_ASSERT_EQUAL_MEMORY(body.data(), text, 3);
  const std::string maximum(kMaxTranscriptBytes, 'x');
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::COMPLETE),
      static_cast<int>(parse(response(maximum), text, 67)));
  TEST_ASSERT_EQUAL_CHAR('\0', text[kMaxTranscriptBytes]);
}

void test_incomplete_body_waits_and_http_errors_do_not_follow_redirects()
{
  char text[kMaxTranscriptBytes + 1];
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::READING),
      static_cast<int>(parse("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 2\r\n\r\nx", text)));
  for (unsigned code : {302u, 400u, 413u, 415u, 503u, 502u, 504u})
  {
    ResponseParser parser(text);
    const std::string wire = "HTTP/1.1 " + std::to_string(code) + " Error\r\n";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::HTTP_ERROR),
        static_cast<int>(parser.feed(reinterpret_cast<const uint8_t*>(wire.data()), wire.size())));
    TEST_ASSERT_EQUAL_UINT(code, parser.httpStatus());
  }
}

void test_rejects_ambiguous_framing_and_media()
{
  char text[kMaxTranscriptBytes + 1];
  const char* invalid[] = {
    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 0\r\nContent-Encoding: gzip\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: -1\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 1, 1\r\n\r\n",
    "HTTP/1.1 2000 OK\r\n", "HTTP/2 200 OK\r\n", "HTTP/1.1 200 OK\n",
    "HTTP/1.1 200 OK\r\n folded: header\r\n",
    "HTTP/1.1 200 OK\r\nBad Header: value\r\n"
  };
  for (const auto* wire : invalid)
  {
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ParseStatus::MALFORMED),
        static_cast<int>(parse(wire, text)), wire);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::MALFORMED),
      static_cast<int>(parse(response("x") + "extra", text, 512)));
}

void test_header_and_response_storage_are_bounded()
{
  struct Guarded
  {
    char text[kMaxTranscriptBytes + 1];
    uint8_t guard[4] = {0x12, 0x34, 0xab, 0xcd};
  } storage;
  const uint8_t expectedGuard[] = {0x12, 0x34, 0xab, 0xcd};
  const std::string tooLarge = "HTTP/1.1 200 OK\r\nContent-Length: 2049\r\n";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::TOO_LARGE),
      static_cast<int>(parse(tooLarge, storage.text)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedGuard, storage.guard, 4);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::TOO_LARGE),
      static_cast<int>(parse("HTTP/1.1 200 OK\r\nX: " + std::string(kMaxLineBytes, 'a'), storage.text)));
  std::string headers = "HTTP/1.1 200 OK\r\n";
  for (unsigned i = 0; i < 200; ++i)
  {
    headers += "X: a\r\n";
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::TOO_LARGE),
      static_cast<int>(parse(headers, storage.text)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedGuard, storage.guard, 4);
}

void test_rejects_invalid_utf8_instead_of_truncating()
{
  char text[kMaxTranscriptBytes + 1];
  const std::string invalid[] = {
    std::string("\x80", 1), std::string("\xc0\xaf", 2), std::string("\xe0\x80\xaf", 3),
    std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
    std::string("\xf5\x80\x80\x80", 4), std::string("\xe6\x97", 2), std::string("\xc2x", 2)
  };
  for (const auto& body : invalid)
  {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ParseStatus::INVALID_UTF8),
        static_cast<int>(parse(response(body), text)));
  }
}

int main(int, char**)
{
  UNITY_BEGIN();
  RUN_TEST(test_numeric_http_url_only);
  RUN_TEST(test_fragmented_utf8_and_empty_success);
  RUN_TEST(test_exact_body_length_including_nul_and_maximum);
  RUN_TEST(test_incomplete_body_waits_and_http_errors_do_not_follow_redirects);
  RUN_TEST(test_rejects_ambiguous_framing_and_media);
  RUN_TEST(test_header_and_response_storage_are_bounded);
  RUN_TEST(test_rejects_invalid_utf8_instead_of_truncating);
  return UNITY_END();
}
