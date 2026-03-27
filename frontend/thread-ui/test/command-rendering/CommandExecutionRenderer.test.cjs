const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createCommandExecutionRenderer } = require('../../command-rendering/CommandExecutionRenderer');

function renderCommandExecution(commandExecution) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createCommandExecutionRenderer({
    domWindow: dom.window,
    shellIdentity: {
      username: 'zos',
      hostname: 'zoidberg',
    },
  });
  return renderer.renderToElement(commandExecution);
}

test('renders title, shell prompt, footer status, and output', () => {
  const element = renderCommandExecution({
    command: '/bin/bash -lc "date"',
    cwd: '/home/zos',
    status: 'completed',
    hasExitCode: true,
    exitCode: 0n,
    hasDurationMs: true,
    durationMs: 215n,
    processId: '12345',
    aggregatedOutput: 'Wed Mar 25 19:35:16 AEST 2026\n',
    actions: [
      {
        kind: 'read',
        path: '/home/zos/file.txt',
        query: '',
        name: 'file.txt',
        command: 'cat /home/zos/file.txt',
      },
    ],
  });

  assert.equal(element.querySelector('.command-execution__title').textContent, 'Read /home/zos/file.txt');
  assert.equal(element.querySelector('.command-execution__title strong').textContent, 'Read');
  assert.equal(element.querySelector('.command-execution__title a').getAttribute('href'), '/home/zos/file.txt');
  assert.equal(
    element.querySelector('.command-execution__prompt').textContent,
    'zos@zoidberg:~$ date'
  );
  assert.equal(element.querySelector('.command-execution__badge').textContent, 'Completed');
  assert.match(element.querySelector('.command-execution__meta').textContent, /exit 0/);
  assert.match(element.querySelector('.command-execution__meta').textContent, /215 ms/);
  assert.match(element.querySelector('.command-execution__meta').textContent, /pid 12345/);
  assert.match(element.querySelector('.command-execution__output').textContent, /Wed Mar 25/);
  assert.equal(element.lastElementChild.className, 'command-execution__footer');
});

test('renders failed commands with their failure badge and preserves output', () => {
  const element = renderCommandExecution({
    command: 'ctest --output-on-failure',
    cwd: '/home/zos/qodex/build',
    status: 'failed',
    hasExitCode: true,
    exitCode: 8n,
    hasDurationMs: false,
    durationMs: 0n,
    processId: '',
    aggregatedOutput: '1/1 Test #9: ThreadUiFileChangeRendererTest ...***Failed',
    actions: [],
  });

  assert.ok(element.querySelector('.command-execution__badge--failed'));
  assert.match(element.querySelector('.command-execution__output').textContent, /\*\*\*Failed/);
});

test('keeps non-home cwd values and falls back to a generic title when there is no action label', () => {
  const element = renderCommandExecution({
    command: 'ctest --output-on-failure',
    cwd: '/tmp/build',
    status: 'completed',
    hasExitCode: false,
    exitCode: 0n,
    hasDurationMs: false,
    durationMs: 0n,
    processId: '',
    aggregatedOutput: 'ok\n',
    actions: [],
  });

  assert.equal(element.querySelector('.command-execution__title').textContent, 'Run command');
  assert.equal(
    element.querySelector('.command-execution__prompt').textContent,
    'zos@zoidberg:/tmp/build$ ctest --output-on-failure'
  );
});

test('renders a no-output placeholder when the command produced no aggregated output', () => {
  const element = renderCommandExecution({
    command: 'true',
    cwd: '/tmp',
    status: 'completed',
    hasExitCode: false,
    exitCode: 0n,
    hasDurationMs: false,
    durationMs: 0n,
    processId: '',
    aggregatedOutput: '',
    actions: [],
  });

  assert.equal(element.querySelector('.command-execution__output').textContent, '(no output)');
});

test('renders list files and search titles from structured command actions', () => {
  const listElement = renderCommandExecution({
    command: 'ls src',
    cwd: '/home/zos/qodex',
    status: 'completed',
    hasExitCode: false,
    exitCode: 0n,
    hasDurationMs: false,
    durationMs: 0n,
    processId: '',
    aggregatedOutput: '',
    actions: [
      {
        kind: 'listFiles',
        path: 'src',
        query: '',
        name: '',
        command: 'ls src',
      },
    ],
  });

  assert.equal(listElement.querySelector('.command-execution__title').textContent, 'List src');
  assert.equal(listElement.querySelector('.command-execution__title strong').textContent, 'List');
  assert.equal(listElement.querySelector('.command-execution__title a').getAttribute('href'), 'src');

  const searchElement = renderCommandExecution({
    command: 'rg ThreadUi src',
    cwd: '/home/zos/qodex',
    status: 'completed',
    hasExitCode: false,
    exitCode: 0n,
    hasDurationMs: false,
    durationMs: 0n,
    processId: '',
    aggregatedOutput: '',
    actions: [
      {
        kind: 'search',
        path: 'src',
        query: 'ThreadUi',
        name: '',
        command: 'rg ThreadUi src',
      },
    ],
  });

  assert.equal(searchElement.querySelector('.command-execution__title').textContent, 'Search ThreadUi in src');
  assert.equal(searchElement.querySelector('.command-execution__title strong').textContent, 'Search');
  assert.equal(searchElement.querySelector('.command-execution__title a').getAttribute('href'), 'src');
});
