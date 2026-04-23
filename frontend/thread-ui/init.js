const fs = require('fs');
const path = require('path');
const { ipcRenderer } = require('electron');

const { createCommandExecutionRenderer } = require('./command-rendering/CommandExecutionRenderer');
const { createImageGenerationRenderer } = require('./image-rendering/ImageGenerationRenderer');
const { createLinkInteractionController } = require('./link-handling/LinkInteractionController');
const { createFileChangeRenderer } = require('./diff-rendering/FileChangeRenderer');
const { createMessageRenderer } = require('./message-rendering/MessageRenderer');
const { createTranscriptView } = require('./transcript-rendering/TranscriptView');

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
let threadStatusElement = null;
let threadSettingsElement = null;
let pendingErrorDialogOpen = false;
let messageRenderer = null;
let commandExecutionRenderer = null;
let fileChangeRenderer = null;
let imageGenerationRenderer = null;
let transcriptView = null;
let linkInteractionController = null;
let composerResizeFrameId = null;
let lastSubmittedComposerText = null;
let lastSubmittedComposerAtMs = 0;
let currentThreadStatus = { kind: 'idle', text: 'Idle', model: '', reasoningEffort: '' };
const pendingLinkResolutionRequests = new Map();

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

function reasoningEffortLabel(reasoningEffort) {
  switch (reasoningEffort) {
    case 'none':
      return 'None';
    case 'minimal':
      return 'Minimal';
    case 'low':
      return 'Low';
    case 'medium':
      return 'Medium';
    case 'high':
      return 'High';
    case 'xhigh':
      return 'XHigh';
    default:
      return '';
  }
}

function renderThreadSettings() {
  if (!threadSettingsElement) {
    return;
  }

  const model = typeof currentThreadStatus?.model === 'string' ? currentThreadStatus.model.trim() : '';
  const reasoningLabel = reasoningEffortLabel(
    typeof currentThreadStatus?.reasoningEffort === 'string' ? currentThreadStatus.reasoningEffort.trim() : ''
  );

  const parts = [];
  if (model.length > 0) {
    parts.push(model);
  }
  if (reasoningLabel.length > 0) {
    parts.push(reasoningLabel);
  }

  threadSettingsElement.textContent = parts.join(' · ');
  threadSettingsElement.title = parts.length > 0
    ? `Model: ${model || 'Unknown'}\nReasoning: ${reasoningLabel || 'Unknown'}`
    : '';
}

function upsertThreadItems(items) {
  if (!transcriptView || !Array.isArray(items) || items.length === 0) {
    return;
  }
  const hadNoMountedTranscriptItems = threadItemsContainer?.querySelector('.thread-item') === null;
  transcriptView.upsertItems(items);
  if (hadNoMountedTranscriptItems) {
    transcriptView.scrollToEndSoon();
  }
}

function renderThreadStatus() {
  if (!threadStatusElement) {
    return;
  }

  const kind = typeof currentThreadStatus?.kind === 'string' ? currentThreadStatus.kind.trim() : 'idle';
  const text = typeof currentThreadStatus?.text === 'string' && currentThreadStatus.text.trim().length > 0
    ? currentThreadStatus.text.trim()
    : 'Idle';

  threadStatusElement.replaceChildren();
  threadStatusElement.dataset.kind = kind;

  if (kind === 'active' && text.startsWith('Active')) {
    const activeLabel = document.createElement('span');
    activeLabel.className = 'thread-status__active-label';
    activeLabel.textContent = 'Active';
    threadStatusElement.append(activeLabel);

    const suffix = text.slice('Active'.length);
    if (suffix.length > 0) {
      threadStatusElement.append(document.createTextNode(suffix));
    }
    renderThreadSettings();
    return;
  }

  threadStatusElement.textContent = text;
  renderThreadSettings();
}

function applyThreadStatus(statusUpdate) {
  if (!threadStatusElement) {
    return;
  }

  if (!statusUpdate || typeof statusUpdate !== 'object') {
    return;
  }

  const text = typeof statusUpdate.text === 'string' ? statusUpdate.text.trim() : '';
  const kind = typeof statusUpdate.kind === 'string' ? statusUpdate.kind.trim() : '';
  const model = typeof statusUpdate.model === 'string' ? statusUpdate.model.trim() : '';
  const reasoningEffort = typeof statusUpdate.reasoningEffort === 'string'
    ? statusUpdate.reasoningEffort.trim()
    : '';
  currentThreadStatus = {
    kind: kind.length > 0 ? kind : 'idle',
    text: text.length > 0 ? text : 'Idle',
    model,
    reasoningEffort,
  };
  renderThreadStatus();
}

