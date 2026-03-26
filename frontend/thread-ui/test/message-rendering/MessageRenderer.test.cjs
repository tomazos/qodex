const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const { execFileSync } = require('node:child_process');

const { JSDOM } = require('jsdom');

const { createMessageRenderer } = require('../../message-rendering/MessageRenderer');

const dom = new JSDOM('<!doctype html><html><body></body></html>');
const renderer = createMessageRenderer({ domWindow: dom.window });

function normalizeHtml(html) {
  return renderer.normalizeHtmlFragment(html);
}

function renderMessage(text) {
  return renderer.renderToHtmlFragment(text);
}

function assertHtmlContains(html, needle) {
  assert.ok(
    html.includes(needle),
    `Expected rendered HTML to contain ${JSON.stringify(needle)}.\nActual HTML:\n${html}`
  );
}

test('returns an empty fragment for empty input', () => {
  assert.equal(renderMessage(''), '');
});

test('escapes raw HTML input instead of rendering it', () => {
  const html = renderMessage('<script>alert(1)</script><b>unsafe</b>');
  assert.match(html, /&lt;script&gt;alert\(1\)&lt;\/script&gt;/);
  assert.doesNotMatch(html, /<script>/i);
  assert.doesNotMatch(html, /<b>unsafe<\/b>/i);
});

test('renders markdown emphasis, lists, and fenced code blocks', () => {
  const html = renderMessage([
    '# Heading',
    '',
    '- one',
    '- two',
    '',
    '```js',
    'console.log("hi");',
    '```',
  ].join('\n'));

  assertHtmlContains(html, '<h1>Heading</h1>');
  assertHtmlContains(html, '<li>one</li>');
  assertHtmlContains(html, '<code class="language-js">console.log("hi");');
});

test('renders GFM-style tables', () => {
  const html = renderMessage([
    '| Col A | Col B |',
    '| --- | --- |',
    '| 1 | 2 |',
  ].join('\n'));

  assertHtmlContains(html, '<table>');
  assertHtmlContains(html, '<th>Col A</th>');
  assertHtmlContains(html, '<td>2</td>');
});

test('renders inline math with KaTeX', () => {
  const html = renderMessage('Einstein wrote $E = mc^2$.');

  assertHtmlContains(html, 'class="katex"');
  assertHtmlContains(html, '<math');
  assertHtmlContains(html, 'E = mc^2');
});

test('renders block math with KaTeX display output', () => {
  const html = renderMessage([
    '$$',
    '\\int_0^1 x^2 dx',
    '$$',
  ].join('\n'));

  assertHtmlContains(html, 'class="katex-display"');
  assertHtmlContains(html, '<math');
});

test('renders LaTeX bracket delimiters used by Codex answers', () => {
  const html = renderMessage([
    'For a vector \\(z = (z_1, z_2, \\dots, z_n)\\), the softmax of component \\(i\\) is:',
    '',
    '\\[',
    '\\mathrm{softmax}(z_i) = \\frac{e^{z_i}}{\\sum_{j=1}^{n} e^{z_j}}',
    '\\]',
  ].join('\n'));

  assertHtmlContains(html, 'class="katex"');
  assertHtmlContains(html, 'class="katex-display"');
  assertHtmlContains(html, '\\mathrm{softmax}(z_i)');
});

test('does not treat escaped dollar signs or currency values as math', () => {
  const html = renderMessage('Price is \\$5.00 and this is not math: $5.00');

  assert.match(html, /\$5\.00/);
  assert.doesNotMatch(html, /class="katex"/);
});

test('renders malformed TeX safely without throwing', () => {
  const html = renderMessage('Broken math: $\\notacommand{');

  assert.ok(html.length > 0);
  assert.match(html, /katex-error|\\notacommand/);
});

