const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createTranscriptView } = require('../../transcript-rendering/TranscriptView');

function createView({
  clientHeight = 600,
  itemGapPx = 12,
  overscanPx = 600,
  measureElementHeight,
  scrollHeightGetter,
} = {}) {
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
  let scrollTop = 0;

  Object.defineProperty(container, 'clientHeight', {
    configurable: true,
    get() {
      return clientHeight;
    },
  });

  Object.defineProperty(container, 'scrollTop', {
    configurable: true,
    get() {
      return scrollTop;
    },
    set(value) {
      scrollTop = Number.isFinite(value) ? value : 0;
    },
  });

  Object.defineProperty(container, 'scrollHeight', {
    configurable: true,
    get() {
      if (typeof scrollHeightGetter === 'function') {
        return scrollHeightGetter();
      }

      return 0;
    },
  });

  const transcriptView = createTranscriptView({
    domWindow: dom.window,
    container,
    emptyStateElement,
    messageRenderer,
    commandExecutionRenderer,
    fileChangeRenderer,
    itemGapPx,
    overscanPx,
    measureElementHeight,
  });

  return {
    dom,
    container,
    transcriptView,
  };
}

function scrollContainer(container, top) {
  container.scrollTop = top;
  container.dispatchEvent(new container.ownerDocument.defaultView.Event('scroll'));
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

test('virtualizes long transcripts to a bounded mounted window', () => {
  const heightsById = new Map();
  for (let index = 0; index < 100; index += 1) {
    heightsById.set(`item-${index}`, 100);
  }

  const { container, transcriptView } = createView({
    clientHeight: 300,
    overscanPx: 0,
    measureElementHeight(element, fallbackHeight) {
      return heightsById.get(element.dataset.itemId) || fallbackHeight;
    },
  });

  transcriptView.upsertItems(
    Array.from({ length: 100 }, (_value, index) => ({
      id: `item-${index}`,
      kind: 'user',
      text: `Message ${index}`,
    })),
  );

  const mountedBeforeScroll = Array.from(container.querySelectorAll('.thread-item'))
    .map((element) => element.dataset.itemId);

  assert.ok(mountedBeforeScroll.length > 0);
  assert.ok(mountedBeforeScroll.length < 20);
  assert.deepEqual(mountedBeforeScroll, ['item-0', 'item-1', 'item-2']);

  scrollContainer(container, 560);

  const mountedAfterScroll = Array.from(container.querySelectorAll('.thread-item'))
    .map((element) => element.dataset.itemId);

  assert.ok(mountedAfterScroll.length > 0);
  assert.ok(mountedAfterScroll.length < 20);
  assert.notDeepEqual(mountedAfterScroll, mountedBeforeScroll);
  assert.ok(!mountedAfterScroll.includes('item-0'));
  assert.deepEqual(
    mountedAfterScroll,
    mountedAfterScroll
      .map((itemId) => Number.parseInt(itemId.replace('item-', ''), 10))
      .sort((left, right) => left - right)
      .map((index) => `item-${index}`),
  );
});

test('reuses the same DOM node when an item scrolls out of view and back', () => {
  const heightsById = new Map();
  for (let index = 0; index < 40; index += 1) {
    heightsById.set(`item-${index}`, 100);
  }

  const { container, transcriptView } = createView({
    clientHeight: 300,
    overscanPx: 0,
    measureElementHeight(element, fallbackHeight) {
      return heightsById.get(element.dataset.itemId) || fallbackHeight;
    },
  });

  transcriptView.upsertItems(
    Array.from({ length: 40 }, (_value, index) => ({
      id: `item-${index}`,
      kind: 'agent',
      text: `Reply ${index}`,
    })),
  );

  const itemNodeBefore = container.querySelector('[data-item-id="item-0"]');
  assert.ok(itemNodeBefore);

  scrollContainer(container, 560);
  assert.equal(container.querySelector('[data-item-id="item-0"]'), null);

  scrollContainer(container, 0);
  const itemNodeAfter = container.querySelector('[data-item-id="item-0"]');

  assert.ok(itemNodeAfter);
  assert.strictEqual(itemNodeAfter, itemNodeBefore);
});

test('sticks to the transcript end after full-history resume settles measured heights', () => {
  let scrollHeightValue = 0;
  const { container, transcriptView } = createView({
    clientHeight: 300,
    overscanPx: 0,
    scrollHeightGetter() {
      return scrollHeightValue;
    },
    measureElementHeight(element, fallbackHeight) {
      const itemIndex = Number.parseInt(element.dataset.itemId.replace('item-', ''), 10);
      if (itemIndex <= 2) {
        scrollHeightValue = 2500;
        return 100;
      }

      if (itemIndex >= 17) {
        scrollHeightValue = 4000;
        return 200;
      }

      return fallbackHeight;
    },
  });

  transcriptView.upsertItems(
    Array.from({ length: 20 }, (_value, index) => ({
      id: `item-${index}`,
      kind: 'agent',
      text: `Reply ${index}`,
    })),
  );

  assert.equal(container.scrollTop, 4000);
  assert.ok(container.querySelector('[data-item-id="item-19"]'));
});
