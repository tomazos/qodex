const DEFAULT_ESTIMATED_ITEM_HEIGHT_PX = 120;
const DEFAULT_FALLBACK_VIEWPORT_HEIGHT_PX = 600;
const DEFAULT_ITEM_GAP_PX = 12;
const DEFAULT_OVERSCAN_PX = 600;

function formatItemKindLabel(kind) {
  const normalizedKind = typeof kind === 'string' && kind.trim() !== '' ? kind : 'item';
  return normalizedKind
    .split('_')
    .map((part) => (part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`))
    .join(' ');
}

function canonicalizeSignatureValue(value) {
  if (typeof value === 'bigint') {
    return `${value}n`;
  }

  if (Array.isArray(value)) {
    return value.map(canonicalizeSignatureValue);
  }

  if (value && typeof value === 'object') {
    return Object.keys(value)
      .sort()
      .reduce((result, key) => {
        result[key] = canonicalizeSignatureValue(value[key]);
        return result;
      }, {});
  }

  return value;
}

function measureRenderedHeight(element, fallbackHeight) {
  if (element && typeof element.getBoundingClientRect === 'function') {
    const rect = element.getBoundingClientRect();
    if (rect && Number.isFinite(rect.height) && rect.height > 0) {
      return rect.height;
    }
  }

  if (element && Number.isFinite(element.offsetHeight) && element.offsetHeight > 0) {
    return element.offsetHeight;
  }

  return fallbackHeight;
}

function createTranscriptView({
  domWindow,
  container,
  emptyStateElement,
  messageRenderer,
  commandExecutionRenderer,
  fileChangeRenderer,
  itemGapPx = DEFAULT_ITEM_GAP_PX,
  overscanPx = DEFAULT_OVERSCAN_PX,
  measureElementHeight = measureRenderedHeight,
}) {
  if (!domWindow || !container) {
    throw new Error('createTranscriptView requires a DOM window and transcript container.');
  }

  const { document } = domWindow;
  const normalizedItemGapPx = Number.isFinite(itemGapPx) ? Math.max(0, itemGapPx) : DEFAULT_ITEM_GAP_PX;
  const normalizedOverscanPx = Number.isFinite(overscanPx) ? Math.max(0, overscanPx) : DEFAULT_OVERSCAN_PX;
  const itemRecordsById = new Map();
  const itemOrder = [];
  const virtualRoot = document.createElement('div');
  const topSpacer = document.createElement('div');
  const mountedItemsHost = document.createElement('div');
  const bottomSpacer = document.createElement('div');
  let currentEmptyStateElement = emptyStateElement || null;
  let anonymousItemSequence = 1;
  let renderFrameId = null;
  let lastRenderedRangeKey = '';
  let lastMeasuredViewportWidth = null;

  virtualRoot.className = 'thread-view__items-virtual';
  virtualRoot.hidden = true;
  topSpacer.className = 'thread-view__spacer';
  bottomSpacer.className = 'thread-view__spacer';
  mountedItemsHost.className = 'thread-view__mounted';
  virtualRoot.append(topSpacer, mountedItemsHost, bottomSpacer);
  container.append(virtualRoot);

  function normalizeKind(item) {
    return typeof item?.kind === 'string' && item.kind.trim() !== '' ? item.kind : 'item';
  }

  function normalizeItemId(item) {
    if (typeof item?.id === 'string' && item.id.trim() !== '') {
      return item.id;
    }

    return `anonymous-item-${anonymousItemSequence++}`;
  }

  function normalizeItem(item) {
    const itemId = normalizeItemId(item);
    return {
      ...item,
      id: itemId,
      kind: normalizeKind(item),
    };
  }

  function computeSignature(item) {
    return JSON.stringify(canonicalizeSignatureValue(item ?? null));
  }

  function isMarkupKind(kind) {
    return kind === 'user' || kind === 'agent' || kind === 'reasoning';
  }

  function countLines(text) {
    if (typeof text !== 'string' || text.length === 0) {
      return 0;
    }

    return text.split('\n').length;
  }

  function estimateItemHeight(item) {
    const kind = normalizeKind(item);

    if (kind === 'command_execution') {
      const commandTextLength = typeof item?.commandExecution?.command === 'string'
        ? item.commandExecution.command.length
        : 0;
      const outputLines = countLines(item?.commandExecution?.aggregatedOutput);
      return Math.min(520, 120 + (Math.min(outputLines, 16) * 18) + Math.ceil(commandTextLength / 72) * 10);
    }

    if (kind === 'file_change') {
      const changes = Array.isArray(item?.fileChange?.changes) ? item.fileChange.changes : [];
      let diffLines = 0;
      for (const change of changes) {
        diffLines += countLines(change?.diff);
      }

      return Math.min(560, 110 + (Math.min(diffLines, 18) * 18));
    }

    const text = typeof item?.text === 'string' ? item.text : '';
    const lineCount = countLines(text);
    const wrappedLines = Math.ceil(text.length / 96);
    return Math.max(
      DEFAULT_ESTIMATED_ITEM_HEIGHT_PX,
      Math.min(360, 48 + (Math.max(lineCount, wrappedLines) * 22)),
    );
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

  function ensureRecordElement(record) {
    if (record.element) {
      return record.element;
    }

    const article = document.createElement('article');
    article.dataset.itemId = record.item.id;
    renderIntoArticle(article, record.item);
    record.element = article;
    return article;
  }

  function sumHeights(startIndex, endIndexExclusive) {
    if (endIndexExclusive <= startIndex) {
      return 0;
    }

    let totalHeight = 0;
    for (let index = startIndex; index < endIndexExclusive; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record) {
        continue;
      }

      totalHeight += record.height;
      if (index > startIndex) {
        totalHeight += normalizedItemGapPx;
      }
    }

    return totalHeight;
  }

  function totalTranscriptHeight() {
    return sumHeights(0, itemOrder.length);
  }

  function viewportHeight() {
    if (Number.isFinite(container.clientHeight) && container.clientHeight > 0) {
      return container.clientHeight;
    }

    return DEFAULT_FALLBACK_VIEWPORT_HEIGHT_PX;
  }

  function computeVisibleRange() {
    if (itemOrder.length === 0) {
      return {
        startIndex: 0,
        endIndexExclusive: 0,
        topSpacerHeight: 0,
        bottomSpacerHeight: 0,
      };
    }

    const viewTop = Math.max(0, container.scrollTop - normalizedOverscanPx);
    const viewBottom = container.scrollTop + viewportHeight() + normalizedOverscanPx;
    let prefixHeight = 0;
    let startIndex = 0;
    let foundStart = false;
    let endIndexExclusive = itemOrder.length;

    for (let index = 0; index < itemOrder.length; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record) {
        continue;
      }

      const itemTop = prefixHeight;
      const itemBottom = itemTop + record.height;

      if (!foundStart && itemBottom >= viewTop) {
        startIndex = index;
        foundStart = true;
      }

      if (foundStart && itemTop > viewBottom) {
        endIndexExclusive = index;
        break;
      }

      prefixHeight = itemBottom + normalizedItemGapPx;
    }

    if (!foundStart) {
      startIndex = Math.max(0, itemOrder.length - 1);
      endIndexExclusive = itemOrder.length;
    }

    if (endIndexExclusive <= startIndex) {
      endIndexExclusive = Math.min(itemOrder.length, startIndex + 1);
    }

    const topSpacerHeight = sumHeights(0, startIndex) + (startIndex > 0 ? normalizedItemGapPx : 0);
    const mountedHeight = sumHeights(startIndex, endIndexExclusive);
    const bottomSpacerHeight = Math.max(0, totalTranscriptHeight() - topSpacerHeight - mountedHeight);

    return {
      startIndex,
      endIndexExclusive,
      topSpacerHeight,
      bottomSpacerHeight,
    };
  }

  function updateSpacerHeights(range) {
    topSpacer.style.height = `${range.topSpacerHeight}px`;
    bottomSpacer.style.height = `${range.bottomSpacerHeight}px`;
  }

  function measureVisibleItems(range) {
    let heightsChanged = false;

    for (let index = range.startIndex; index < range.endIndexExclusive; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record || !record.element || record.element.parentNode !== mountedItemsHost) {
        continue;
      }

      const measuredHeight = measureElementHeight(record.element, record.height);
      if (!Number.isFinite(measuredHeight) || measuredHeight <= 0) {
        continue;
      }

      if (Math.abs(record.height - measuredHeight) >= 1) {
        record.height = measuredHeight;
        heightsChanged = true;
      }
    }

    return heightsChanged;
  }

  function mountVisibleRange(range) {
    const rangeKey = `${range.startIndex}:${range.endIndexExclusive}:${range.topSpacerHeight}:${range.bottomSpacerHeight}`;
    if (rangeKey === lastRenderedRangeKey) {
      updateSpacerHeights(range);
      return range;
    }

    const fragment = document.createDocumentFragment();
    for (let index = range.startIndex; index < range.endIndexExclusive; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record) {
        continue;
      }

      fragment.append(ensureRecordElement(record));
    }

    mountedItemsHost.replaceChildren(fragment);
    updateSpacerHeights(range);
    lastRenderedRangeKey = rangeKey;
    return range;
  }

  function renderVisibleWindow() {
    if (itemOrder.length === 0) {
      mountedItemsHost.replaceChildren();
      updateSpacerHeights({
        topSpacerHeight: 0,
        bottomSpacerHeight: 0,
      });
      lastRenderedRangeKey = '';
      virtualRoot.hidden = true;
      return;
    }

    virtualRoot.hidden = false;

    const initialRange = computeVisibleRange();
    mountVisibleRange(initialRange);

    if (measureVisibleItems(initialRange)) {
      const measuredRange = computeVisibleRange();
      mountVisibleRange(measuredRange);
    }

    const measuredViewportWidth = mountedItemsHost.clientWidth;
    if (
      Number.isFinite(measuredViewportWidth) &&
      measuredViewportWidth > 0 &&
      lastMeasuredViewportWidth !== null &&
      measuredViewportWidth !== lastMeasuredViewportWidth
    ) {
      for (const itemId of itemOrder) {
        const record = itemRecordsById.get(itemId);
        if (!record) {
          continue;
        }

        record.height = estimateItemHeight(record.item);
      }

      const resizedRange = computeVisibleRange();
      mountVisibleRange(resizedRange);
      measureVisibleItems(resizedRange);
    }

    if (Number.isFinite(measuredViewportWidth) && measuredViewportWidth > 0) {
      lastMeasuredViewportWidth = measuredViewportWidth;
    }
  }

  function requestFrame(callback) {
    if (typeof domWindow.requestAnimationFrame === 'function') {
      return domWindow.requestAnimationFrame(callback);
    }

    callback();
    return null;
  }

  function cancelFrame(frameId) {
    if (frameId === null || typeof domWindow.cancelAnimationFrame !== 'function') {
      return;
    }

    domWindow.cancelAnimationFrame(frameId);
  }

  function scheduleRenderVisibleWindow() {
    if (renderFrameId !== null) {
      return;
    }

    renderFrameId = requestFrame(() => {
      renderFrameId = null;
      renderVisibleWindow();
    });
  }

  function resetEstimatedHeights() {
    for (const itemId of itemOrder) {
      const record = itemRecordsById.get(itemId);
      if (!record) {
        continue;
      }

      record.height = estimateItemHeight(record.item);
    }

    lastMeasuredViewportWidth = null;
    lastRenderedRangeKey = '';
  }

  function upsertItems(items) {
    if (!Array.isArray(items) || items.length === 0) {
      return;
    }

    const shouldStickToBottom = isScrolledNearBottom();
    ensureEmptyStateRemoved();

    for (const item of items) {
      const normalizedItem = normalizeItem(item);
      const signature = computeSignature(normalizedItem);
      let record = itemRecordsById.get(normalizedItem.id);

      if (!record) {
        itemOrder.push(normalizedItem.id);
        itemRecordsById.set(normalizedItem.id, {
          item: normalizedItem,
          signature,
          element: null,
          height: estimateItemHeight(normalizedItem),
        });
        lastRenderedRangeKey = '';
        continue;
      }

      if (record.signature === signature) {
        continue;
      }

      record.item = normalizedItem;
      record.signature = signature;
      record.height = estimateItemHeight(normalizedItem);
      if (record.element) {
        renderIntoArticle(record.element, normalizedItem);
      }
      lastRenderedRangeKey = '';
    }

    renderVisibleWindow();

    if (shouldStickToBottom) {
      scrollToBottom();
      renderVisibleWindow();
    }
  }

  function handleScroll() {
    scheduleRenderVisibleWindow();
  }

  function handleResize() {
    resetEstimatedHeights();
    scheduleRenderVisibleWindow();
  }

  container.addEventListener('scroll', handleScroll, { passive: true });
  domWindow.addEventListener('resize', handleResize);

  return {
    upsertItems,
    dispose() {
      cancelFrame(renderFrameId);
      renderFrameId = null;
      container.removeEventListener('scroll', handleScroll);
      domWindow.removeEventListener('resize', handleResize);
    },
  };
}

module.exports = {
  createTranscriptView,
};
