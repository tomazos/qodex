const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createLinkInteractionController } = require('../../link-handling/LinkInteractionController');

async function flushAsyncWork() {
  await new Promise((resolve) => setTimeout(resolve, 0));
  await Promise.resolve();
}

function createHarness() {
  const dom = new JSDOM(`
    <!doctype html>
    <html>
      <body>
        <div id="container">
          <a id="link" href="docs/readme.md#L12">Readme</a>
        </div>
      </body>
    </html>
  `);

  const resolvedLinks = [];
  const performedActions = [];
  const contextMenuPayloads = [];

  const controller = createLinkInteractionController({
    container: dom.window.document.getElementById('container'),
    async resolveLink(rawHref) {
      resolvedLinks.push(rawHref);
      return {
        ok: true,
        rawHref,
        normalizedHref: 'file:///home/zos/qodex/docs/readme.md#L12',
        tooltip: 'file:///home/zos/qodex/docs/readme.md#L12\nResolved: /home/zos/qodex/docs/readme.md:12\nExists: yes',
        kind: 'file',
        resolvedPath: '/home/zos/qodex/docs/readme.md',
        exists: true,
        isDirectory: false,
        hasLine: true,
        line: 12n,
        hasColumn: false,
        column: 0n,
        defaultAction: 'open',
        canOpen: true,
        canOpenExternally: false,
        canRevealInFolder: true,
        canCopyResolvedPath: true,
      };
    },
    async performLinkAction(payload) {
      performedActions.push(payload);
    },
    async showLinkContextMenu(payload) {
      contextMenuPayloads.push(payload);
    },
  });

  return {
    dom,
    controller,
    resolvedLinks,
    performedActions,
    contextMenuPayloads,
    link: dom.window.document.getElementById('link'),
  };
}

test('left-click resolves a link once and performs the default action', async () => {
  const harness = createHarness();

  const clickEvent = new harness.dom.window.MouseEvent('click', {
    bubbles: true,
    cancelable: true,
    button: 0,
  });
  harness.link.dispatchEvent(clickEvent);
  await flushAsyncWork();

  assert.equal(clickEvent.defaultPrevented, true);
  assert.deepEqual(harness.resolvedLinks, ['docs/readme.md#L12']);
  assert.equal(harness.performedActions.length, 1);
  assert.equal(harness.performedActions[0].actionKind, 'open');
  assert.equal(harness.performedActions[0].resolvedLink.resolvedPath, '/home/zos/qodex/docs/readme.md');

  harness.link.dispatchEvent(new harness.dom.window.MouseEvent('click', {
    bubbles: true,
    cancelable: true,
    button: 0,
  }));
  await flushAsyncWork();

  assert.deepEqual(harness.resolvedLinks, ['docs/readme.md#L12']);
  assert.equal(harness.performedActions.length, 2);
  harness.controller.dispose();
});

test('hover resolves a tooltip and contextmenu uses the resolved descriptor', async () => {
  const harness = createHarness();

  harness.link.dispatchEvent(new harness.dom.window.MouseEvent('mouseover', {
    bubbles: true,
    cancelable: true,
  }));
  await flushAsyncWork();

  assert.match(harness.link.title, /file:\/\/\/home\/zos\/qodex\/docs\/readme\.md#L12/);

  const contextMenuEvent = new harness.dom.window.MouseEvent('contextmenu', {
    bubbles: true,
    cancelable: true,
  });
  harness.link.dispatchEvent(contextMenuEvent);
  await flushAsyncWork();

  assert.equal(contextMenuEvent.defaultPrevented, true);
  assert.equal(harness.contextMenuPayloads.length, 1);
  assert.equal(harness.contextMenuPayloads[0].linkText, 'Readme');
  assert.equal(harness.contextMenuPayloads[0].resolvedLink.canRevealInFolder, true);
  harness.controller.dispose();
});
