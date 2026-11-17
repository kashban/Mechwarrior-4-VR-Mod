#!/usr/bin/env node
// AI code review for pull requests by a local model on the developer's PC: with AI_API=openai an OpenAI-compatible
// server (llama.cpp's llama-server behind llama-swap), else Ollama; OLLAMA_URL and OLLAMA_MODEL name the server and
// its model. Taken over from the MiljanaMovingIn project's pipeline, where it was measured and tuned; what differs
// here is GitHub's API, the reviewed paths and the prompt. It runs on a self-hosted runner that reaches the model
// (.github/workflows/ai-review.yml, docs/PIPELINE.md).
//
// With FALLBACK_URL set, a second server (the laptop, llama-swap) reviews when the first doesn't answer at all:
// unreachable, key rejected, no first token in time, or an error before the first token. Once the first server
// writes, it stays the one: a review that runs too long or comes back empty is not repeated elsewhere.
//
// Advisory only — it never fails the job:
//   - Ollama unreachable (PC off)             -> next server, else skip
//   - no first token within FIRST_TOKEN_TIMEOUT_S (GPU busy / model swapping) -> next server, else skip
//   - whole review over TOTAL_TIMEOUT_S        -> skip
//   - model process dies while loading (HTTP 500) -> one retry after LOAD_RETRY_DELAY_S, then next server or skip
// Skips are posted to the PR so a missing review is visible, not buried in the job log.
// After every request the model is unloaded (keep_alive 0; llama-swap: its unload endpoint after the review) so
// the VRAM is free again, e.g. for games.
//
// Context sent with the diff: docs/ai-review-guide.md (project facts and rules), the full content of
// the changed files (so the model doesn't guess about code outside the hunks) and .gitignore. On a later push to
// the same PR also the last review and the verdicts on earlier ones (comments marked <!-- ai-review-verdicts -->,
// a comment anyone with write access puts on the PR), so a finding judged false isn't reported again.
//
// Without GITHUB_TOKEN/PR_NUMBER it prints to stdout instead (local dry run).
// Env: AI_API, AI_API_KEY, OLLAMA_URL, OLLAMA_MODEL, FALLBACK_URL, FALLBACK_API, FALLBACK_API_KEY, FALLBACK_MODEL,
//      FALLBACK_NAME, FALLBACK_FIRST_TOKEN_TIMEOUT_S, BASE_REF, FIRST_TOKEN_TIMEOUT_S, TOTAL_TIMEOUT_S, NUM_CTX,
//      NUM_PREDICT, MAX_DIFF_CHARS, MAX_PROMPT_CHARS, MAX_HISTORY_CHARS, LOAD_RETRY_DELAY_S, and for posting: GITHUB_API,
//      REPO, PR_NUMBER, GITHUB_TOKEN.
// The functions are exported for the tests (tools/ci/ai-review.test.mjs).
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const BASE_REF = process.env.BASE_REF || 'main';
const MAX_DIFF_CHARS = Number(process.env.MAX_DIFF_CHARS || 60000);
const FIRST_TOKEN_TIMEOUT_S = Number(process.env.FIRST_TOKEN_TIMEOUT_S || 120);
// The laptop reads prompts at ~360 tokens/s and loads in ~15 s: a 24K-token prompt takes ~85 s to the first token.
const FALLBACK_FIRST_TOKEN_TIMEOUT_S = Number(process.env.FALLBACK_FIRST_TOKEN_TIMEOUT_S || 180);
const TOTAL_TIMEOUT_S = Number(process.env.TOTAL_TIMEOUT_S || 600);
// Token budget, ~3.5 characters per token: MAX_PROMPT_CHARS (~24K tokens) + NUM_PREDICT < NUM_CTX.
// NUM_PREDICT includes thinking: qwen3.6 used up 6144 on the PR #4 diff and returned nothing, so 12K.
// It also caps models that loop. A bigger NUM_CTX costs VRAM: at 64K, devstral:24b spilled to
// 44% CPU and ran at 5 tokens/s. (All numbers here were measured in MiljanaMovingIn's model evaluation.)
const NUM_CTX = Number(process.env.NUM_CTX || 40960);
const NUM_PREDICT = Number(process.env.NUM_PREDICT || 12288);
const RETRY_NUM_PREDICT = Number(process.env.RETRY_NUM_PREDICT || 3072);
// OLLAMA_THINK=false turns thinking off (faster, but qwen3.6 then looped on a big diff); unset = model default.
// With thinking on, qwen3.6 sometimes spends all of NUM_PREDICT on thinking and answers nothing; the
// review then retries once with thinking off (7/7 planted bugs in 25 s in the eval, application code).
const THINK = process.env.OLLAMA_THINK ? process.env.OLLAMA_THINK === 'true' : undefined;
const MAX_PROMPT_CHARS = Number(process.env.MAX_PROMPT_CHARS || 85000);
// On 2026-10-05 qwen3.6 failed to load for an hour ("llama-server process has terminated … CUDA error:
// shared object initialization failed" in the flash-attention warmup), then loaded fine again. A crash
// while loading is retried once after this delay; it never reached generation, so nothing is repeated.
const LOAD_RETRY_DELAY_S = Number(process.env.LOAD_RETRY_DELAY_S ?? 15);
const LOAD_FAILURE = /llama-server process has terminated|llama runner process has terminated|failed to load model/i;
const MAX_FILE_CHARS = 30000;
// The last review and the verdicts on this PR's earlier reviews: the newest verdicts are kept when it is too long.
const MAX_HISTORY_CHARS = Number(process.env.MAX_HISTORY_CHARS || 8000);
// Only code is reviewed (the mod, the tools, the tests): on CI/infra diffs every model tested looped, answered
// nothing or gave generic advice. Space-separated git pathspecs.
const REVIEW_PATHS = (process.env.REVIEW_PATHS || 'mod/src tools tests').split(/\s+/).filter(Boolean);

