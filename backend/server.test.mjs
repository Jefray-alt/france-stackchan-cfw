import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { EventEmitter, once } from 'node:events';
import { chmodSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { connect } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { PassThrough, Writable } from 'node:stream';
import { setTimeout as delay } from 'node:timers/promises';
import { request } from 'node:http';
import { test } from 'node:test';
import { createTranscriptionServer, loadConfig } from './server.mjs';

const pcm = Buffer.alloc(64000, 0x42);
const config = { cliPath: '/local/whisper-cli', modelPath: '/local/base.bin', language: 'en' };

async function start(t, options = {})
{
  const app = createTranscriptionServer(config, options);
  app.server.listen(0, '127.0.0.1');
  await once(app.server, 'listening');
  t.after(() => app.close());
  return { ...app, port: app.server.address().port };
}

function post(app, body = pcm, headers = {}, method = 'POST', path = '/v1/transcriptions')
{
  return new Promise((resolve, reject) => {
    const req = request({ host: '127.0.0.1', port: app.port, method, path,
      headers: Object.fromEntries(Object.entries({ 'Content-Type': 'application/octet-stream',
        'Content-Length': body.length, ...headers }).filter(([, value]) => value !== undefined)) }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks).toString() }));
    });
    req.on('error', reject);
    req.end(body);
  });
}

test('valid PCM becomes an exact WAV and transcript-only UTF-8 response', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: (file, args, options) => {
    calls++;
    assert.equal(file, config.cliPath);
    assert.deepEqual(args, ['--model', config.modelPath, '--language', 'en', '--file', '-',
      '--output-txt', '--output-file', '-', '--no-prints', '--no-timestamps']);
    assert.deepEqual(options, { stdio: ['pipe', 'pipe', 'pipe'], shell: false });
    return spawn(process.execPath, ['--input-type=module', '-e', `
      import assert from 'node:assert/strict';
      const chunks = [];
      for await (const chunk of process.stdin) chunks.push(chunk);
      const wav = Buffer.concat(chunks);
      assert.equal(wav.length, 64044);
      assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
      assert.equal(wav.readUInt32LE(4), 64036);
      assert.equal(wav.toString('ascii', 8, 16), 'WAVEfmt ');
      assert.equal(wav.readUInt32LE(16), 16);
      assert.equal(wav.readUInt16LE(20), 1);
      assert.equal(wav.readUInt16LE(22), 1);
      assert.equal(wav.readUInt32LE(24), 16000);
      assert.equal(wav.readUInt32LE(28), 32000);
      assert.equal(wav.readUInt16LE(32), 2);
      assert.equal(wav.readUInt16LE(34), 16);
      assert.equal(wav.toString('ascii', 36, 40), 'data');
      assert.equal(wav.readUInt32LE(40), 64000);
      assert.ok(wav.subarray(44).equals(Buffer.alloc(64000, 0x42)));
      process.stderr.write('diagnostics must stay private');
      process.stdout.write('  Bonjour 世界! \\n');
    `], options);
  } });
  const result = await post(app);
  assert.equal(result.status, 200);
  assert.equal(result.headers['content-type'], 'text/plain; charset=utf-8');
  assert.equal(result.headers['content-length'], String(Buffer.byteLength('Bonjour 世界!')));
  assert.equal(result.body, 'Bonjour 世界!');
  assert.equal(calls, 1);
});

test('minimum, intermediate and maximum clips preserve exact WAV sizes and PCM', async (t) => {
  for (const length of [3840, 64002, 320000])
  {
    await t.test(String(length), async (t) => {
      const body = Buffer.alloc(length);
      for (let i = 0; i < length; i++) body[i] = i % 251;
      const app = await start(t, { spawnProcess: fixture(`
        import assert from 'node:assert/strict';
        const chunks = [];
        for await (const chunk of process.stdin) chunks.push(chunk);
        const wav = Buffer.concat(chunks);
        assert.equal(wav.length, ${length} + 44);
        assert.equal(wav.readUInt32LE(4), ${length} + 36);
        assert.equal(wav.readUInt32LE(40), ${length});
        for (let i = 0; i < ${length}; i++) assert.equal(wav[i + 44], i % 251);
        process.stdout.write('variable clip');
      `) });
      const result = await post(app, body);
      assert.equal(result.status, 200);
      assert.equal(result.body, 'variable clip');
    });
  }
});

