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

function createCommandExecutionRenderer({ domWindow }) {
  if (!domWindow || !domWindow.document) {
    throw new Error('createCommandExecutionRenderer requires a DOM window');
  }

  return {
    renderToElement(commandExecution) {
      const { document } = domWindow;
      const container = document.createElement('div');
      container.className = 'thread-item__command-execution';

      const header = document.createElement('header');
      header.className = 'command-execution__header';

      const badge = document.createElement('span');
      const normalizedStatus = normalizeStatus(commandExecution?.status);
      badge.className = `command-execution__badge command-execution__badge--${normalizedStatus}`;
      badge.textContent = formatLabel(normalizedStatus);
      header.append(badge);

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

      if (metaParts.length > 0) {
        const meta = document.createElement('div');
        meta.className = 'command-execution__meta';
        meta.textContent = metaParts.join(' · ');
        header.append(meta);
      }

      container.append(header);

      const commandElement = document.createElement('div');
      commandElement.className = 'command-execution__command';
      commandElement.textContent = typeof commandExecution?.command === 'string' ? commandExecution.command : '';
      container.append(commandElement);

      if (typeof commandExecution?.cwd === 'string' && commandExecution.cwd !== '') {
        const cwdElement = document.createElement('div');
        cwdElement.className = 'command-execution__cwd';
        cwdElement.textContent = commandExecution.cwd;
        container.append(cwdElement);
      }

      if (Array.isArray(commandExecution?.actionLabels) && commandExecution.actionLabels.length > 0) {
        const actions = document.createElement('div');
        actions.className = 'command-execution__actions';
        for (const actionLabel of commandExecution.actionLabels) {
          if (typeof actionLabel !== 'string' || actionLabel.trim() === '') {
            continue;
          }

          const chip = document.createElement('span');
          chip.className = 'command-execution__action';
          chip.textContent = actionLabel;
          actions.append(chip);
        }

        if (actions.childElementCount > 0) {
          container.append(actions);
        }
      }

      const output = document.createElement('pre');
      output.className = 'command-execution__output';
      output.textContent = typeof commandExecution?.aggregatedOutput === 'string' && commandExecution.aggregatedOutput !== ''
        ? commandExecution.aggregatedOutput
        : '(no output)';
      container.append(output);

      return container;
    },
  };
}

module.exports = {
  createCommandExecutionRenderer,
};