/**
 * The model servers, tried in this order. api 'openai': Qwen3.8 27B with its MTP draft through llama-server wrote
 * 1.7× as fast as under Ollama, and the server caps thinking (--reasoning-budget), so it always answers.
 * llama-swap takes requests only with its key, sent with every request. The fallback is a second llama-swap (a
 * laptop), slower.
 */
export function servers(env = process.env) {
  const list = [{
    name: 'dev PC',
    api: env.AI_API === 'openai' ? 'openai' : 'ollama',
    url: env.OLLAMA_URL || 'http://localhost:11434',
    model: env.OLLAMA_MODEL || 'qwen3.6',
    key: env.AI_API_KEY,
    keySecret: 'AI_API_KEY',
    firstTokenS: FIRST_TOKEN_TIMEOUT_S,
  }];
  if (env.FALLBACK_URL) {
    list.push({
      name: env.FALLBACK_NAME || 'laptop',
      api: env.FALLBACK_API === 'ollama' ? 'ollama' : 'openai',
      url: env.FALLBACK_URL,
      model: env.FALLBACK_MODEL || 'qwen3.6-35b-a3b',
      key: env.FALLBACK_API_KEY,
      keySecret: 'AI_API_KEY_LAPTOP',
      firstTokenS: FALLBACK_FIRST_TOKEN_TIMEOUT_S,
    });
  }
  return list;
}
const SERVERS = servers();

const headers = (server) => ({
  'Content-Type': 'application/json',
  ...(server.key ? { Authorization: `Bearer ${server.key}` } : {}),
});
const label = (server) => (server.api === 'openai' ? 'Model server' : 'Ollama');

/** The server didn't answer at all: worth trying the next one. */
function unavailable(err) {
  err.unavailable = true;
  return err;
}
const GUIDE_FILE = 'docs/ai-review-guide.md';
const ALWAYS_INCLUDE = ['.gitignore'];
const MARKER = '<!-- ai-review -->';
export const VERDICTS = '<!-- ai-review-verdicts -->';
const NOTICE_START = '<!-- ai-review-notice -->';
const NOTICE_END = '<!-- /ai-review-notice -->';

