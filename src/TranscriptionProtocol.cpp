#include "TranscriptionProtocol.h"

#include <cstring>

namespace
{
  char lower(char value)
  {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
  }

  bool equalIgnoringCase(const char* left, const char* right)
  {
    while (*left && *right && lower(*left) == lower(*right))
    {
      ++left;
      ++right;
    }
    return *left == *right;
  }

  bool isToken(char value)
  {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || (value != '\0' && std::strchr("!#$%&'*+-.^_`|~", value));
  }

  bool validUtf8(const char* text, size_t count)
  {
    size_t offset = 0;
    while (offset < count)
    {
      const auto first = static_cast<uint8_t>(text[offset++]);
      if (first < 0x80)
      {
        continue;
      }

      unsigned continuation;
      uint32_t code;
      uint32_t minimum;
      if (first >= 0xc2 && first <= 0xdf)
      {
        continuation = 1;
        code = first & 0x1f;
        minimum = 0x80;
      }
      else if (first >= 0xe0 && first <= 0xef)
      {
        continuation = 2;
        code = first & 0x0f;
        minimum = 0x800;
      }
      else if (first >= 0xf0 && first <= 0xf4)
      {
        continuation = 3;
        code = first & 0x07;
        minimum = 0x10000;
      }
      else
      {
        return false;
      }

      if (count - offset < continuation)
      {
        return false;
      }
      while (continuation--)
      {
        const auto next = static_cast<uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80)
        {
          return false;
        }
        code = (code << 6) | (next & 0x3f);
      }
      if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      {
        return false;
      }
    }
    return true;
  }
}

namespace TranscriptionProtocol
{
  bool parseBaseUrl(const char* url, Endpoint& endpoint)
  {
    if (!url || std::strncmp(url, "http://", 7) != 0)
    {
      return false;
    }
    const char* cursor = url + 7;
    Endpoint parsed;
    for (unsigned octet = 0; octet < 4; ++octet)
    {
      unsigned value = 0;
      unsigned digits = 0;
      while (*cursor >= '0' && *cursor <= '9')
      {
        value = value * 10 + (*cursor++ - '0');
        if (++digits > 3 || value > 255)
        {
          return false;
        }
      }
      if (!digits)
      {
        return false;
      }
      parsed.address[octet] = value;
      if (octet != 3 && *cursor++ != '.')
      {
        return false;
      }
    }
    if (*cursor == ':')
    {
      ++cursor;
      unsigned port = 0;
      unsigned digits = 0;
      while (*cursor >= '0' && *cursor <= '9')
      {
        port = port * 10 + (*cursor++ - '0');
        if (++digits > 5 || port > 65535)
        {
          return false;
        }
      }
      if (!digits || !port)
      {
        return false;
      }
      parsed.port = port;
    }
    if (*cursor == '/')
    {
      ++cursor;
    }
    if (*cursor)
    {
      return false;
    }
    endpoint = parsed;
    return true;
  }

  ResponseParser::ResponseParser(char* text) : text(text)
  {
    text[0] = '\0';
  }

  ParseStatus ResponseParser::feed(const uint8_t* bytes, size_t count)
  {
    for (size_t offset = 0; offset < count; ++offset)
    {
      if (currentStatus != ParseStatus::READING)
      {
        // Never parse error payloads, redirects or additional responses.
        if (currentStatus == ParseStatus::COMPLETE)
        {
          currentStatus = ParseStatus::MALFORMED;
        }
        break;
      }
      const uint8_t byte = bytes[offset];
      if (inBody)
      {
        text[bodyBytes++] = byte;
        text[bodyBytes] = '\0';
        if (bodyBytes == contentLength)
        {
          currentStatus = validUtf8(text, bodyBytes)
              ? ParseStatus::COMPLETE : ParseStatus::INVALID_UTF8;
        }
        continue;
      }
      if (++headerBytes > kMaxHeaderBytes)
      {
        currentStatus = ParseStatus::TOO_LARGE;
      }
      else if (sawCr)
      {
        if (byte != '\n')
        {
          currentStatus = ParseStatus::MALFORMED;
        }
        else
        {
          line[lineBytes] = '\0';
          finishLine();
          lineBytes = 0;
          sawCr = false;
        }
      }
      else if (byte == '\r')
      {
        sawCr = true;
      }
      else if ((byte < 0x20 && byte != '\t') || byte >= 0x7f)
      {
        currentStatus = ParseStatus::MALFORMED;
      }
      else if (lineBytes == kMaxLineBytes)
      {
        currentStatus = ParseStatus::TOO_LARGE;
      }
      else
      {
        line[lineBytes++] = byte;
      }
    }
    return currentStatus;
  }

  void ResponseParser::finishLine()
  {
    if (!statusCode)
    {
      if (lineBytes < 12 || (std::strncmp(line, "HTTP/1.1 ", 9) != 0
          && std::strncmp(line, "HTTP/1.0 ", 9) != 0)
          || line[9] < '1' || line[9] > '5'
          || line[10] < '0' || line[10] > '9'
          || line[11] < '0' || line[11] > '9'
          || (lineBytes > 12 && line[12] != ' '))
      {
        currentStatus = ParseStatus::MALFORMED;
        return;
      }
      statusCode = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
      if (statusCode != 200)
      {
        currentStatus = ParseStatus::HTTP_ERROR;
      }
      return;
    }
    if (!lineBytes)
    {
      if (!haveLength || !haveType)
      {
        currentStatus = ParseStatus::MALFORMED;
      }
      else
      {
        inBody = true;
        if (!contentLength)
        {
          currentStatus = ParseStatus::COMPLETE;
        }
      }
      return;
    }

    char* colon = std::strchr(line, ':');
    if (!colon || colon == line)
    {
      currentStatus = ParseStatus::MALFORMED;
      return;
    }
    for (char* cursor = line; cursor != colon; ++cursor)
    {
      if (!isToken(*cursor))
      {
        currentStatus = ParseStatus::MALFORMED;
        return;
      }
    }
    *colon = '\0';
    char* value = colon + 1;
    while (*value == ' ' || *value == '\t')
    {
      ++value;
    }
    char* end = line + lineBytes;
    while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
    {
      --end;
    }
    *end = '\0';

    if (equalIgnoringCase(line, "content-length"))
    {
      if (haveLength || !*value)
      {
        currentStatus = ParseStatus::MALFORMED;
        return;
      }
      for (const char* cursor = value; *cursor; ++cursor)
      {
        if (*cursor < '0' || *cursor > '9')
        {
          currentStatus = ParseStatus::MALFORMED;
          return;
        }
        contentLength = contentLength * 10 + (*cursor - '0');
        if (contentLength > kMaxTranscriptBytes)
        {
          currentStatus = ParseStatus::TOO_LARGE;
          return;
        }
      }
      haveLength = true;
    }
    else if (equalIgnoringCase(line, "content-type"))
    {
      if (haveType || (!equalIgnoringCase(value, "text/plain; charset=utf-8")
          && !equalIgnoringCase(value, "text/plain;charset=utf-8")))
      {
        currentStatus = ParseStatus::MALFORMED;
        return;
      }
      haveType = true;
    }
    else if (equalIgnoringCase(line, "transfer-encoding")
        || equalIgnoringCase(line, "content-encoding"))
    {
      currentStatus = ParseStatus::MALFORMED;
    }
  }
}
