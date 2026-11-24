# Local transcription test backend

A disposable Node.js 24 service for the CoreS3's variable-length recordings. It runs
local `whisper-cli` once per accepted request. There are no npm packages, API
keys, remote inference requests, or saved recordings. Setup downloads source
and model weights; inference works without internet afterward.

## One-time local setup

Install Node.js 24, Git, CMake, a C/C++ compiler and a build tool. On macOS,
Apple's Command Line Tools provide the compiler and Make (`xcode-select
--install`); install CMake separately. On Linux, use your distribution's CMake,
compiler and Make packages. No FFmpeg is needed for this PCM/WAV path.

The verified source is
[whisper.cpp v1.9.5](https://github.com/ggml-org/whisper.cpp/releases/tag/v1.9.5),
commit `d1be6fde11ac6e0407606b4e42fe72d34add8037`. Its CLI reports version
`1.9.5-dev`. Keep source, build files and models outside Git. From the repository
root, choose an absolute artifact directory outside a disposable worktree:

```sh
WHISPER_ROOT="$HOME/.cache/stackchan-whisper"
mkdir -p "$WHISPER_ROOT/models"
git clone --depth 1 --branch v1.9.5 https://github.com/ggml-org/whisper.cpp.git "$WHISPER_ROOT/whisper.cpp"
git -C "$WHISPER_ROOT/whisper.cpp" rev-parse HEAD
cmake -S "$WHISPER_ROOT/whisper.cpp" -B "$WHISPER_ROOT/whisper.cpp/build" -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_TESTS=OFF
cmake --build "$WHISPER_ROOT/whisper.cpp/build" --target whisper-cli -j 4
bash "$WHISPER_ROOT/whisper.cpp/models/download-ggml-model.sh" base "$WHISPER_ROOT/models"
shasum "$WHISPER_ROOT/models/ggml-base.bin"
"$WHISPER_ROOT/whisper.cpp/build/bin/whisper-cli" --version
```

Verify the printed commit above and model SHA-1
`465707469ff3a37a2b9b8d8f89f2f99de7299dac` against the pinned
[model list](https://github.com/ggml-org/whisper.cpp/blob/d1be6fde11ac6e0407606b4e42fe72d34add8037/models/README.md).
The multilingual `base` model is about 142 MiB. The download script uses the
[project's Hugging Face model repository](https://huggingface.co/ggerganov/whisper.cpp).
The entire build `bin` directory must remain available for shared libraries;
copying only the executable can break startup. `backend/.local/` is also ignored
for temporary artifacts, but external storage survives task worktree removal.

Copy and edit configuration:

```sh
cp backend/.env.example backend/.env
```

Set `WHISPER_CLI` and `WHISPER_MODEL` to absolute paths to the executable and
readable model. `HOST` defaults to `127.0.0.1` (an IP address), `PORT` to `3000`
(1–65535), and `WHISPER_LANGUAGE` to `en`. Language accepts lowercase two/three
letter codes or `auto`; the CLI validates language support. Keep `en` for the
first short English tests rather than short-clip language detection. Missing or
invalid paths/settings fail startup; model contents are checked by Whisper at
request time. `backend/.env` is ignored. Start from the repository root:

```sh
node --env-file=backend/.env backend/server.mjs
```

Stop with Ctrl-C. SIGINT/SIGTERM stops admission, cancels active work and waits
for child cleanup. An unresponsive child receives SIGKILL after one second.

## Offline recognition smoke test

The pinned source includes `samples/jfk.wav`, a mono 16 kHz, 16-bit PCM excerpt
of John F. Kennedy's 1961 inaugural address. The sample ships in the
[MIT-licensed source](https://github.com/ggml-org/whisper.cpp/blob/d1be6fde11ac6e0407606b4e42fe72d34add8037/LICENSE);
the federal address recording is identified as public domain in its
[source and reuse record](https://commons.wikimedia.org/wiki/File:JFK_inaugural_address.ogg).
The command below extracts the first ten seconds (the capture cap) in memory,
validates the sample format and
sends the raw PCM to a running backend. It needs no audio conversion package or
network download. Set `WHISPER_ROOT` as in setup in this terminal first.

```sh
node --input-type=module - "$WHISPER_ROOT/whisper.cpp/samples/jfk.wav" <<'JS'
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const wav = readFileSync(process.argv[2]);
assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
assert.equal(wav.toString('ascii', 8, 12), 'WAVE');
let pcm;
let formatOK = false;
for (let offset = 12; offset + 8 <= wav.length;)
{
  const name = wav.toString('ascii', offset, offset + 4);
  const size = wav.readUInt32LE(offset + 4);
  const data = offset + 8;
  assert.ok(data + size <= wav.length);
  if (name === 'fmt ')
  {
    assert.ok(size >= 16);
    formatOK = wav.readUInt16LE(data) === 1 && wav.readUInt16LE(data + 2) === 1 &&
      wav.readUInt32LE(data + 4) === 16000 && wav.readUInt16LE(data + 14) === 16;
  }
  if (name === 'data') pcm = wav.subarray(data, data + size).subarray(0, 320000);
  offset = data + size + (size % 2);
}
assert.ok(formatOK && pcm?.length === 320000);
const started = performance.now();
const response = await fetch('http://127.0.0.1:3000/v1/transcriptions', {
  method: 'POST', headers: { 'Content-Type': 'application/octet-stream',
    'Content-Length': String(pcm.length) }, body: pcm, signal: AbortSignal.timeout(30000)
});
const text = await response.text();
console.log({ status: response.status, text, elapsedMs: Math.round(performance.now() - started) });
assert.equal(response.status, 200);
assert.match(text, /my fellow Americans.*ask not what your country can do for you.*ask what you can do for your country/i);
JS
```

Each request includes model loading. Measure on your own computer against the
five-second body, 20-second inference, and 30-second firmware request deadlines.
The ten-second fixture returned HTTP 200 in 370 ms on the development computer,
including “ask not what your country can do for you, ask what you can do for
your country.” The earlier two-second crop cut into “Americans”; the longer
fixture checks words beyond that old cutoff. This checks local inference and
HTTP handling, with no CoreS3 microphone, Wi-Fi, energy detection, or broad
accuracy claim. Whisper can hallucinate text for silence/noise; the backend has
no speech detector. An empty successful transcript is valid. See the
[silence-stop plan](../docs/plan/speech-silence-stop.md) for evidence and device
checks.

## CoreS3 and replacement contract

For a trusted LAN test, set `HOST` to the computer's LAN IP address or `0.0.0.0`
and permit inbound TCP on the selected port. Configure the computer's numeric
LAN IPv4 address as the firmware backend base URL, for example
`http://192.168.1.20:3000`. `localhost` on the CoreS3 points to the CoreS3, not
the computer. This testing service uses plain HTTP on a trusted LAN and has no
public hosting or authentication scope. Audio stays on the device/computer path.