function fixture(source, onChild = () => {})
{
  return (_file, _args, options) => {
    const child = spawn(process.execPath, ['--input-type=module', '-e', source], options);
    onChild(child);
    return child;
  };
}

function outputFixture(bytes)
{
  return fixture(`
    for await (const chunk of process.stdin) {}
    process.stdout.write(Buffer.from('${Buffer.from(bytes).toString('base64')}', 'base64'));
  `);
}

async function waitUntil(predicate)
{
  for (let attempt = 0; attempt < 200; attempt++)
  {
    if (predicate()) return;
    await delay(5);
  }
  assert.fail('Condition not reached');
}

function pendingRequest(app, chunked = false)
{
  const req = request({ host: '127.0.0.1', port: app.port, method: 'POST', path: '/v1/transcriptions',
    headers: { 'Content-Type': 'application/octet-stream',
      ...(chunked ? { 'Transfer-Encoding': 'chunked' } : { 'Content-Length': 64000 }) } });
  req.on('error', () => {});
  return req;
}

function responseFor(req)
{
  return new Promise((resolve) => req.on('response', async (response) => {
    const chunks = [];
    for await (const chunk of response) chunks.push(chunk);
    resolve({ status: response.statusCode, body: Buffer.concat(chunks).toString() });
  }));
}

test('chunked PCM arrives during an open pause and infers once only after the final chunk', async (t) => {
  let calls = 0;
  let receivedBytes = 0;
  let answered = false;
  const body = Buffer.alloc(320640);
  for (let i = 0; i < body.length; i++) body[i] = i % 251;
  const app = await start(t, { spawnProcess: fixture(`
    import assert from 'node:assert/strict';
    const chunks = [];
    for await (const chunk of process.stdin) chunks.push(chunk);
    const wav = Buffer.concat(chunks);
    assert.equal(wav.length, 320684);
    assert.equal(wav.readUInt32LE(40), 320640);
    for (let i = 0; i < 320640; i++) assert.equal(wav[i + 44], i % 251);
    process.stdout.write('stream complete');
  `, () => { calls++; }) });
  app.server.prependListener('request', (req) => {
    if (req.headers['transfer-encoding'] === 'chunked')
    {
      assert.equal(req.headers['content-length'], undefined);
      req.on('data', (chunk) => { receivedBytes += chunk.length; });
    }
  });
  const req = request({ host: '127.0.0.1', port: app.port, method: 'POST', path: '/v1/transcriptions',
    headers: { 'Content-Type': 'application/octet-stream', 'Transfer-Encoding': 'chunked' } });
  t.after(() => req.destroy());
  req.on('error', () => {});
  const result = new Promise((resolve) => req.on('response', async (response) => {
    answered = true;
    const chunks = [];
    for await (const chunk of response) chunks.push(chunk);
    resolve({ status: response.statusCode, body: Buffer.concat(chunks).toString() });
  }));
  req.write(body.subarray(0, 1));
  req.write(body.subarray(1, 640)); // HTTP chunks may split a PCM sample.
  await waitUntil(() => receivedBytes === 640);
  await delay(20);
  assert.equal(answered, false, 'an open valid stream must remain admitted');
  assert.equal(calls, 0, 'received PCM must not launch inference before completion');
  assert.equal((await post(app)).status, 503, 'streaming and fixed-length requests share admission');
  req.write(body.subarray(640));
  await waitUntil(() => receivedBytes === body.length);
  assert.equal(calls, 0, 'all PCM without the final HTTP chunk is still incomplete');
  req.end();
  assert.deepEqual(await result, { status: 200, body: 'stream complete' });
  assert.equal(calls, 1);
});

