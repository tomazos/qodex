const os = require('node:os');

function formatLabel(text) {
  const normalized = typeof text === 'string' && text.trim() !== '' ? text.trim() : 'unknown';
  return normalized
    .split('_')
    .map((part) => part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`)
    .join(' ');
}

function bigintLikeToString(value) {
  if (typeof value === 'bigint') {
    return value.toString();
  }

  if (typeof value === 'number' && Number.isFinite(value)) {
    return Math.trunc(value).toString();
  }

  if (typeof value === 'string' && value.trim() !== '') {
    return value.trim();
  }

  return '';
}

function formatDuration(value) {
  const raw = bigintLikeToString(value);
  if (raw === '') {
    return '';
  }

  const numeric = Number(raw);
  if (!Number.isFinite(numeric)) {
    return `${raw} ms`;
  }

  if (numeric >= 1000) {
    return `${(numeric / 1000).toFixed(numeric >= 10000 ? 0 : 1)} s`;
  }

  return `${numeric} ms`;
}

function normalizeStatus(status) {
  switch (status) {
    case 'completed':
    case 'failed':
    case 'in_progress':
    case 'declined':
      return status;
    default:
      return 'unknown';
  }
}

function normalizeShellIdentity(shellIdentity) {
  const fallbackIdentity = {
    username: 'user',
    hostname: 'host',
  };

  if (!shellIdentity || typeof shellIdentity !== 'object') {
    try {
      return {
        username: typeof os.userInfo().username === 'string' && os.userInfo().username.trim() !== ''
          ? os.userInfo().username.trim()
          : fallbackIdentity.username,
        hostname: typeof os.hostname() === 'string' && os.hostname().trim() !== ''
          ? os.hostname().trim()
          : fallbackIdentity.hostname,
      };
    } catch {
      return fallbackIdentity;
    }
  }

  return {
    username: typeof shellIdentity.username === 'string' && shellIdentity.username.trim() !== ''
      ? shellIdentity.username.trim()
      : fallbackIdentity.username,
    hostname: typeof shellIdentity.hostname === 'string' && shellIdentity.hostname.trim() !== ''
      ? shellIdentity.hostname.trim()
      : fallbackIdentity.hostname,
  };
}

function normalizePromptCwd(cwd, username) {
  if (typeof cwd !== 'string' || cwd.trim() === '') {
    return '?';
  }

  const trimmedCwd = cwd.trim();
  const homePrefix = `/home/${username}`;
  if (trimmedCwd === homePrefix) {
    return '~';
  }

  if (trimmedCwd.startsWith(`${homePrefix}/`)) {
    return `~${trimmedCwd.slice(homePrefix.length)}`;
  }

  return trimmedCwd;
}

function unwrapShellCommand(command) {
  if (typeof command !== 'string') {
    return '';
  }

  const prefix = '/bin/bash -lc ';
  if (!command.startsWith(prefix)) {
    return command;
  }

  const payload = command.slice(prefix.length);
  if (payload.length < 2) {
    return command;
  }

  if (payload.startsWith('"') && payload.endsWith('"')) {
    try {
      return JSON.parse(payload);
    } catch {
      return payload.slice(1, -1);
    }
  }

  if (payload.startsWith('\'') && payload.endsWith('\'')) {
    return payload
      .slice(1, -1)
      .replace(/'\\''/g, '\'');
  }

  return command;
}

function normalizeActions(commandExecution) {
  if (!Array.isArray(commandExecution?.actions)) {
    return [];
  }

  return commandExecution.actions
    .filter((action) => action && typeof action === 'object')
    .map((action) => ({
      kind: typeof action.kind === 'string' ? action.kind.trim() : '',
      path: typeof action.path === 'string' ? action.path.trim() : '',
      query: typeof action.query === 'string' ? action.query.trim() : '',
      name: typeof action.name === 'string' ? action.name.trim() : '',
      command: typeof action.command === 'string' ? action.command.trim() : '',
    }))
    .filter((action) => action.kind.length > 0);
}

function appendTitleLink(document, title, href, text) {
  if (typeof href !== 'string' || href.trim() === '' || typeof text !== 'string' || text.trim() === '') {
    return false;
  }

  const link = document.createElement('a');
  link.className = 'command-execution__title-link';
  link.href = href.trim();
  link.textContent = text.trim();
  title.append(link);
  return true;
}

function appendTitleStrong(document, title, text) {
  const strong = document.createElement('strong');
  strong.textContent = text;
  title.append(strong);
}

function renderTitle(document, title, commandExecution) {
  const actions = normalizeActions(commandExecution);
  const firstAction = actions.length > 0 ? actions[0] : null;
  if (!firstAction) {
    title.textContent = 'Run command';
    return;
  }

  switch (firstAction.kind) {
    case 'read':
      appendTitleStrong(document, title, 'Read');
      if (appendTitleLink(document, title, firstAction.path, firstAction.path || firstAction.name)) {
        title.insertBefore(document.createTextNode(' '), title.lastChild);
      }
      return;
    case 'listFiles':
      if (firstAction.path !== '') {
        appendTitleStrong(document, title, 'List');
        title.append(document.createTextNode(' '));
        appendTitleLink(document, title, firstAction.path, firstAction.path);
        return;
      }
      title.textContent = 'List Files';
      return;
    case 'search':
      if (firstAction.query !== '' && firstAction.path !== '') {
        appendTitleStrong(document, title, 'Search');
        title.append(document.createTextNode(` ${firstAction.query} in `));
        appendTitleLink(document, title, firstAction.path, firstAction.path);
        return;
      }
      if (firstAction.query !== '') {
        appendTitleStrong(document, title, 'Search');
        title.append(document.createTextNode(` ${firstAction.query}`));
        return;
      }
      title.textContent = 'Search Files';
      return;
    default:
      title.textContent = 'Run command';
  }
}

function createCommandExecutionRenderer({ domWindow, shellIdentity }) {
  if (!domWindow || !domWindow.document) {
    throw new Error('createCommandExecutionRenderer requires a DOM window');
  }

  const resolvedShellIdentity = normalizeShellIdentity(shellIdentity);

  return {
    renderToElement(commandExecution) {
      const { document } = domWindow;
      const container = document.createElement('div');
      container.className = 'thread-item__command-execution';

      const normalizedStatus = normalizeStatus(commandExecution?.status);
      const title = document.createElement('div');
      title.className = 'command-execution__title';
      renderTitle(document, title, commandExecution);
      container.append(title);

      const prompt = document.createElement('div');
      prompt.className = 'command-execution__prompt';

      const identity = document.createElement('span');
      identity.className = 'command-execution__prompt-identity';
      identity.textContent = `${resolvedShellIdentity.username}@${resolvedShellIdentity.hostname}`;
      prompt.append(identity);

      prompt.append(document.createTextNode(':'));

      const cwd = document.createElement('span');
      cwd.className = 'command-execution__prompt-cwd';
      cwd.textContent = normalizePromptCwd(commandExecution?.cwd, resolvedShellIdentity.username);
      prompt.append(cwd);

      prompt.append(document.createTextNode('$ '));

      const commandElement = document.createElement('span');
      commandElement.className = 'command-execution__prompt-command';
      commandElement.textContent = unwrapShellCommand(commandExecution?.command);
      prompt.append(commandElement);

      container.append(prompt);

      const metaParts = [];
      if (commandExecution?.hasExitCode) {
        metaParts.push(`exit ${bigintLikeToString(commandExecution?.exitCode)}`);
      }
      if (commandExecution?.hasDurationMs) {
        metaParts.push(formatDuration(commandExecution?.durationMs));
      }
      if (typeof commandExecution?.processId === 'string' && commandExecution.processId !== '') {
        metaParts.push(`pid ${commandExecution.processId}`);
      }

      const output = document.createElement('pre');
      output.className = 'command-execution__output';
      output.textContent = typeof commandExecution?.aggregatedOutput === 'string' && commandExecution.aggregatedOutput !== ''
        ? commandExecution.aggregatedOutput
        : '(no output)';
      container.append(output);

      const footer = document.createElement('div');
      footer.className = 'command-execution__footer';

      const badge = document.createElement('span');
      badge.className = `command-execution__badge command-execution__badge--${normalizedStatus}`;
      badge.textContent = formatLabel(normalizedStatus);
      footer.append(badge);

      if (metaParts.length > 0) {
        const meta = document.createElement('div');
        meta.className = 'command-execution__meta';
        meta.textContent = metaParts.join(' · ');
        footer.append(meta);
      }

      container.append(footer);

      return container;
    },
  };
}

module.exports = {
  createCommandExecutionRenderer,
};
