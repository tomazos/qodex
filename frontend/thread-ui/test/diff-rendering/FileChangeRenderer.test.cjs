const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createFileChangeRenderer, parseUnifiedDiff } = require('../../diff-rendering/FileChangeRenderer');

function renderFileChange(fileChange) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createFileChangeRenderer({ domWindow: dom.window });
  return renderer.renderToElement(fileChange);
}

test('parses unified diff rows for update changes', () => {
  const rows = parseUnifiedDiff([
    'diff --git a/styles.css b/styles.css',
    'index 123..456 100644',
    '--- a/styles.css',
    '+++ b/styles.css',
    '@@ -1,2 +1,2 @@',
    '-old',
    '+new',
    ' context',
  ].join('\n'));

  assert.ok(rows);
  assert.deepEqual(
    rows.map((row) => row.type),
    ['meta', 'meta', 'file-header', 'file-header', 'hunk', 'removed', 'added', 'context']
  );
});

test('renders added file contents as added diff lines', () => {
  const element = renderFileChange({
    status: 'completed',
    changes: [
      {
        path: '/tmp/new.txt',
        kind: 'add',
        diff: 'first line\nsecond line\n',
      },
    ],
  });

  const addedLines = [...element.querySelectorAll('.file-change__line--added')].map((line) => line.textContent);
  assert.deepEqual(addedLines, ['first line', 'second line']);
  assert.equal(element.querySelector('.file-change__badge').textContent, 'Add');
});

test('renders update diffs with removed, added, and hunk rows', () => {
  const element = renderFileChange({
    status: 'completed',
    changes: [
      {
        path: '/tmp/styles.css',
        kind: 'update',
        diff: [
          '--- a/styles.css',
          '+++ b/styles.css',
          '@@ -1 +1 @@',
          '-color: Highlight;',
          '+color: LinkText;',
        ].join('\n'),
      },
    ],
  });

  assert.equal(element.querySelector('.file-change__path').textContent, '/tmp/styles.css');
  assert.equal(element.querySelector('.file-change__badge').textContent, 'Update');
  assert.ok(element.querySelector('.file-change__line--removed'));
  assert.ok(element.querySelector('.file-change__line--added'));
  assert.ok(element.querySelector('.file-change__line--hunk'));
});

test('renders rename targets in the header for update changes', () => {
  const element = renderFileChange({
    status: 'completed',
    changes: [
      {
        path: '/tmp/old-name.txt',
        kind: 'update',
        movePath: '/tmp/new-name.txt',
        diff: [
          'diff --git a/old-name.txt b/new-name.txt',
          'rename from old-name.txt',
          'rename to new-name.txt',
          '@@ -0,0 +1 @@',
          '+renamed',
        ].join('\n'),
      },
    ],
  });

  assert.equal(
    element.querySelector('.file-change__path').textContent,
    '/tmp/old-name.txt -> /tmp/new-name.txt'
  );
});

test('renders move-style update diffs even with trailing move metadata', () => {
  const element = renderFileChange({
    status: 'completed',
    changes: [
      {
        path: '/tmp/second-add.txt',
        kind: 'update',
        movePath: '/tmp/renamed-note.txt',
        diff: [
          '@@ -3,2 +3,2 @@',
          ' ',
          '-This file was added in a later batch so you can inspect another add event.',
          '+This file was renamed in a later batch so you can inspect a move-style update event.',
          '',
          '',
          'Moved to: /tmp/renamed-note.txt',
        ].join('\n'),
      },
    ],
  });

  assert.ok(element.querySelector('.file-change__line--removed'));
  assert.ok(element.querySelector('.file-change__line--added'));
  assert.ok(element.querySelector('.file-change__line--meta'));
  assert.equal(element.querySelector('.file-change__raw'), null);
});

test('falls back to raw rendering for malformed update diffs', () => {
  const element = renderFileChange({
    status: 'completed',
    changes: [
      {
        path: '/tmp/bad.patch',
        kind: 'update',
        diff: 'plain text that is not a unified diff',
      },
    ],
  });

  const raw = element.querySelector('.file-change__raw');
  assert.ok(raw);
  assert.match(raw.textContent, /plain text that is not a unified diff/);
});

test('shows non-completed patch status when present', () => {
  const element = renderFileChange({
    status: 'failed',
    changes: [],
  });

  assert.equal(element.querySelector('.file-change__status').textContent, 'Patch status: Failed');
});