function animateThreadStatus() {
  if (!threadStatusElement || currentThreadStatus?.kind !== 'active') {
    return;
  }

  const activeLabel = threadStatusElement.querySelector('.thread-status__active-label');
  if (!activeLabel) {
    return;
  }

  const hue = Math.floor(Math.random() * 360);
  activeLabel.style.color = `color-mix(in srgb, hsl(${hue}deg 88% 56%) 62%, canvastext 38%)`;
}

function settlePendingLinkResolutions() {
  const resolvedLinks = native.takePendingResolvedLinks();
  if (!Array.isArray(resolvedLinks) || resolvedLinks.length === 0) {
    return;
  }

  for (const resolvedLink of resolvedLinks) {
    const requestKey = typeof resolvedLink?.requestId === 'bigint'
      ? resolvedLink.requestId.toString()
      : String(resolvedLink?.requestId ?? '');
    const pendingRequest = pendingLinkResolutionRequests.get(requestKey);
    if (!pendingRequest) {
      continue;
    }

    pendingLinkResolutionRequests.delete(requestKey);
    pendingRequest.resolve(resolvedLink);
  }
}

function resolveLinkDescriptor(rawHref) {
  if (typeof rawHref !== 'string' || rawHref.trim() === '') {
    return Promise.resolve({
      ok: false,
      rawHref: '',
      normalizedHref: '',
      tooltip: '',
      defaultAction: 'none',
    });
  }

  const requestId = native.resolveLink(rawHref.trim());
  const requestKey = typeof requestId === 'bigint' ? requestId.toString() : String(requestId ?? '');
  if (requestKey === '' || requestKey === '0') {
    return Promise.resolve({
      ok: false,
      rawHref,
      normalizedHref: rawHref,
      tooltip: rawHref,
      defaultAction: 'none',
    });
  }

  return new Promise((resolve) => {
    pendingLinkResolutionRequests.set(requestKey, { resolve });
  });
}

async function performLinkAction(payload) {
  await ipcRenderer.invoke('thread-ui:perform-link-action', payload);
}

async function showLinkContextMenu(payload) {
  await ipcRenderer.invoke('thread-ui:show-link-context-menu', payload);
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

  const submittedRecently = (performance.now() - lastSubmittedComposerAtMs) <= 5000;
  if (
    composerInput &&
    submittedRecently &&
    typeof lastSubmittedComposerText === 'string' &&
    composerInput.value.length === 0
  ) {
    composerInput.value = lastSubmittedComposerText;
    resizeComposerInput();
  }
  lastSubmittedComposerText = null;
  lastSubmittedComposerAtMs = 0;

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

  lastSubmittedComposerText = text;
  lastSubmittedComposerAtMs = performance.now();
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
    threadStatusElement = document.getElementById('thread-status');
    threadSettingsElement = document.getElementById('thread-settings');
    renderThreadStatus();
    initializeComposer();
    messageRenderer = createMessageRenderer({ domWindow: window });
    commandExecutionRenderer = createCommandExecutionRenderer({ domWindow: window });
    fileChangeRenderer = createFileChangeRenderer({ domWindow: window });
    imageGenerationRenderer = createImageGenerationRenderer({ domWindow: window });
    transcriptView = createTranscriptView({
      domWindow: window,
      container: threadItemsContainer,
      emptyStateElement,
      messageRenderer,
      commandExecutionRenderer,
      fileChangeRenderer,
      imageGenerationRenderer,
    });
    linkInteractionController = createLinkInteractionController({
      container: threadItemsContainer,
      resolveLink: resolveLinkDescriptor,
      performLinkAction,
      showLinkContextMenu,
    });
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

        upsertThreadItems(native.takePendingItems());
        applyThreadStatus(native.takePendingThreadStatus());
        animateThreadStatus();
        settlePendingLinkResolutions();
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

  if (linkInteractionController) {
    linkInteractionController.dispose();
    linkInteractionController = null;
  }

  pendingLinkResolutionRequests.clear();

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