export const SYSTEM_PROMPT = `You review pull requests for MW4VR, a mod that shows the game MechWarrior 4: Mercenaries in
a VR headset: a 32-bit Direct3D 11 proxy DLL in C++ (MSVC, Win32, OpenXR, HLSL shaders inside C++ strings) that
hooks the game's renderer, and Python tools that convert cockpit models.
You get the project review guide, the full content of the changed files (large ones are left out) and the diff.
Review ONLY the changes in the diff; the guide and the full files are context.

Report concrete problems, most severe first:
1. Bugs and logic errors (with the file and what goes wrong): wrong matrix or vector math, wrong buffer sizes and
   offsets, out-of-bounds access, use of a released COM object, a missing Release, state of the game's device
   context changed and not restored, uninitialised values, integer overflow
2. Security issues (unchecked sizes when reading files, path handling, secrets, unsafe defaults)
3. Violations of the project rules in the review guide
4. Missing or weak tests for changed behavior
5. Maintainability issues worth fixing now (skip pure style nits)

Evidence rules — a wrong finding costs more than a missed one:
- Only report what you can point to in the provided code. Name the file and quote the relevant line.
- Check the full file content before claiming something is missing, unset or not configured.
- Do not state facts about external tools, library versions, deprecations or APIs unless the
  provided material shows them. Treat the "facts" in the review guide as true.
- Do not report the accepted risks listed in the guide.
- If you are unsure, leave the finding out. At most 10 findings.

Earlier reviews of the same pull request may follow, with the author's verdicts on their findings (real, minor or
false, each with a reason). Do not report again a finding judged false, nor one judged real and since fixed, unless
the diff changes that code again so that it is wrong; a verdict's reason counts like the guide.

Write only the final findings, never your reasoning. Check each finding before you write it down; if it
turns out to be fine, leave it out completely. Never write a finding and then take it back ("No, …",
"This is correct", "Correction: …", "I will skip this").

Format: Markdown. One bullet per finding: **file** — problem (quote) — suggested fix.
If there is nothing significant, say so in one sentence. Do not praise, do not summarize the diff, be concise.`;

// Phrases with which a model (above all qwen3.6 with thinking off) takes back what it just wrote.
const RETRACTED = new RegExp(
  [
    'this is (fine|correct|expected|safe|consistent)',
    'this (seems|looks) (fine|correct)',
    'is actually (fine|correct|safe|valid)',
    'technically correct',
    'correction:',
    'i will skip',
    'no issue here',
    'not (a|an) (real )?(issue|bug|problem)',
    'which is fine',
  ].join('|'),
  'i',
);

// A finding starts with a bullet, a bold file name or a path followed by a dash.
const FINDING_START = /^(\s*[-*] |\*\*|[\w.@-]+\/[\w./@-]+\s+[—-])/;

/**
 * Drops the findings the model withdrew in the same breath ("Bug: … No, … This is correct."). Returns the rest
 * and how many were dropped; text that isn't findings (one "nothing to report" sentence) stays as it is.
 */
export function dropRetracted(text) {
  const blocks = [];
  for (const line of text.split('\n')) {
    if (blocks.length === 0 || FINDING_START.test(line)) blocks.push([line]);
    else blocks.at(-1).push(line);
  }
  const findings = blocks.map((b) => b.join('\n'));
  const kept = findings.filter((f) => !(FINDING_START.test(f) && RETRACTED.test(f)));
  return { text: kept.join('\n').trim(), dropped: findings.length - kept.length };
}

/**
 * Makes the model's answer inert before it goes into a PR comment: a code change could talk the model into writing
 * an image whose URL carries data off (GitHub loads images when the page is shown) or raw HTML. Images lose their URL
 * and keep their alt text, and `<` outside code starts no tag. Code spans and fenced blocks stay as they are.
 */
