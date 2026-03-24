const fs = require('fs');
const path = require('path');
const { ipcRenderer } = require('electron');

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

function updateWindowState(state) {
  const maximizeButton = document.getElementById('window-maximize');

  if (!maximizeButton) {
    return;
  }

  const isMaximized = state?.isMaximized === true;
  maximizeButton.textContent = isMaximized ? 'Restore' : 'Max';
  maximizeButton.setAttribute(
    'aria-label',
    isMaximized ? 'Restore window' : 'Maximize window',
  );
}

async function initializeWindowChrome() {
  document.getElementById('window-minimize')
    .addEventListener('click', async () => {
      await ipcRenderer.invoke('window:minimize');
    });

  document.getElementById('window-maximize')
    .addEventListener('click', async () => {
      const state = await ipcRenderer.invoke('window:toggle-maximize');
      updateWindowState(state);
    });

  document.getElementById('window-close')
    .addEventListener('click', async () => {
      await ipcRenderer.invoke('window:close');
    });

  document.getElementById('titlebar-drag-region')
    .addEventListener('dblclick', async () => {
      const state = await ipcRenderer.invoke('window:toggle-maximize');
      updateWindowState(state);
    });

  ipcRenderer.on('window:state-changed', (_event, state) => {
    updateWindowState(state);
  });

  updateWindowState(await ipcRenderer.invoke('window:get-state'));
}

function applyInstanceInfo(instanceInfo) {
  if (!instanceInfo || typeof instanceInfo.title !== 'string' || instanceInfo.title.trim() === '') {
    return;
  }

  document.title = instanceInfo.title;
  const titleElement = document.querySelector('.titlebar__title');
  if (titleElement) {
    titleElement.textContent = instanceInfo.title;
  }
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
    const kind = item?.kind === 'user' ? 'user' : 'agent';
    element.className = `thread-item thread-item--${kind}`;

    const label = document.createElement('div');
    label.className = 'thread-item__label';
    label.textContent = kind === 'user' ? 'User' : 'Agent';

    const body = document.createElement('pre');
    body.className = 'thread-item__body';
    body.textContent = typeof item?.text === 'string' ? item.text : '';

    element.append(label, body);
    threadItemsContainer.appendChild(element);
  }

  threadItemsContainer.scrollTop = threadItemsContainer.scrollHeight;
}

async function initialize() {
  if (running) {
    return;
  }

  try {
    await initializeWindowChrome();
    applyInstanceInfo(await ipcRenderer.invoke('thread-ui:get-instance-info'));
    ipcRenderer.on('thread-ui:instance-info', (_event, instanceInfo) => {
      applyInstanceInfo(instanceInfo);
    });

    running = true;
    threadItemsContainer = document.getElementById('thread-items');
    emptyStateElement = document.getElementById('thread-empty-state');
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

  if (animationFrameId !== null) {
    window.cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
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
window.addEventListener('error', (event) => {
  void reportFatalError(event.error || event.message || 'Unknown renderer error');
});
window.addEventListener('unhandledrejection', (event) => {
  void reportFatalError(event.reason || 'Unhandled renderer promise rejection');
});