Update this backend before installing the silence-stop firmware. Legacy
64,000-byte requests remain valid. A replacement service only needs the following contract and an updated device
base URL; Whisper flags and local paths never reach the device:

| Request/response | Contract |
| --- | --- |
| Endpoint | `POST /v1/transcriptions` |
| Request media | `Content-Type: application/octet-stream` |
| Request body | An even 3,840–320,000 bytes inclusive; signed 16-bit little-endian mono PCM, 16 kHz |
| Request framing | Matching `Content-Length`; no transfer/content encoding or multipart |
| Success | 200, `Content-Type: text/plain; charset=utf-8`, explicit byte `Content-Length` |
| Success body | Trimmed UTF-8 transcript, at most 2,048 bytes; empty is valid |
| Invalid length/framing/body | 400 (including odd/undersized lengths, incomplete bodies and a five-second body receipt deadline) |
| Oversized input | 413 |
| Unsupported media | 415 |
| Busy/shutting down | 503; one admitted request, no queue or retry |
| Inference/UTF-8/output failure | 502; short message without process diagnostics |
| Inference deadline | 504; 20 seconds shared by startup, stdin, inference and stdout |
| Unknown path/method | 404/405 |

The backend adds the 44-byte WAV header in memory and launches `whisper-cli`
asynchronously without a shell, using `--model`, `--language`, `--file -`,
`--output-txt`, `--output-file -`, `--no-prints` and `--no-timestamps`.
It drains stderr, buffers at most 2,112 stdout bytes (2,048 transcript + 64
surrounding whitespace), rejects invalid UTF-8/oversized output, and never
silently truncates a transcript. Cancellation on disconnect, shutdown or
inference failure terminates the child, escalates after one second if needed,
and holds admission until process and stdio `close`. Cleanup may therefore add
up to a second to a timeout response under normal OS termination. There is no
automatic inference retry and no disk audio or transcript artifact.

## Automated checks

These tests use Node's built-in runner and controlled child processes/streams;
they do not download models, require `backend/.env`, or prove recognition:

```sh
node --test backend/server.test.mjs
node --check backend/server.mjs
node --check backend/server.test.mjs
git diff --check
```