export function defang(text) {
  return text
    .split(/(```[\s\S]*?(?:```|$)|`[^`\n]*`)/)
    .map((part, i) =>
      i % 2
        ? part
        : part
            .replace(/!\[([^\]]*)\](\([^)]*\)|\[[^\]]*\])?/g, (_, alt) => `[image removed${alt ? `: ${alt}` : ''}]`)
            .replace(/<(?=[a-zA-Z/!?])/g, '&lt;'),
    )
    .join('');
}

export class SkipReview extends Error {}

function git(...args) {
  return execFileSync('git', args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

function prDiff() {
  const exclude = [':(exclude)**/package-lock.json', ':(exclude)**/*.lock'];
  // AI_REVIEW_DIFF_FILE: lets tests feed a fixed diff instead of the working tree's branch.
  let diff = process.env.AI_REVIEW_DIFF_FILE
    ? readFileSync(process.env.AI_REVIEW_DIFF_FILE, 'utf8')
    : git('diff', '--unified=3', `origin/${BASE_REF}...HEAD`, '--', ...REVIEW_PATHS, ...exclude);
  if (diff.length > MAX_DIFF_CHARS) {
    diff = diff.slice(0, MAX_DIFF_CHARS) + `\n\n[diff truncated at ${MAX_DIFF_CHARS} characters]`;
  }
  return diff;
}

// Paths of files that exist after the change, taken from the diff headers.
export function changedPaths(diff) {
  const paths = [];
  for (const match of diff.matchAll(/^\+\+\+ b\/(.+)$/gm)) paths.push(match[1].trim());
  return paths;
}

function readText(path) {
  if (!existsSync(path) || !statSync(path).isFile()) return null;
  const text = readFileSync(path, 'utf8');
  return text.includes('\0') ? null : text;
}

// Full files for the prompt, within the budget. Returns [{path, content}] and the paths left out.
export function collectFiles(paths, read = readText, budget = MAX_PROMPT_CHARS) {
  const files = [];
  const skipped = [];
  let used = 0;
  for (const path of [...new Set([...paths, ...ALWAYS_INCLUDE])]) {
    const content = read(path);
    if (content === null) continue;
    if (content.length > MAX_FILE_CHARS || used + content.length > budget) {
      skipped.push(path);
      continue;
    }
    files.push({ path, content });
    used += content.length;
  }
  return { files, skipped };
}

export function readGuide(read = readText) {
  return read(GUIDE_FILE) ?? '';
}

// What is left for full files once the system prompt, guide and diff are in.
export function fileBudget(diff, guide, history = '') {
  return Math.max(0, MAX_PROMPT_CHARS - SYSTEM_PROMPT.length - guide.length - diff.length - history.length);
}

/**
 * What earlier pushes left on the PR, for the prompt: the last review's findings (not a skip notice, not "nothing to
 * review") and the verdicts on earlier reviews, oldest first. Over `max` characters the oldest verdicts go first, then
 * the last review. '' when there is nothing.
 */
export function historyOf(comments, max = MAX_HISTORY_CHARS) {
  const parts = [];
  const last = comments.filter((c) => c.body.includes(MARKER)).at(-1);
  if (last) {
    const findings = stripNotice(last.body)
      .replace(MARKER, '')
      .replace(/\n---\n<sub>[\s\S]*$/, '')
      .trim();
    if (/^### /.test(findings) && !/nothing to review/i.test(findings)) {
      parts.push(findings.replace(/^### .*$/m, (h) => `${h} — the last review`));
    }
  }
  const verdicts = comments
    .filter((c) => c.body.includes(VERDICTS))
    .map((c) => c.body.replace(VERDICTS, '').trim());
  const fits = () => [...parts, ...verdicts].join('\n\n').length <= max;
  while (verdicts.length && !fits()) verdicts.shift();
  if (!fits()) parts.length = 0;
  return [...parts, ...verdicts].join('\n\n');
}

export function buildMessages({
  diff, files = [], skipped = [], guide = '', history = '', systemPrompt = SYSTEM_PROMPT,
}) {
  const fence = '````';
  const parts = [];
  if (guide) parts.push(`## Project review guide\n\n${guide.trim()}`);
  if (files.length) {
    const blocks = files.map((f) => `### ${f.path}\n\n${fence}\n${f.content}\n${fence}`);
    parts.push(`## Full content of the changed files (after the change)\n\n${blocks.join('\n\n')}`);
  }
  if (skipped.length) parts.push(`(Not shown in full, too large: ${skipped.join(', ')})`);
  if (history) parts.push(`## Earlier reviews of this pull request and the author's verdicts\n\n${history}`);
  parts.push(`## Diff to review\n\n\`\`\`diff\n${diff}\n\`\`\``);
  return [
    { role: 'system', content: systemPrompt },
    { role: 'user', content: parts.join('\n\n') },
  ];
}

export async function assertReachable(server = SERVERS[0]) {
  let res;
  try {
    res = await fetch(`${server.url}${server.api === 'openai' ? '/v1/models' : '/api/tags'}`, {
      headers: headers(server),
      signal: AbortSignal.timeout(5000),
    });
  } catch (err) {
    res = null;
  }
  if (res?.status === 401) {
    throw unavailable(new SkipReview(`${label(server)} rejected the key (secret ${server.keySecret})`));
  }
  // Without the server's address: the reason goes into a comment on the pull request, and the address is a home
  // network's.
  if (!res?.ok) throw unavailable(new SkipReview(`${label(server)} not reachable (${server.name} off?)`));
}

