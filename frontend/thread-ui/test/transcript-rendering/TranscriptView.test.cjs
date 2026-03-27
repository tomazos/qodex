const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createTranscriptView } = require('../../transcript-rendering/TranscriptView');

function createView() {
  const dom = new JSDOM(`
    <!doctype html>
    <html>
      <body>
        <div id="thread-items">
          <p id="thread-empty-state">Empty</p>
        </div>
      </body>
    </html>
  `);

  const messageRenderer = {
    renderToHtmlFragment(text) {
      return `<p>${text}</p>`;
    },
  };
  const commandExecutionRenderer = {
    renderToElement(item) {
      const element = dom.window.document.createElement('section');
      element.className = 'command-execution';
      element.textContent = item.commandExecution?.command || '';
      return element;
    },
  };
  const fileChangeRenderer = {
    renderToElement(item) {
      const element = dom.window.document.createElement('section');
      element.className = 'file-change';
      element.textContent = item.fileChange?.status || '';
      return element;
    },
  };

  const container = dom.window.document.getElementById('thread-items');
  const emptyStateElement = dom.window.document.getElementById('thread-empty-state');
  const transcriptView = createTranscriptView({
    domWindow: dom.window,
    container,
    emptyStateElement,
    messageRenderer,
    commandExecutionRenderer,
    fileChangeRenderer,
  });

  return {
    dom,
    container,
    transcriptView,
  };
}

test('upserts items by stable id without recreating unaffected nodes', () => {
  const { container, transcriptView } = createView();

  transcriptView.upsertItems([
    { id: 'user-1', kind: 'user', text: 'Hello' },
    { id: 'agent-1', kind: 'agent', text: 'Original reply' },
  ]);

  const userNodeBefore = container.querySelector('[data-item-id="user-1"]');
  const agentNodeBefore = container.querySelector('[data-item-id="agent-1"]');

  assert.ok(userNodeBefore);
  assert.ok(agentNodeBefore);
  assert.equal(container.querySelectorAll('.thread-item').length, 2);

  transcriptView.upsertItems([
    { id: 'agent-1', kind: 'agent', text: 'Updated reply' },
  ]);

  const userNodeAfter = container.querySelector('[data-item-id="user-1"]');
  const agentNodeAfter = container.querySelector('[data-item-id="agent-1"]');

  assert.strictEqual(userNodeAfter, userNodeBefore);
  assert.strictEqual(agentNodeAfter, agentNodeBefore);
  assert.match(agentNodeAfter.innerHTML, /Updated reply/);
  assert.equal(container.querySelectorAll('.thread-item').length, 2);
});

test('falls back to anonymous ids for legacy items without ids', () => {
  const { container, transcriptView } = createView();

  transcriptView.upsertItems([
    { kind: 'plan', text: '{"text":"Step 1"}' },
  ]);

  const items = container.querySelectorAll('.thread-item');
  assert.equal(items.length, 1);
  assert.match(items[0].dataset.itemId, /^anonymous-item-/);
});

test('accepts transcript items containing nested bigint fields', () => {
  const { container, transcriptView } = createView();

  transcriptView.upsertItems([
    {
      id: 'command-1',
      kind: 'command_execution',
      commandExecution: {
        command: 'ctest',
        exitCode: 8n,
        durationMs: 215n,
        processId: '12345',
      },
    },
  ]);

  const commandNode = container.querySelector('[data-item-id="command-1"]');
  assert.ok(commandNode);
  assert.match(commandNode.textContent, /ctest/);

  transcriptView.upsertItems([
    {
      id: 'command-1',
      kind: 'command_execution',
      commandExecution: {
        command: 'ctest --output-on-failure',
        exitCode: 8n,
        durationMs: 300n,
        processId: '12345',
      },
    },
  ]);

  assert.match(commandNode.textContent, /ctest --output-on-failure/);
});