test('chunked minimum and exact 60-second maximum preserve the WAV and every PCM byte', async (t) => {
  for (const length of [3840, 1920000])
  {
    await t.test(String(length), async (t) => {
      const body = Buffer.alloc(length);
      for (let i = 0; i < length; i++) body[i] = i % 251;
      const app = await start(t, { spawnProcess: fixture(`
        import assert from 'node:assert/strict';
        const chunks = [];
        for await (const chunk of process.stdin) chunks.push(chunk);
        const wav = Buffer.concat(chunks);
        assert.equal(wav.length, ${length} + 44);
        assert.equal(wav.readUInt32LE(4), ${length} + 36);
        assert.equal(wav.readUInt32LE(40), ${length});
        for (let i = 0; i < ${length}; i++) assert.equal(wav[i + 44], i % 251);
        process.stdout.write('bounded stream');
      `) });
      const result = await post(app, body, { 'Content-Length': undefined, 'Transfer-Encoding': 'chunked' });
      assert.deepEqual({ status: result.status, body: result.body }, { status: 200, body: 'bounded stream' });
    });
  }
});

test('one sample above the stream maximum receives live 413 before the final chunk and releases admission', { timeout: 2000 }, async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: fixture(`
    for await (const chunk of process.stdin) {}
    process.stdout.write('retry');
  `, () => { calls++; }) });
  const req = pendingRequest(app, true);
  t.after(() => req.destroy());
  const result = responseFor(req);
  req.write(Buffer.alloc(1920000));
  req.write(Buffer.alloc(2));
  assert.equal((await result).status, 413);
  assert.equal(req.writableEnded, false, 'server rejects without waiting for a final HTTP chunk');
  assert.equal(calls, 0);
  assert.equal((await post(app)).body, 'retry');
  assert.equal(calls, 1);
});

test('completed empty, undersized and odd chunked bodies never infer', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  for (const length of [0, 2, 3838, 3839, 3841, 64001])
  {
    assert.equal((await post(app, Buffer.alloc(length), {
      'Content-Length': undefined, 'Transfer-Encoding': 'chunked'
    })).status, 400);
  }
  assert.equal(calls, 0);
});

test('valid empty, split multibyte, and maximum-size transcripts', async (t) => {
  for (const [name, bytes, expected] of [
    ['empty', ' \n\t', ''],
    ['multibyte', ' 世界 ', '世界'],
    ['maximum', `\n${'é'.repeat(1024)}\n`, 'é'.repeat(1024)],
    ['trim allowance', `${' '.repeat(32)}${'a'.repeat(2048)}${' '.repeat(32)}`, 'a'.repeat(2048)]
  ])
  {
    await t.test(name, async (t) => {
      const raw = Buffer.from(bytes);
      const app = await start(t, { spawnProcess: fixture(`
        for await (const chunk of process.stdin) {}
        const bytes = Buffer.from('${raw.toString('base64')}', 'base64');
        for (const byte of bytes) process.stdout.write(Buffer.from([byte]));
      `) });
      const result = await post(app);
      assert.equal(result.status, 200);
      assert.equal(result.body, expected);
      assert.equal(result.headers['content-length'], String(Buffer.byteLength(expected)));
    });
  }
});

test('invalid media, length, framing, paths and methods never launch a process', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  for (const [body, headers, method, path, expected] of [
    [pcm, { 'Content-Type': 'audio/wav' }, 'POST', '/v1/transcriptions', 415],
    [pcm, { 'Content-Type': 'application/octet-stream; rate=16000' }, 'POST', '/v1/transcriptions', 415],
    [Buffer.alloc(1), {}, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(3838), {}, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(3839), {}, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(64001), {}, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(320001), {}, 'POST', '/v1/transcriptions', 413],
    [pcm, { 'Content-Encoding': 'gzip' }, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(0), { 'Content-Length': undefined }, 'POST', '/v1/transcriptions', 400],
    [Buffer.alloc(0), {}, 'GET', '/v1/transcriptions', 405],
    [Buffer.alloc(0), {}, 'POST', '/other', 404]
  ])
  {
    assert.equal((await post(app, body, headers, method, path)).status, expected);
  }
  assert.equal(calls, 0);
});

