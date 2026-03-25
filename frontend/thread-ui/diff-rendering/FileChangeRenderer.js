function splitLines(text) {
  const normalized = typeof text === 'string' ? text : '';
  const lines = normalized.split('\n');
  if (lines.length > 0 && lines.at(-1) === '') {
    lines.pop();
  }
  return lines;
}

function formatLabel(text) {
  const normalized = typeof text === 'string' && text.trim() !== '' ? text.trim() : 'unknown';
  return normalized
    .split('_')
    .map((part) => part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`)
    .join(' ');
}

function normalizeChangeKind(kind) {
  switch (kind) {
    case 'add':
    case 'delete':
    case 'update':
      return kind;
    default:
      return 'unknown';
  }
}

const DIFF_META_PREFIXES = [
  'diff --git ',
  'index ',
  'new file mode ',
  'deleted file mode ',
  'old mode ',
  'new mode ',
  'similarity index ',
  'rename from ',
  'rename to ',
  'Moved to: ',
  'Moved from: ',
];

function parseUnifiedDiff(diffText) {
  const lines = splitLines(diffText);
  const rows = [];
  let sawUnifiedDiffSyntax = false;

  for (const line of lines) {
    if (DIFF_META_PREFIXES.some((prefix) => line.startsWith(prefix))) {
      rows.push({ type: 'meta', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line.startsWith('--- ') || line.startsWith('+++ ')) {
      rows.push({ type: 'file-header', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line.startsWith('@@')) {
      rows.push({ type: 'hunk', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line === '\\ No newline at end of file') {
      rows.push({ type: 'note', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line === '') {
      rows.push({ type: 'context', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line.startsWith('+')) {
      rows.push({ type: 'added', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line.startsWith('-')) {
      rows.push({ type: 'removed', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    if (line.startsWith(' ')) {
      rows.push({ type: 'context', text: line });
      sawUnifiedDiffSyntax = true;
      continue;
    }

    return null;
  }

  return sawUnifiedDiffSyntax ? rows : null;
}

function createLineElement(document, type, text) {
  const line = document.createElement('div');
  line.className = `file-change__line file-change__line--${type}`;
  line.textContent = text.length > 0 ? text : ' ';
  return line;
}

function renderRawDiff(document, diffText, lineType) {
  const diffElement = document.createElement('div');
  diffElement.className = 'file-change__diff';

  const lines = splitLines(diffText);
  if (lines.length === 0) {
    diffElement.append(createLineElement(document, lineType, ''));
    return diffElement;
  }

  for (const line of lines) {
    diffElement.append(createLineElement(document, lineType, line));
  }

  return diffElement;
}

function renderParsedUnifiedDiff(document, rows) {
  const diffElement = document.createElement('div');
  diffElement.className = 'file-change__diff';

  for (const row of rows) {
    diffElement.append(createLineElement(document, row.type, row.text));
  }

  return diffElement;
}

function renderChange(document, change) {
  const kind = normalizeChangeKind(change?.kind);
  const section = document.createElement('section');
  section.className = `file-change__change file-change__change--${kind}`;

  const header = document.createElement('header');
  header.className = 'file-change__header';

  const badge = document.createElement('span');
  badge.className = `file-change__badge file-change__badge--${kind}`;
  badge.textContent = formatLabel(kind);

  const path = document.createElement('div');
  path.className = 'file-change__path';
  const sourcePath = typeof change?.path === 'string' ? change.path : '';
  const movePath = typeof change?.movePath === 'string' ? change.movePath : '';
  path.textContent = kind === 'update' && movePath !== ''
    ? `${sourcePath} -> ${movePath}`
    : sourcePath;

  header.append(badge, path);
  section.append(header);

  const diffText = typeof change?.diff === 'string' ? change.diff : '';
  if (kind === 'update') {
    const parsedRows = parseUnifiedDiff(diffText);
    if (parsedRows) {
      section.append(renderParsedUnifiedDiff(document, parsedRows));
      return section;
    }

    const raw = document.createElement('pre');
    raw.className = 'file-change__raw';
    raw.textContent = diffText;
    section.append(raw);
    return section;
  }

  if (kind === 'add') {
    section.append(renderRawDiff(document, diffText, 'added'));
    return section;
  }

  if (kind === 'delete') {
    section.append(renderRawDiff(document, diffText, 'removed'));
    return section;
  }

  const raw = document.createElement('pre');
  raw.className = 'file-change__raw';
  raw.textContent = diffText;
  section.append(raw);
  return section;
}

function createFileChangeRenderer({ domWindow }) {
  if (!domWindow || !domWindow.document) {
    throw new Error('createFileChangeRenderer requires a DOM window');
  }

  return {
    renderToElement(fileChange) {
      const { document } = domWindow;
      const container = document.createElement('div');
      container.className = 'thread-item__file-change';

      const status = typeof fileChange?.status === 'string' ? fileChange.status.trim() : '';
      if (status !== '' && status !== 'completed') {
        const statusElement = document.createElement('div');
        statusElement.className = 'file-change__status';
        statusElement.textContent = `Patch status: ${formatLabel(status)}`;
        container.append(statusElement);
      }

      const changes = Array.isArray(fileChange?.changes) ? fileChange.changes : [];
      if (changes.length === 0) {
        const emptyElement = document.createElement('div');
        emptyElement.className = 'file-change__status';
        emptyElement.textContent = 'No file changes';
        container.append(emptyElement);
        return container;
      }

      for (const change of changes) {
        container.append(renderChange(document, change));
      }

      return container;
    },
  };
}

module.exports = {
  createFileChangeRenderer,
  parseUnifiedDiff,
};
