const fs = require('fs');
const path = require('path');
const { ipcRenderer } = require('electron');

const { createCommandExecutionRenderer } = require('./command-rendering/CommandExecutionRenderer');
const { createFileChangeRenderer } = require('./diff-rendering/FileChangeRenderer');
const { createMessageRenderer } = require('./message-rendering/MessageRenderer');

function resolveNativeModulePath() {
  const candidates = [
    path.join(__dirname, '..', 'native', 'qodex_thread_ui.node'),
    path.join(__dirname, '..', '..', 'build', 'runtime', 'thread-ui', 'native', 'qodex_thread_ui.node'),
  ];

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }

  throw new Error(`Unable to locate qodex_thread_ui.node. Tried: ${candidates.join(', ')}`);
}

const native = require(resolveNativeModulePath());

let animationFrameId = null;
let running = false;
let fatalErrorReported = false;
let threadItemsContainer = null;
let emptyStateElement = null;
let composerForm = null;
let composerInput = null;
let pendingErrorDialogOpen = false;
let messageRenderer = null;
let commandExecutionRenderer = null;
let fileChangeRenderer = null;
let composerResizeFrameId = null;

function describeError(error) {
  if (error instanceof Error) {
    return error.stack || error.message || String(error);
  }

  return typeof error === 'string' ? error : JSON.stringify(error);
}

async function reportFatalError(error) {
  if (fatalErrorReported) {
    return;
  }

  fatalErrorReported = true;
  running = false;

  if (animationFrameId !== null) {
    window.cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }

  await ipcRenderer.invoke('thread-ui:fatal-error', describeError(error));
}

function applyInstanceInfo(instanceInfo) {
  if (!instanceInfo || typeof instanceInfo.title !== 'string' || instanceInfo.title.trim() === '') {
    return;
  }

  document.title = instanceInfo.title;
}

function normalizeLaunchConfig(launchConfig) {
  return {
    host: typeof launchConfig?.host === 'string' ? launchConfig.host : '',
    port: Number.isInteger(launchConfig?.port) && launchConfig.port >= 0 && launchConfig.port <= 65535
      ? launchConfig.port
      : 0,
    token: typeof launchConfig?.token === 'string' ? launchConfig.token : '',
  };
}