test('short transport bodies at minimum, legacy and maximum lengths never infer', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  for (const length of [3840, 64000, 320000])
  {
    const socket = connect({ host: '127.0.0.1', port: app.port, allowHalfOpen: true });
    t.after(() => socket.destroy());
    let response = '';
    socket.on('data', (chunk) => { response += chunk.toString(); });
    await once(socket, 'connect');
    socket.end(`POST /v1/transcriptions HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nContent-Length: ${length}\r\n\r\nshort`);
    await once(socket, 'end');
    assert.match(response, /^HTTP\/1\.1 400 /);
  }
  assert.equal(calls, 0);
});

test('ambiguous or malformed raw framing never launches inference', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  for (const [framing, body = '', version = '1.1'] of [
    ['Content-Length: 3840\r\nContent-Length: 3840'],
    ['Content-Length: 3840\r\nTransfer-Encoding: chunked'],
    ['Content-Length: -2'], ['Content-Length: 3.5'], ['Content-Length: invalid'],
    ['Transfer-Encoding: gzip'], ['Transfer-Encoding: gzip, chunked'],
    ['Transfer-Encoding: chunked, gzip'],
    ['Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked'],
    ['Transfer-Encoding: chunked\r\nContent-Encoding: gzip'],
    ['Transfer-Encoding: chunked', 'not-hex\r\n'],
    ['Transfer-Encoding: chunked', '4\r\nabcdX\r\n0\r\n\r\n'],
    ['Transfer-Encoding: chunked', '0\r\n\r\n', '1.0']
  ])
  {
    const socket = connect({ host: '127.0.0.1', port: app.port });
    t.after(() => socket.destroy());
    let response = '';
    socket.on('data', (chunk) => { response += chunk.toString(); });
    await once(socket, 'connect');
    socket.end(`POST /v1/transcriptions HTTP/${version}\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\n${framing}\r\n\r\n${body}`);
    await once(socket, 'end');
    assert.match(response, /^HTTP\/1\.1 400 /);
  }
  assert.equal(calls, 0);
});

test('truncated chunks and missing terminal chunks discard PCM and release admission', async (t) => {
  let calls = 0;
  const app = await start(t, { spawnProcess: fixture(`
    for await (const chunk of process.stdin) {}
    process.stdout.write('retry');
  `, () => { calls++; }) });
  for (const body of ['f00\r\nshort', `f00\r\n${'a'.repeat(3840)}\r\n`,
    `f00\r\n${'a'.repeat(3840)}\r\n0\r\n`])
  {
    const before = calls;
    const socket = connect({ host: '127.0.0.1', port: app.port, allowHalfOpen: true });
    t.after(() => socket.destroy());
    let response = '';
    socket.on('data', (chunk) => { response += chunk.toString(); });
    await once(socket, 'connect');
    socket.end(`POST /v1/transcriptions HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n\r\n${body}`);
    await once(socket, 'end');
    assert.match(response, /^HTTP\/1\.1 400 /);
    assert.equal(calls, before);
    assert.equal((await post(app)).body, 'retry');
  }
});

test('chunked idle deadline resets on PCM progress and releases admission after a stalled body', { timeout: 2000 }, async (t) => {
  let calls = 0;
  let answered = false;
  const app = await start(t, { streamBodyIdleTimeoutMs: 100, streamBodyTimeoutMs: 500,
    spawnProcess: fixture(`
      for await (const chunk of process.stdin) {}
      process.stdout.write('retry');
    `, () => { calls++; }) });
  const req = pendingRequest(app, true);
  t.after(() => req.destroy());
  const result = responseFor(req).then((value) => { answered = true; return value; });
  req.write(pcm.subarray(0, 640));
  await delay(65);
  req.write(pcm.subarray(640, 1280));
  await delay(65);
  assert.equal(answered, false, 'progress resets the idle deadline beyond the initial deadline');
  assert.equal((await post(app)).status, 503);
  assert.equal((await result).status, 400);
  assert.equal(calls, 0);
  assert.equal((await post(app)).body, 'retry');
});