test('rejects javascript links from markdown link syntax', () => {
  const html = renderMessage('[click](javascript:alert(1))');

  assert.doesNotMatch(html, /href\s*=\s*"javascript:/i);
  assert.doesNotMatch(html, /href\s*=\s*'javascript:/i);
});

test('allows file links through sanitization for local-file opening', () => {
  const html = renderMessage('[local file](file:///tmp/example.txt)');

  assertHtmlContains(html, 'href="file:///tmp/example.txt"');
});

test('does not allow dangerous KaTeX trust-required commands through', () => {
  const html = renderMessage('$\\href{javascript:alert(1)}{owned}$');

  assert.doesNotMatch(html, /<a\b/i);
  assert.doesNotMatch(html, /href\s*=\s*"javascript:/i);
});

function loadRealWorldCorpusFromLocalDb() {
  const defaultDbPath = path.join(
    process.env.HOME || '',
    '.local',
    'share',
    'Tomazos.com',
    'Qodex',
    'qodex.sqlite3'
  );
  const dbPath = process.env.QODEX_RENDERER_TEST_DB || defaultDbPath;
  if (!dbPath || !fs.existsSync(dbPath)) {
    return [];
  }

  let stdout = '';
  try {
    stdout = execFileSync(
      'sqlite3',
      [
        '-readonly',
        '-json',
        dbPath,
        'SELECT payload_json FROM api_log ORDER BY id DESC LIMIT 500',
      ],
      {
        encoding: 'utf8',
        timeout: 5000,
      }
    );
  } catch {
    return [];
  }

  let rows = [];
  try {
    rows = JSON.parse(stdout);
  } catch {
    return [];
  }

  const messages = [];
  const seen = new Set();

  function addMessage(candidate) {
    if (typeof candidate !== 'string') {
      return;
    }

    const normalized = candidate.trim();
    if (normalized.length === 0 || seen.has(normalized)) {
      return;
    }

    seen.add(normalized);
    messages.push(normalized);
  }

  function walk(value) {
    if (Array.isArray(value)) {
      for (const element of value) {
        walk(element);
      }
      return;
    }

    if (value == null || typeof value !== 'object') {
      return;
    }

    if ((value.kind === 'agentMessage' || value.phase === 'commentary' || value.phase === 'final_answer') &&
        typeof value.text === 'string') {
      addMessage(value.text);
    }

    if (value.kind === 'userMessage' && Array.isArray(value.content)) {
      for (const contentItem of value.content) {
        if (contentItem && typeof contentItem.text === 'string') {
          addMessage(contentItem.text);
        }
      }
    }

    for (const nestedValue of Object.values(value)) {
      walk(nestedValue);
    }
  }

  for (const row of rows) {
    if (!row || typeof row.payload_json !== 'string') {
      continue;
    }

    try {
      walk(JSON.parse(row.payload_json));
    } catch {
      // Ignore malformed rows in the optional local corpus pass.
    }
  }

  return messages.slice(0, 100);
}

test('renders the optional local real-world message corpus without unsafe output', { skip: false }, (t) => {
  const corpus = loadRealWorldCorpusFromLocalDb();
  if (corpus.length === 0) {
    t.skip('No local qodex message corpus was available.');
    return;
  }

  for (const message of corpus) {
    const html = renderMessage(message);
    assert.ok(typeof html === 'string');
    assert.doesNotMatch(html, /<script\b/i);
    assert.doesNotMatch(html, /href\s*=\s*"javascript:/i);
  }
});

test('renders large mixed markdown input without throwing or emitting unsafe links', () => {
  const largeMessage = [
    '# Transcript Stress Test',
    '',
    ...Array.from({ length: 200 }, (_, index) => `- item ${index + 1}: $x_${index} = y_${index}^2$`),
    '',
    '```text',
    'This is a large fenced block.',
    '```',
  ].join('\n');

  const html = renderMessage(largeMessage);
  assert.ok(html.length > 0);
  assert.doesNotMatch(html, /<script\b/i);
  assert.doesNotMatch(html, /href\s*=\s*"javascript:/i);
});
