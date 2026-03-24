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

function setFrameCountDisplay(frameCount) {
  document.getElementById('frame-count-display')
    .textContent = `The current frame is ${frameCount}`;
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

async function initialize() {
  if (running) {
    return;
  }

  await initializeWindowChrome();
  applyInstanceInfo(await ipcRenderer.invoke('thread-ui:get-instance-info'));
  ipcRenderer.on('thread-ui:instance-info', (_event, instanceInfo) => {
    applyInstanceInfo(instanceInfo);
  });

  running = true;
  native.setFrameCountDisplayCallback(setFrameCountDisplay);
  native.initialize();

  const result = native.add(2, 3);
  document.getElementById('native-result').textContent = `2 + 3 = ${result}`;
  await ipcRenderer.invoke('thread-ui:notify-ready');

  function frame() {
    if (!running) {
      return;
    }

    native.tick();
    animationFrameId = window.requestAnimationFrame(frame);
  }

  animationFrameId = window.requestAnimationFrame(frame);
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
  initialize();
}

window.addEventListener('pagehide', shutdown);
window.addEventListener('beforeunload', shutdown);
