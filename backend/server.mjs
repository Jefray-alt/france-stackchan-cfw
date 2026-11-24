import { spawn } from 'node:child_process';
import { accessSync, constants, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { isIP } from 'node:net';
import { isAbsolute, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const PCM_BYTES = 64000;
const TRANSCRIPT_BYTES = 2048;
const OUTPUT_BYTES = TRANSCRIPT_BYTES + 64;

class RequestError extends Error
{
  constructor(status, message)
  {
    super(message);
    this.status = status;
  }
}

export function loadConfig(env = process.env)
{
  for (const [key, mode] of [['WHISPER_CLI', constants.X_OK], ['WHISPER_MODEL', constants.R_OK]])
  {
    const path = env[key];
    if (!path || !isAbsolute(path))
    {
      throw new Error(`${key} must be an absolute file path`);
    }
    try
    {
      if (!statSync(path).isFile()) throw new Error('not a file');
      accessSync(path, mode);
    }
    catch
    {
      throw new Error(`${key} must be ${key === 'WHISPER_CLI' ? 'an executable' : 'a readable'} file`);
    }
  }

  const host = env.HOST ?? '127.0.0.1';
  const port = env.PORT ?? '3000';
  const language = env.WHISPER_LANGUAGE ?? 'en';
  if (!isIP(host)) throw new Error('HOST must be an IP address');
  if (!/^[0-9]+$/.test(port) || Number(port) < 1 || Number(port) > 65535)
  {
    throw new Error('PORT must be an integer from 1 to 65535');
  }
  if (!/^(?:[a-z]{2,3}|auto)$/.test(language))
  {
    throw new Error('WHISPER_LANGUAGE must be a lowercase language code or auto');
  }
  return { cliPath: env.WHISPER_CLI, modelPath: env.WHISPER_MODEL, language, host, port: Number(port) };
}

function makeWav(pcm)
{
  const wav = Buffer.alloc(44 + pcm.length);
  wav.write('RIFF', 0);
  wav.writeUInt32LE(36 + pcm.length, 4);
  wav.write('WAVEfmt ', 8);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(16000, 24);
  wav.writeUInt32LE(32000, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write('data', 36);
  wav.writeUInt32LE(pcm.length, 40);
  pcm.copy(wav, 44);
  return wav;
}

// A cancellation requests termination; only 'close' proves process AND stdio cleanup.
export function transcribe(pcm, config, {
  signal, spawnProcess = spawn, inferenceTimeoutMs = 20000, killGraceMs = 1000
} = {})
{
  return new Promise((resolveResult, rejectResult) => {
    if (signal?.aborted)
    {
      rejectResult(new RequestError(503, 'Request cancelled'));
      return;
    }
    let child;
    let failure;
    let closed = false;
    let killTimer;
    let inputFinished = false;
    let outputBytes = 0;
    const output = Buffer.alloc(OUTPUT_BYTES);
    const fail = (error) => {
      if (failure || closed) return;
      failure = error;
      child.stdin.destroy();
      try { child.kill('SIGTERM'); } catch { /* Still wait for close. */ }
      killTimer = setTimeout(() => {
        try { child.kill('SIGKILL'); } catch { /* Close remains authoritative. */ }
      }, killGraceMs);
    };
    const onAbort = () => fail(new RequestError(503, 'Request cancelled'));
    const deadline = setTimeout(() => fail(new RequestError(504, 'Inference timed out')), inferenceTimeoutMs);
    try
    {
      child = spawnProcess(config.cliPath, [
        '--model', config.modelPath, '--language', config.language, '--file', '-',
        '--output-txt', '--output-file', '-', '--no-prints', '--no-timestamps'
      ], { stdio: ['pipe', 'pipe', 'pipe'], shell: false });
    }
    catch
    {
      clearTimeout(deadline);
      rejectResult(new RequestError(502, 'Local inference failed'));
      return;
    }
    signal?.addEventListener('abort', onAbort, { once: true });
    child.on('error', () => fail(new RequestError(502, 'Local inference failed')));
    child.stdin.on('error', () => fail(new RequestError(502, 'Inference input failed')));
    child.stdout.on('error', () => fail(new RequestError(502, 'Inference output failed')));
    child.stderr.on('error', () => fail(new RequestError(502, 'Local inference failed')));
    child.stderr.resume();
    child.stdout.on('data', (chunk) => {
      if (failure) return;
      if (outputBytes + chunk.length > OUTPUT_BYTES)
      {
        fail(new RequestError(502, 'Inference output too large'));
        return;
      }
      chunk.copy(output, outputBytes);
      outputBytes += chunk.length;
    });
    child.on('close', (code) => {
      closed = true;
      clearTimeout(deadline);
      clearTimeout(killTimer);
      signal?.removeEventListener('abort', onAbort);
      if (failure) return rejectResult(failure);
      if (code !== 0 || !inputFinished)
      {
        return rejectResult(new RequestError(502, 'Local inference failed'));
      }
      try
      {
        const text = new TextDecoder('utf-8', { fatal: true }).decode(output.subarray(0, outputBytes)).trim();
        if (Buffer.byteLength(text) > TRANSCRIPT_BYTES) throw new Error('too large');
        resolveResult(text);
      }
      catch
      {
        rejectResult(new RequestError(502, 'Invalid inference output'));
      }
    });
    child.stdin.end(makeWav(pcm), (error) => {
      if (error) fail(new RequestError(502, 'Inference input failed'));
      else inputFinished = true;
    });
  });
}

function receiveBody(request, signal, timeoutMs)
{
  return new Promise((resolveBody, rejectBody) => {
    const body = Buffer.alloc(PCM_BYTES);
    let bytes = 0;
    const finish = (error) => {
      clearTimeout(timer);
      request.removeListener('data', onData);
      request.removeListener('end', onEnd);
      request.removeListener('error', onError);
      signal.removeEventListener('abort', onAbort);
      if (error) rejectBody(error);
      else resolveBody(body);
    };
    const onData = (chunk) => {
      if (bytes + chunk.length > PCM_BYTES)
      {
        finish(new RequestError(413, 'Recording too large'));
        return;
      }
      chunk.copy(body, bytes);
      bytes += chunk.length;
    };
    const onEnd = () => finish(bytes === PCM_BYTES ? null : new RequestError(400, 'Invalid recording length'));
    const onError = () => finish(new RequestError(400, 'Invalid recording body'));
    const onAbort = () => finish(new RequestError(503, 'Request cancelled'));
    const timer = setTimeout(() => finish(new RequestError(400, 'Recording body timed out')), timeoutMs);
    request.on('data', onData);
    request.once('end', onEnd);
    request.once('error', onError);
    signal.addEventListener('abort', onAbort, { once: true });
  });
}

function sendText(response, status, text, close = false)
{
  if (response.destroyed || response.writableEnded) return;
  response.writeHead(status, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Content-Length': Buffer.byteLength(text),
    ...(close ? { Connection: 'close' } : {})
  });
  response.end(text);
}

export function createTranscriptionServer(config, {
  spawnProcess = spawn, bodyTimeoutMs = 5000, inferenceTimeoutMs = 20000, killGraceMs = 1000
} = {})
{
  let active;
  let shuttingDown = false;
  let closePromise;
  const server = createServer({ maxHeaderSize: 8192 }, (request, response) => {
    const reject = (status, text) => {
      sendText(response, status, text, true);
      request.resume();
    };
    if (request.url !== '/v1/transcriptions') return reject(404, 'Not found');
    if (request.method !== 'POST') return reject(405, 'Method not allowed');
    if (shuttingDown || active) return reject(503, 'Transcription busy');
    if (request.headers['content-type']?.trim().toLowerCase() !== 'application/octet-stream')
    {
      return reject(415, 'Expected application/octet-stream');
    }
    const length = request.headers['content-length'];
    if (request.headers['transfer-encoding'] || !length || !/^[0-9]+$/.test(length))
    {
      return reject(400, 'Expected Content-Length: 64000');
    }
    if (Number(length) > PCM_BYTES) return reject(413, 'Recording too large');
    if (Number(length) !== PCM_BYTES) return reject(400, 'Expected Content-Length: 64000');

    const controller = new AbortController();
    const cancel = () => controller.abort();
    request.once('aborted', cancel);
    // Request 'close' also fires for a normally received body; response close identifies disconnects.
    const responseClosed = () => { if (!response.writableFinished) cancel(); };
    response.once('close', responseClosed);
    let release;
    const done = new Promise((resolveDone) => { release = resolveDone; });
    active = { cancel, done };
    void (async () => {
      try
      {
        const pcm = await receiveBody(request, controller.signal, bodyTimeoutMs);
        const text = await transcribe(pcm, config, {
          signal: controller.signal, spawnProcess, inferenceTimeoutMs, killGraceMs
        });
        sendText(response, 200, text);
      }
      catch (error)
      {
        reject(error instanceof RequestError ? error.status : 502,
          error instanceof RequestError ? error.message : 'Local inference failed');
      }
      finally
      {
        request.removeListener('aborted', cancel);
        response.removeListener('close', responseClosed);
        active = undefined;
        release();
      }
    })();
  });
  server.headersTimeout = 5000;
  server.requestTimeout = 0; // The admitted body has its own exact deadline.
  return {
    server,
    close()
    {
      if (!closePromise)
      {
        shuttingDown = true;
        const done = active?.done;
        active?.cancel();
        closePromise = Promise.all([
          done,
          new Promise((resolveClose) => server.close(resolveClose))
        ]).then(() => undefined);
        server.closeIdleConnections();
      }
      return closePromise;
    }
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href)
{
  try
  {
    const config = loadConfig();
    const app = createTranscriptionServer(config);
    app.server.on('error', (error) => {
      console.error(`Backend startup failed: ${error.code ?? 'listen error'}`);
      process.exitCode = 1;
    });
    app.server.listen(config.port, config.host, () => {
      console.log(`Transcription backend listening on ${config.host}:${config.port}`);
    });
    for (const signal of ['SIGINT', 'SIGTERM'])
    {
      process.on(signal, () => { void app.close(); });
    }
  }
  catch (error)
  {
    console.error(`Backend startup failed: ${error.message}`);
    process.exitCode = 1;
  }
}
