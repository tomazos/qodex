const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createCommandExecutionRenderer } = require('../../command-rendering/CommandExecutionRenderer');

function renderCommandExecution(commandExecution) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createCommandExecutionRenderer({ domWindow: dom.window });
  return renderer.renderToElement(commandExecution);
}

test('renders command, cwd, status badge, and output', () => {
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
    actionLabels: ['Read /home/zos/file.txt'],
  });

  assert.equal(element.querySelector('.command-execution__badge').textContent, 'Completed');
  assert.match(element.querySelector('.command-execution__meta').textContent, /exit 0/);
  assert.match(element.querySelector('.command-execution__meta').textContent, /215 ms/);
  assert.match(element.querySelector('.command-execution__meta').textContent, /pid 12345/);
  assert.equal(element.querySelector('.command-execution__command').textContent, '/bin/bash -lc "date"');
  assert.equal(element.querySelector('.command-execution__cwd').textContent, '/home/zos');
  assert.match(element.querySelector('.command-execution__output').textContent, /Wed Mar 25/);
  assert.equal(element.querySelector('.command-execution__action').textContent, 'Read /home/zos/file.txt');
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
    actionLabels: [],
  });

  assert.ok(element.querySelector('.command-execution__badge--failed'));
  assert.match(element.querySelector('.command-execution__output').textContent, /\*\*\*Failed/);
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
    actionLabels: [],
  });

  assert.equal(element.querySelector('.command-execution__output').textContent, '(no output)');
});