function formatItemKindLabel(kind) {
  const normalizedKind = typeof kind === 'string' && kind.trim() !== '' ? kind : 'item';
  return normalizedKind
    .split('_')
    .map((part) => part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`)
    .join(' ');
}

function appendThreadItems(items) {
  if (!threadItemsContainer || !Array.isArray(items) || items.length === 0) {
    return;
  }

  if (emptyStateElement) {
    emptyStateElement.remove();
    emptyStateElement = null;
  }

  for (const item of items) {
    const element = document.createElement('article');
    const kind = typeof item?.kind === 'string' && item.kind.trim() !== '' ? item.kind : 'item';
    const isMarkupKind = kind === 'user' || kind === 'agent' || kind === 'reasoning';
    const isCommandExecutionKind = kind === 'command_execution';
    const isFileChangeKind = kind === 'file_change';
    element.className = isMarkupKind ? `thread-item thread-item--${kind}` : 'thread-item';
    if (isCommandExecutionKind) {
      element.classList.add('thread-item--command-execution');
    }
    if (isFileChangeKind) {
      element.classList.add('thread-item--file-change');
    }

    if (isCommandExecutionKind && commandExecutionRenderer) {
      element.append(commandExecutionRenderer.renderToElement(item));
    } else if (isFileChangeKind && fileChangeRenderer) {
      element.append(fileChangeRenderer.renderToElement(item));
    } else {
      const body = document.createElement('div');
      body.className = 'thread-item__body';
      if (isMarkupKind && messageRenderer) {
        body.innerHTML = messageRenderer.renderToHtmlFragment(typeof item?.text === 'string' ? item.text : '');
      } else {
        body.classList.add('thread-item__body--plain');
        body.textContent = typeof item?.text === 'string' ? item.text : '';
      }

      if (!isMarkupKind) {
        const label = document.createElement('div');
        label.className = 'thread-item__label';
        label.textContent = formatItemKindLabel(kind);
        element.append(label);
      }

      element.append(body);
    }

    threadItemsContainer.appendChild(element);
  }

  threadItemsContainer.scrollTop = threadItemsContainer.scrollHeight;
}

function resizeComposerInput() {
  if (!composerInput) {
    return;
  }

  composerInput.style.height = '0px';
  composerInput.style.height = `${composerInput.scrollHeight}px`;
}

function scheduleComposerResize() {
  if (composerResizeFrameId !== null) {
    window.cancelAnimationFrame(composerResizeFrameId);
  }

  composerResizeFrameId = window.requestAnimationFrame(() => {
    composerResizeFrameId = null;
    resizeComposerInput();
  });
}

async function showPendingError(message) {
  if (fatalErrorReported || pendingErrorDialogOpen || typeof message !== 'string' || message.length === 0) {
    return;
  }

  pendingErrorDialogOpen = true;
  try {
    await ipcRenderer.invoke('thread-ui:show-error', message);
  } catch (error) {
    await reportFatalError(error);
    return;
  } finally {
    pendingErrorDialogOpen = false;
  }

  if (composerInput) {
    composerInput.focus();
  }
}

function submitComposerInput() {
  if (!composerInput) {
    return;
  }

  const text = composerInput.value;
  if (text.trim().length === 0) {
    composerInput.focus();
    resizeComposerInput();
    return;
  }

  native.sendUserInput(text);
  composerInput.value = '';
  resizeComposerInput();
  composerInput.focus();
}

function initializeComposer() {
  composerForm = document.getElementById('composer-form');
  composerInput = document.getElementById('composer-input');
  if (!composerForm || !composerInput) {
    throw new Error('Thread UI composer controls were not found.');
  }

  composerForm.addEventListener('submit', (event) => {
    event.preventDefault();
    submitComposerInput();
  });

  composerInput.addEventListener('input', () => {
    resizeComposerInput();
  });

  composerInput.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter' || event.shiftKey) {
      return;
    }

    event.preventDefault();
    submitComposerInput();
  });

  resizeComposerInput();
  scheduleComposerResize();
}

async function initialize() {
  if (running) {
    return;
  }

  try {
    applyInstanceInfo(await ipcRenderer.invoke('thread-ui:get-instance-info'));
    ipcRenderer.on('thread-ui:instance-info', (_event, instanceInfo) => {
      applyInstanceInfo(instanceInfo);
    });

    running = true;
    threadItemsContainer = document.getElementById('thread-items');
    emptyStateElement = document.getElementById('thread-empty-state');
    initializeComposer();
    messageRenderer = createMessageRenderer({ domWindow: window });
    commandExecutionRenderer = createCommandExecutionRenderer({ domWindow: window });
    fileChangeRenderer = createFileChangeRenderer({ domWindow: window });
    native.initialize(normalizeLaunchConfig(await ipcRenderer.invoke('thread-ui:get-launch-config')));
    await ipcRenderer.invoke('thread-ui:notify-ready');

    function frame() {
      if (!running) {
        return;
      }

      try {
        native.tick();
        const fatalError = native.takeFatalError();
        if (typeof fatalError === 'string' && fatalError.length > 0) {
          void reportFatalError(fatalError);
          return;
        }

        appendThreadItems(native.takePendingItems());
        if (!pendingErrorDialogOpen) {
          const pendingError = native.takePendingError();
          if (typeof pendingError === 'string' && pendingError.length > 0) {
            void showPendingError(pendingError);
          }
        }
        animationFrameId = window.requestAnimationFrame(frame);
      } catch (error) {
        void reportFatalError(error);
      }
    }

    animationFrameId = window.requestAnimationFrame(frame);
  } catch (error) {
    await reportFatalError(error);
  }
}

function shutdown() {
  if (!running) {
    return;
  }

  running = false;
  pendingErrorDialogOpen = false;

  if (animationFrameId !== null) {
    window.cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
  }

  if (composerResizeFrameId !== null) {
    window.cancelAnimationFrame(composerResizeFrameId);
    composerResizeFrameId = null;
  }

  native.shutdown();
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', initialize, { once: true });
} else {
  void initialize();
}

window.addEventListener('pagehide', shutdown);
window.addEventListener('beforeunload', shutdown);
window.addEventListener('resize', scheduleComposerResize);
window.addEventListener('error', (event) => {
  void reportFatalError(event.error || event.message || 'Unknown renderer error');
});
window.addEventListener('unhandledrejection', (event) => {
  void reportFatalError(event.reason || 'Unhandled renderer promise rejection');
});
