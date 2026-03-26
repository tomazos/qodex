function formatItemKindLabel(kind) {
  const normalizedKind = typeof kind === 'string' && kind.trim() !== '' ? kind : 'item';
  return normalizedKind
    .split('_')
    .map((part) => part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`)
    .join(' ');
}

function createTranscriptView({
  domWindow,
  container,
  emptyStateElement,
  messageRenderer,
  commandExecutionRenderer,
  fileChangeRenderer,
}) {
  if (!domWindow || !container) {
    throw new Error('createTranscriptView requires a DOM window and transcript container.');
  }

  const { document } = domWindow;
  const itemRecordsById = new Map();
  let currentEmptyStateElement = emptyStateElement || null;
  let anonymousItemSequence = 1;

  function normalizeKind(item) {
    return typeof item?.kind === 'string' && item.kind.trim() !== '' ? item.kind : 'item';
  }

  function normalizeItemId(item) {
    if (typeof item?.id === 'string' && item.id.trim() !== '') {
      return item.id;
    }

    const generatedId = `anonymous-item-${anonymousItemSequence++}`;
    return generatedId;
  }

  function computeSignature(item) {
    return JSON.stringify(item ?? null);
  }

  function isMarkupKind(kind) {
    return kind === 'user' || kind === 'agent' || kind === 'reasoning';
  }

  function isScrolledNearBottom() {
    const remaining = container.scrollHeight - container.scrollTop - container.clientHeight;
    return remaining <= 4;
  }

  function scrollToBottom() {
    container.scrollTop = container.scrollHeight;
  }

  function renderIntoArticle(article, item) {
    const kind = normalizeKind(item);
    const markupKind = isMarkupKind(kind);
    const commandExecutionKind = kind === 'command_execution';
    const fileChangeKind = kind === 'file_change';

    article.className = 'thread-item';
    article.classList.toggle('thread-item--user', kind === 'user');
    article.classList.toggle('thread-item--agent', kind === 'agent');
    article.classList.toggle('thread-item--reasoning', kind === 'reasoning');
    article.classList.toggle('thread-item--command-execution', commandExecutionKind);
    article.classList.toggle('thread-item--file-change', fileChangeKind);

    article.replaceChildren();

    if (commandExecutionKind && commandExecutionRenderer) {
      article.append(commandExecutionRenderer.renderToElement(item));
      return;
    }

    if (fileChangeKind && fileChangeRenderer) {
      article.append(fileChangeRenderer.renderToElement(item));
      return;
    }

    const body = document.createElement('div');
    body.className = 'thread-item__body';
    if (markupKind && messageRenderer) {
      body.innerHTML = messageRenderer.renderToHtmlFragment(typeof item?.text === 'string' ? item.text : '');
    } else {
      body.classList.add('thread-item__body--plain');
      body.textContent = typeof item?.text === 'string' ? item.text : '';
    }

    if (!markupKind) {
      const label = document.createElement('div');
      label.className = 'thread-item__label';
      label.textContent = formatItemKindLabel(kind);
      article.append(label);
    }

    article.append(body);
  }

  function ensureEmptyStateRemoved() {
    if (currentEmptyStateElement) {
      currentEmptyStateElement.remove();
      currentEmptyStateElement = null;
    }
  }

  function upsertItems(items) {
    if (!Array.isArray(items) || items.length === 0) {
      return;
    }

    const shouldStickToBottom = isScrolledNearBottom();
    ensureEmptyStateRemoved();

    for (const item of items) {
      const itemId = normalizeItemId(item);
      const signature = computeSignature({ ...item, id: itemId });
      let record = itemRecordsById.get(itemId);

      if (!record) {
        const article = document.createElement('article');
        article.dataset.itemId = itemId;
        renderIntoArticle(article, { ...item, id: itemId });
        container.appendChild(article);
        itemRecordsById.set(itemId, {
          element: article,
          signature,
        });
        continue;
      }

      if (record.signature === signature) {
        continue;
      }

      renderIntoArticle(record.element, { ...item, id: itemId });
      record.signature = signature;
    }

    if (shouldStickToBottom) {
      scrollToBottom();
    }
  }

  return {
    upsertItems,
  };
}

module.exports = {
  createTranscriptView,
};