async function unloadModel(server, model) {
  // Best effort: free the VRAM even when a request was aborted mid-generation.
  try {
    const [path, body] =
      server.api === 'openai'
        ? [`/api/models/unload/${encodeURIComponent(model)}`, undefined]
        : ['/api/generate', JSON.stringify({ model, keep_alive: 0 })];
    await fetch(`${server.url}${path}`, {
      method: 'POST',
      headers: headers(server),
      body,
      signal: AbortSignal.timeout(10000),
    });
  } catch {
    // Ollama (and llama-swap, after its ttl) unloads idle models on its own eventually.
  }
}

// Models that loop repeat the same findings word for word; keep the first copy of each paragraph.
export function dedupeParagraphs(text) {
  const seen = new Set();
  return text
    .split(/\n{2,}/)
    .filter((p) => {
      const key = p.trim();
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .join('\n\n');
}

class ModelLoadFailed extends Error {}

/** The chat request, streamed: Ollama's /api/chat, or OpenAI's /v1/chat/completions as llama-server takes it. */
export function requestBody(model, think, numPredict, messages, api = SERVERS[0].api) {
  if (api === 'openai') {
    return {
      model,
      stream: true,
      temperature: 0.2,
      max_tokens: numPredict,
      ...(think === undefined ? {} : { chat_template_kwargs: { enable_thinking: think } }),
      messages,
    };
  }
  return {
    model,
    stream: true,
    keep_alive: 0,
    ...(think === undefined ? {} : { think }),
    options: { temperature: 0.2, num_ctx: NUM_CTX, num_predict: numPredict },
    messages,
  };
}

/** One line of the stream as an event, or null: blank lines, and the "data: [DONE]" that ends server-sent events. */
export function parseEvent(line, api = SERVERS[0].api) {
  if (!line) return null;
  if (api !== 'openai') return JSON.parse(line);
  if (!line.startsWith('data:')) return null;
  const data = line.slice('data:'.length).trim();
  return data === '[DONE]' ? null : JSON.parse(data);
}

export async function review(
  messages, model = SERVERS[0].model, think = THINK, numPredict = NUM_PREDICT, server = SERVERS[0],
) {
  try {
    return await reviewOnce(messages, model, think, numPredict, server);
  } catch (err) {
    if (!(err instanceof ModelLoadFailed)) throw err;
    console.log(`::notice::${model} failed to load, retrying once in ${LOAD_RETRY_DELAY_S}s (${err.message})`);
    await new Promise((resolve) => setTimeout(resolve, LOAD_RETRY_DELAY_S * 1000));
    return reviewOnce(messages, model, think, numPredict, server);
  }
}

async function reviewOnce(messages, model, think, numPredict, server) {
  const api = server.api;
  let started = false;
  const controller = new AbortController();
  let reason = null;
  const abortWith = (why) => {
    reason = why;
    controller.abort();
  };
  const firstTokenTimer = setTimeout(
    () => abortWith(`no response within ${server.firstTokenS}s — GPU busy or model still loading`),
    server.firstTokenS * 1000,
  );
  const totalTimer = setTimeout(
    () => abortWith(`review exceeded ${TOTAL_TIMEOUT_S}s`),
    TOTAL_TIMEOUT_S * 1000,
  );

  try {
    const res = await fetch(`${server.url}${api === 'openai' ? '/v1/chat/completions' : '/api/chat'}`, {
      method: 'POST',
      headers: headers(server),
      signal: controller.signal,
      body: JSON.stringify(requestBody(model, think, numPredict, messages, api)),
    });
    if (!res.ok) {
      const text = await res.text();
      const message = `${label(server)} returned HTTP ${res.status}: ${text}`;
      throw res.status === 500 && LOAD_FAILURE.test(text) ? new ModelLoadFailed(message) : new Error(message);
    }

    // NDJSON (Ollama) or server-sent events (OpenAI). Any first chunk (thinking or content) proves the model is
    // loaded and generating.
    const decoder = new TextDecoder();
    let buffer = '';
    let content = '';
    for await (const chunk of res.body) {
      clearTimeout(firstTokenTimer);
      started = true;
      buffer += decoder.decode(chunk, { stream: true });
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const event = parseEvent(buffer.slice(0, newline).trim(), api);
        buffer = buffer.slice(newline + 1);
        if (!event) continue;
        if (event.error) throw new Error(`${label(server)}: ${event.error.message ?? event.error}`);
        if (api === 'openai') {
          content += event.choices?.[0]?.delta?.content ?? '';
          if (event.timings) console.log(`::notice::${serverTimings(model, event.timings)}`);
        } else {
          content += event.message?.content ?? '';
          if (event.done) console.log(`::notice::${timings(model, event)}`);
        }
      }
    }
    return dedupeParagraphs(content.trim());
  } catch (err) {
    if (reason) {
      await unloadModel(server, model);
      const skip = new SkipReview(reason);
      throw started ? skip : unavailable(skip);
    }
    throw started ? err : unavailable(err);
  } finally {
    clearTimeout(firstTokenTimer);
    clearTimeout(totalTimer);
  }
}

/**
 * Where the time went, from Ollama's last chunk (durations in ns): loading, reading the prompt (prefill) and writing
 * (decode, thinking included). Prefill and decode are different bottlenecks: long diffs are read, short answers written.
 */
export function timings(model, e) {
  const s = (ns) => (ns ?? 0) / 1e9;
  const rate = (n, ns) => (s(ns) > 0 ? Math.round((n ?? 0) / s(ns)) : 0);
  return (
    `${model}: load ${s(e.load_duration).toFixed(1)} s, ` +
    `prompt ${e.prompt_eval_count ?? 0} tokens in ${s(e.prompt_eval_duration).toFixed(1)} s ` +
    `(${rate(e.prompt_eval_count, e.prompt_eval_duration)} tokens/s), ` +
    `answer ${e.eval_count ?? 0} tokens in ${s(e.eval_duration).toFixed(1)} s ` +
    `(${rate(e.eval_count, e.eval_duration)} tokens/s), total ${s(e.total_duration).toFixed(1)} s`
  );
}

/**
 * The same from llama-server's timings (in ms; loading is llama-swap's and not counted), with how many of the MTP
 * draft's tokens the model took: the share decides how much faster it writes.
 */
export function serverTimings(model, t) {
  const s = (ms) => (ms ?? 0) / 1000;
  const rate = (n, ms) => (s(ms) > 0 ? Math.round((n ?? 0) / s(ms)) : 0);
  const draft = t.draft_n ? `, draft ${t.draft_n_accepted ?? 0} of ${t.draft_n} tokens taken` : '';
  return (
    `${model}: prompt ${t.prompt_n ?? 0} tokens in ${s(t.prompt_ms).toFixed(1)} s ` +
    `(${rate(t.prompt_n, t.prompt_ms)} tokens/s), ` +
    `answer ${t.predicted_n ?? 0} tokens in ${s(t.predicted_ms).toFixed(1)} s ` +
    `(${rate(t.predicted_n, t.predicted_ms)} tokens/s)${draft}`
  );
}

async function github(path, init = {}) {
  const res = await fetch(`${process.env.GITHUB_API || 'https://api.github.com'}/repos/${process.env.REPO}${path}`, {
    ...init,
    headers: {
      Authorization: `Bearer ${process.env.GITHUB_TOKEN}`,
      Accept: 'application/vnd.github+json',
      'X-GitHub-Api-Version': '2022-11-28',
      'User-Agent': 'mw4vr-ai-review',
      'Content-Type': 'application/json',
    },
  });
  if (!res.ok) throw new Error(`GitHub ${init.method || 'GET'} ${path} -> HTTP ${res.status}: ${await res.text()}`);
  return res.status === 204 ? null : res.json();
}

const canPost = () => Boolean(process.env.GITHUB_TOKEN && process.env.PR_NUMBER);

async function existingComment() {
  const comments = await github(`/issues/${process.env.PR_NUMBER}/comments?per_page=100`);
  return comments.find((c) => c.body.includes(MARKER));
}

async function upsertComment(body) {
  const pr = process.env.PR_NUMBER;
  const existing = await existingComment();
  if (existing) {
    await github(`/issues/comments/${existing.id}`, { method: 'PATCH', body: JSON.stringify({ body }) });
    console.log(`Updated AI review comment ${existing.id} on PR #${pr}`);
  } else {
    await github(`/issues/${pr}/comments`, { method: 'POST', body: JSON.stringify({ body }) });
    console.log(`Posted AI review comment on PR #${pr}`);
  }
}

function stripNotice(body) {
  const start = body.indexOf(NOTICE_START);
  const end = body.indexOf(NOTICE_END);
  return start >= 0 && end > start ? body.slice(0, start) + body.slice(end + NOTICE_END.length).replace(/^\n+/, '') : body;
}

async function reportSkip(reason, sha) {
  console.log(`::warning::AI review skipped: ${reason}`);
  if (!canPost()) return;
  const notice = `${NOTICE_START}\n> ⚠️ **AI review skipped for commit ${sha}:** ${reason}.\n` +
    `> Re-run the "AI review" job once the model is available.\n${NOTICE_END}\n`;
  const existing = await existingComment();
  if (existing) {
    // Keep the last successful review below the notice; it still describes most of the PR.
    const previous = stripNotice(existing.body).replace(MARKER, '').trimStart();
    await upsertComment(`${MARKER}\n${notice}\n${previous}`);
  } else {
    await upsertComment(`${MARKER}\n${notice}`);
  }
}

async function main() {
  const sha = git('rev-parse', '--short', 'HEAD').trim();
  const diff = prDiff();
  // No hunks = nothing to read (empty, or only file mode changes like chmod +x).
  if (!/^@@ /m.test(diff)) {
    const note = `No application code changed (reviewed paths: ${REVIEW_PATHS.join(', ')}), nothing to review.`;
    console.log(note);
    // Replace an earlier review so the PR doesn't show findings for code that is no longer in it.
    if (canPost()) await upsertComment(`${MARKER}\n### 🤖 AI review (commit ${sha})\n\n${note}`);
    return;
  }

  let messages;
  try {
    const guide = readGuide();
    const history = canPost()
      ? historyOf(await github(`/issues/${process.env.PR_NUMBER}/comments?per_page=100`))
      : '';
    if (history) console.log(`::notice::Earlier reviews and verdicts in the prompt: ${history.length} characters`);
    const { files, skipped } = collectFiles(changedPaths(diff), readText, fileBudget(diff, guide, history));
    messages = buildMessages({ diff, files, skipped, guide, history });
  } catch (err) {
    await reportSkip(`review failed (${err.message})`, sha);
    return;
  }
  const missed = [];
  for (const [i, server] of SERVERS.entries()) {
    const next = SERVERS[i + 1];
    try {
      await reviewOn(server, i > 0, messages, sha);
      return;
    } catch (err) {
      const reason = err instanceof SkipReview ? err.message : `review failed (${err.message})`;
      if (err.unavailable && next) {
        console.log(`::notice::${server.name} unavailable (${reason}), trying the ${next.name}`);
        missed.push(`${server.name}: ${reason}`);
        continue;
      }
      await reportSkip(missed.length ? [...missed, `${server.name}: ${reason}`].join('; ') : reason, sha);
      return;
    } finally {
      // Ollama unloads with every request (keep_alive 0); llama-swap would hold the GPU until its ttl.
      if (server.api === 'openai') await unloadModel(server, server.model);
    }
  }
}

/** Reviews on one server and posts the review; throws when it can't (err.unavailable: the next server may). */
async function reviewOn(server, isFallback, messages, sha) {
  await assertReachable(server);
  const started = Date.now();
  let text = await review(messages, server.model, THINK, NUM_PREDICT, server);
  let mode = '';
  if (!text && THINK !== false) {
    console.log('::notice::Empty answer (thinking used up NUM_PREDICT?), retrying once with thinking off');
    // Without thinking qwen3.6 tends to repeat itself on long answers: the cap keeps the first findings.
    text = await review(messages, server.model, false, RETRY_NUM_PREDICT, server);
    mode = ', thinking off';
  }
  if (!text) throw new SkipReview(`${server.model} returned an empty review, also with thinking off`);
  const { text: findings, dropped } = dropRetracted(text);
  const body = defang(findings) || 'Nothing significant to report.';
  const withdrawn = dropped ? ` ${dropped} finding${dropped === 1 ? '' : 's'} the model took back left out.` : '';
  const secs = Math.round((Date.now() - started) / 1000);
  const where = isFallback ? ` on the ${server.name}` : '';
  const comment = `${MARKER}\n### 🤖 AI review (local \`${server.model}\`${where}${mode}, commit ${sha})\n\n${body}\n\n---\n` +
    `<sub>Advisory, generated by a local LLM in ${secs}s — may be wrong.${withdrawn} Updated on every push.</sub>`;
  if (canPost()) await upsertComment(comment);
  else console.log(comment);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((err) => {
    // Even reporting failed (e.g. GitHub API down): warn, never fail the job.
    console.log(`::warning::AI review could not report: ${err.message}`);
  });
}