test('chunked total deadline expires despite continuing body progress', { timeout: 2000 }, async (t) => {
  let calls = 0;
  const app = await start(t, { streamBodyIdleTimeoutMs: 100, streamBodyTimeoutMs: 180,
    spawnProcess: fixture(`
      for await (const chunk of process.stdin) {}
      process.stdout.write('retry');
    `, () => { calls++; }) });
  const req = pendingRequest(app, true);
  t.after(() => req.destroy());
  const result = responseFor(req);
  req.write(pcm.subarray(0, 640));
  const timer = setInterval(() => req.write(pcm.subarray(0, 640)), 30);
  t.after(() => clearInterval(timer));
  assert.equal((await result).status, 400);
  clearInterval(timer);
  assert.equal(calls, 0);
  assert.equal((await post(app)).body, 'retry');
});

test('fixed-length deadline remains total even when body data keeps arriving', async (t) => {
  let calls = 0;
  const app = await start(t, { bodyTimeoutMs: 100,
    spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  const req = pendingRequest(app);
  t.after(() => req.destroy());
  const result = responseFor(req);
  req.write(pcm.subarray(0, 640));
  const timer = setInterval(() => req.write(pcm.subarray(0, 640)), 20);
  t.after(() => clearInterval(timer));
  assert.equal((await result).status, 400);
  clearInterval(timer);
  assert.equal(calls, 0);
});

test('body deadline holds admission, closes slow input and permits retry', async (t) => {
  const app = await start(t, { bodyTimeoutMs: 60, spawnProcess: outputFixture('retry') });
  const req = pendingRequest(app);
  const result = new Promise((resolve) => req.on('response', async (response) => {
    for await (const chunk of response) {}
    resolve(response.statusCode);
  }));
  req.write(pcm.subarray(0, 1));
  await delay(15);
  assert.equal((await post(app)).status, 503);
  assert.equal(await result, 400);
  assert.equal((await post(app)).body, 'retry');
});

test('client disconnect while receiving a body releases admission without spawning', async (t) => {
  for (const chunked of [false, true])
  {
    let calls = 0;
    let received = false;
    const app = await start(t, { spawnProcess: fixture(`
      for await (const chunk of process.stdin) {}
      process.stdout.write('retry');
    `, () => { calls++; }) });
    app.server.prependListener('request', (req) => req.once('data', () => { received = true; }));
    const req = pendingRequest(app, chunked);
    req.write(pcm.subarray(0, 3840));
    await waitUntil(() => received);
    assert.equal((await post(app)).status, 503);
    req.destroy();
    await delay(15);
    assert.equal(calls, 0, 'disconnect without normal completion must not infer');
    assert.equal((await post(app)).body, 'retry');
    assert.equal(calls, 1);
  }
});

test('shutdown cancels an open chunked body without inference', async (t) => {
  let calls = 0;
  let received = false;
  const app = await start(t, { spawnProcess: () => { calls++; throw new Error('must not spawn'); } });
  app.server.prependListener('request', (req) => req.once('data', () => { received = true; }));
  const req = pendingRequest(app, true);
  t.after(() => req.destroy());
  const result = responseFor(req);
  req.write(pcm.subarray(0, 3840));
  await waitUntil(() => received);
  const closing = app.close();
  assert.equal((await result).status, 503);
  await closing;
  assert.equal(calls, 0);
});

test('spawn throw, missing executable, nonzero and early process exits are sanitized', async (t) => {
  for (const [name, spawnProcess] of [
    ['throw', () => { throw new Error('secret process details'); }],
    ['missing executable', (_file, args, options) => spawn('/does-not-exist/whisper-cli', args, options)],
    ['nonzero', fixture("process.stderr.write('secret process details'); process.exit(7);")],
    ['early exit', fixture('process.stdin.destroy(); process.exit(1);')]
  ])
  {
    await t.test(name, async (t) => {
      const app = await start(t, { spawnProcess });
      const result = await post(app);
      assert.equal(result.status, 502);
      assert.doesNotMatch(result.body, /secret|does-not-exist/);
      assert.equal((await post(app)).status, 502);
    });
  }
});

test('invalid UTF-8, trimmed overflow and raw overflow fail explicitly', async (t) => {
  for (const bytes of [Buffer.from([0xc3, 0x28]), Buffer.alloc(2049, 0x61), Buffer.alloc(2113, 0x20)])
  {
    const app = await start(t, { spawnProcess: outputFixture(bytes) });
    assert.equal((await post(app)).status, 502);
  }
});

test('stderr is drained without entering the response', async (t) => {
  const app = await start(t, { spawnProcess: fixture(`
    for await (const chunk of process.stdin) {}
    process.stderr.write(Buffer.alloc(2 * 1024 * 1024, 0x78));
    process.stdout.write('only transcript');
  `) });
  const result = await post(app);
  assert.equal(result.status, 200);
  assert.equal(result.body, 'only transcript');
});

function controlledChild()
{
  const child = new EventEmitter();
  child.stdin = new Writable({ write(_chunk, _encoding, callback) { callback(); } });
  child.stdout = new PassThrough();
  child.stderr = new PassThrough();
  child.signals = [];
  child.kill = (signal) => { child.signals.push(signal); return true; };
  child.close = () => {
    child.stdout.end();
    child.stderr.end();
    child.emit('close', null, 'SIGKILL');
  };
  return child;
}

test('admission stays held through exit until process AND stdio close', async (t) => {
  const child = controlledChild();
  let spawned = false;
  const app = await start(t, { inferenceTimeoutMs: 35, killGraceMs: 35,
    spawnProcess: () => { spawned = true; return child; } });
  const first = post(app);
  await waitUntil(() => spawned);
  await waitUntil(() => child.signals.includes('SIGTERM'));
  child.emit('exit', null, 'SIGTERM');
  assert.equal((await post(app)).status, 503);
  await waitUntil(() => child.signals.includes('SIGKILL'));
  assert.equal((await post(app)).status, 503);
  child.close();
  assert.equal((await first).status, 504);
});

test('shared inference deadline escalates a real child that ignores SIGTERM, then permits retry', async (t) => {
  let child;
  let ready = false;
  let calls = 0;
  const held = fixture(`
    process.on('SIGTERM', () => {});
    for await (const chunk of process.stdin) {}
    process.stderr.write('ready');
    setInterval(() => {}, 1000);
  `, (value) => {
    child = value;
    value.stderr.on('data', () => { ready = true; });
  });
  const app = await start(t, { inferenceTimeoutMs: 180, killGraceMs: 50,
    spawnProcess: (...args) => (++calls === 1 ? held(...args) : outputFixture('retry')(...args)) });
  const first = post(app);
  await waitUntil(() => ready);
  assert.equal((await post(app)).status, 503);
  assert.equal((await first).status, 504);
  assert.equal(child.signalCode, 'SIGKILL');
  assert.throws(() => process.kill(child.pid, 0), { code: 'ESRCH' });
  assert.equal((await post(app)).body, 'retry');
});

test('disconnect cancels inference and does not release admission before child closure', async (t) => {
  const child = controlledChild();
  let calls = 0;
  const app = await start(t, { killGraceMs: 30, spawnProcess: () => { calls++; return child; } });
  const req = pendingRequest(app);
  req.end(pcm);
  await waitUntil(() => calls === 1);
  req.destroy();
  await waitUntil(() => child.signals.includes('SIGTERM'));
  assert.equal((await post(app)).status, 503);
  child.close();
  await delay(10);
});

test('shutdown cancels inference and waits for child closure', async (t) => {
  const child = controlledChild();
  let spawned = false;
  const app = await start(t, { killGraceMs: 30, spawnProcess: () => { spawned = true; return child; } });
  const first = post(app);
  await waitUntil(() => spawned);
  let closed = false;
  const closing = app.close().then(() => { closed = true; });
  await waitUntil(() => child.signals.includes('SIGKILL'));
  assert.equal(closed, false);
  child.close();
  assert.equal((await first).status, 503);
  await closing;
  assert.equal(closed, true);
});

test('stdin, stdout and stderr errors terminate and wait for cleanup', async (t) => {
  for (const stream of ['stdin', 'stdout', 'stderr'])
  {
    await t.test(stream, async (t) => {
      const child = controlledChild();
      let spawned = false;
      const app = await start(t, { spawnProcess: () => { spawned = true; return child; } });
      const first = post(app);
      await waitUntil(() => spawned);
      child[stream].emit('error', new Error('private stream error'));
      assert.deepEqual(child.signals, ['SIGTERM']);
      assert.equal((await post(app)).status, 503);
      child.close();
      const result = await first;
      assert.equal(result.status, 502);
      assert.doesNotMatch(result.body, /private/);
    });
  }
});

test('stdin writing is included in the shared inference deadline', async (t) => {
  const child = controlledChild();
  child.stdin = new Writable({ write() { /* Never acknowledge the input write. */ } });
  const app = await start(t, { inferenceTimeoutMs: 30, killGraceMs: 30, spawnProcess: () => child });
  const first = post(app);
  await waitUntil(() => child.signals.includes('SIGTERM'));
  assert.equal((await post(app)).status, 503);
  child.close();
  assert.equal((await first).status, 504);
});

test('startup validates paths, access, defaults and settings', () => {
  const dir = mkdtempSync(join(tmpdir(), 'stackchan-backend-'));
  try
  {
    const cli = join(dir, 'whisper-cli');
    const model = join(dir, 'ggml-base.bin');
    writeFileSync(cli, '#!/bin/sh\nexit 0\n', { mode: 0o700 });
    writeFileSync(model, 'fixture');
    const env = { WHISPER_CLI: cli, WHISPER_MODEL: model };
    assert.deepEqual(loadConfig(env), { cliPath: cli, modelPath: model, host: '127.0.0.1', port: 3000, language: 'en' });
    assert.equal(loadConfig({ ...env, HOST: '0.0.0.0', PORT: '8100', WHISPER_LANGUAGE: 'fr' }).port, 8100);
    for (const patch of [
      { WHISPER_CLI: undefined }, { WHISPER_CLI: 'relative' }, { WHISPER_CLI: join(dir, 'missing') },
      { WHISPER_MODEL: 'relative' }, { WHISPER_MODEL: join(dir, 'missing') }, { WHISPER_MODEL: dir },
      { PORT: '0' }, { PORT: '65536' }, { PORT: '3.5' }, { PORT: '' },
      { HOST: '' }, { HOST: 'not an address' }, { WHISPER_LANGUAGE: '' }, { WHISPER_LANGUAGE: '--bad' }
    ])
    {
      assert.throws(() => loadConfig({ ...env, ...patch }));
    }
    chmodSync(cli, 0o600);
    assert.throws(() => loadConfig(env), /executable/);
    chmodSync(cli, 0o700);
    chmodSync(model, 0o000);
    assert.throws(() => loadConfig(env), /readable/);
  }
  finally
  {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('standalone startup fails clearly without configuration', async () => {
  const child = spawn(process.execPath, ['backend/server.mjs'], {
    env: { PATH: process.env.PATH }, stdio: ['ignore', 'pipe', 'pipe']
  });
  let errors = '';
  child.stderr.on('data', (chunk) => { errors += chunk; });
  const [code] = await once(child, 'close');
  assert.equal(code, 1);
  assert.match(errors, /Backend startup failed: WHISPER_CLI must be an absolute file path/);
});
