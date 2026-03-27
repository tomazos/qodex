const DEFAULT_ESTIMATED_ITEM_HEIGHT_PX = 120;
const DEFAULT_FALLBACK_VIEWPORT_HEIGHT_PX = 600;
const DEFAULT_ITEM_GAP_PX = 12;
const DEFAULT_OVERSCAN_PX = 600;
const DEFAULT_BACKGROUND_MEASUREMENT_BATCH_SIZE = 8;
const DEFAULT_SCROLL_IDLE_DELAY_MS = 120;

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
  deferredMeasurementEnabled = true,
  backgroundMeasurementBatchSize = DEFAULT_BACKGROUND_MEASUREMENT_BATCH_SIZE,
  scrollIdleDelayMs = DEFAULT_SCROLL_IDLE_DELAY_MS,
}) {
  if (!domWindow || !container) {
    throw new Error('createTranscriptView requires a DOM window and transcript container.');
  }

  const { document } = domWindow;
  const normalizedItemGapPx = Number.isFinite(itemGapPx) ? Math.max(0, itemGapPx) : DEFAULT_ITEM_GAP_PX;
  const normalizedOverscanPx = Number.isFinite(overscanPx) ? Math.max(0, overscanPx) : DEFAULT_OVERSCAN_PX;
  const normalizedBackgroundMeasurementBatchSize = Number.isFinite(backgroundMeasurementBatchSize)
    ? Math.max(1, Math.floor(backgroundMeasurementBatchSize))
    : DEFAULT_BACKGROUND_MEASUREMENT_BATCH_SIZE;
  const normalizedScrollIdleDelayMs = Number.isFinite(scrollIdleDelayMs)
    ? Math.max(0, scrollIdleDelayMs)
    : DEFAULT_SCROLL_IDLE_DELAY_MS;
  const itemRecordsById = new Map();
  const itemOrder = [];
  const pendingMeasurementItemIds = [];
  const pendingMeasurementItemIdSet = new Set();
  const virtualRoot = document.createElement('div');
  const mountedItemsHost = document.createElement('div');
  const measurementHost = document.createElement('div');
  let currentEmptyStateElement = emptyStateElement || null;
  let anonymousItemSequence = 1;
  let renderFrameId = null;
  let stickToBottomFrameId = null;
  let stickToBottomTimeoutId = null;
  let lateStickToBottomTimeoutId = null;
  let measurementBatchTimeoutId = null;
  let lastRenderedRangeKey = '';
  let lastMeasuredViewportWidth = null;
  let cachedTotalTranscriptHeight = 0;
  let pendingMeasurementReadIndex = 0;
  let lastScrollEventTimestampMs = 0;

  virtualRoot.className = 'thread-view__items-virtual';
  virtualRoot.hidden = true;
  mountedItemsHost.className = 'thread-view__mounted';
  virtualRoot.append(mountedItemsHost);
  container.append(virtualRoot);
  Object.assign(measurementHost.style, {
    position: 'absolute',
    left: '-100000px',
    top: '0',
    visibility: 'hidden',
    pointerEvents: 'none',
    contain: 'layout style paint',
    overflow: 'hidden',
  });
  document.body.append(measurementHost);

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

  function effectiveScrollHeight() {
    return Math.max(container.scrollHeight, totalTranscriptHeight());
  }

  function currentTimestampMs() {
    if (domWindow.performance && typeof domWindow.performance.now === 'function') {
      return domWindow.performance.now();
    }

    return Date.now();
  }

  function currentMeasurementWidthPx() {
    const measuredWidth = container.clientWidth || mountedItemsHost.clientWidth || container.getBoundingClientRect().width;
    if (Number.isFinite(measuredWidth) && measuredWidth > 0) {
      return measuredWidth;
    }

    return 800;
  }

  function isScrolledNearBottom() {
    const remaining = effectiveScrollHeight() - container.scrollTop - container.clientHeight;
    return remaining <= 4;
  }

  function approximateBottomScrollTop() {
    return Math.max(0, totalTranscriptHeight() - viewportHeight());
  }

  function scrollToBottom() {
    container.scrollTop = approximateBottomScrollTop();
  }

  function scrollToDomBottom() {
    container.scrollTop = container.scrollHeight;
  }

  function alignLastItemIntoView() {
    if (itemOrder.length === 0) {
      return;
    }

    const lastRecord = itemRecordsById.get(itemOrder[itemOrder.length - 1]);
    if (!lastRecord || !lastRecord.element || lastRecord.element.parentNode !== mountedItemsHost) {
      return;
    }

    if (typeof lastRecord.element.scrollIntoView === 'function') {
      lastRecord.element.scrollIntoView({
        block: 'end',
        inline: 'nearest',
      });
    }
  }

  function stickToBottom() {
    scrollToBottom();
    renderVisibleWindow();
    alignLastItemIntoView();
    renderVisibleWindow();
    scrollToBottom();
    alignLastItemIntoView();
    renderVisibleWindow();
  }

  function nudgeDomScrollToBottom() {
    scrollToDomBottom();
    scheduleRenderVisibleWindow();
  }

  function scheduleStickToBottom() {
    cancelFrame(stickToBottomFrameId);
    cancelTimeout(stickToBottomTimeoutId);
    cancelTimeout(lateStickToBottomTimeoutId);

    stickToBottomTimeoutId = requestTimeout(() => {
      stickToBottomTimeoutId = null;
      nudgeDomScrollToBottom();
    }, 0);

    lateStickToBottomTimeoutId = requestTimeout(() => {
      lateStickToBottomTimeoutId = null;
      nudgeDomScrollToBottom();
    }, 50);

    stickToBottomFrameId = requestFrame(() => {
      stickToBottomFrameId = null;
      stickToBottom();
      stickToBottomFrameId = requestFrame(() => {
        stickToBottomFrameId = null;
        stickToBottom();
      });
    });
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
    article.style.position = 'absolute';
    article.style.left = '0';
    article.style.right = '0';
    renderIntoArticle(article, record.item);
    record.element = article;
    return article;
  }

  function prepareMeasurementHost() {
    measurementHost.style.width = `${Math.max(1, Math.round(currentMeasurementWidthPx()))}px`;
  }

  function measureItemHeight(item, fallbackHeight) {
    prepareMeasurementHost();
    const article = document.createElement('article');
    article.dataset.itemId = item.id;
    renderIntoArticle(article, item);
    measurementHost.replaceChildren(article);
    const measuredHeight = measureElementHeight(article, fallbackHeight);
    measurementHost.replaceChildren();
    if (Number.isFinite(measuredHeight) && measuredHeight > 0) {
      return measuredHeight;
    }

    return fallbackHeight;
  }

  function totalTranscriptHeight() {
    return cachedTotalTranscriptHeight;
  }

  function recomputeLayoutMetrics() {
    let nextTop = 0;
    for (const itemId of itemOrder) {
      const record = itemRecordsById.get(itemId);
      if (!record) {
        continue;
      }

      record.top = nextTop;
      nextTop += record.height + normalizedItemGapPx;
    }

    cachedTotalTranscriptHeight = nextTop > 0 ? nextTop - normalizedItemGapPx : 0;
    lastRenderedRangeKey = '';
  }

  function itemTopForIndex(index) {
    if (index < 0 || index >= itemOrder.length) {
      return 0;
    }

    const record = itemRecordsById.get(itemOrder[index]);
    return record ? record.top : 0;
  }

  function updateVirtualCanvasHeight() {
    const canvasHeightPx = Math.max(totalTranscriptHeight(), viewportHeight());
    virtualRoot.style.height = `${canvasHeightPx}px`;
    mountedItemsHost.style.height = `${canvasHeightPx}px`;
  }

  function compactPendingMeasurements() {
    if (pendingMeasurementReadIndex === 0) {
      return;
    }

    if (pendingMeasurementReadIndex >= pendingMeasurementItemIds.length) {
      pendingMeasurementItemIds.length = 0;
      pendingMeasurementReadIndex = 0;
      return;
    }

    if (pendingMeasurementReadIndex >= 32) {
      pendingMeasurementItemIds.splice(0, pendingMeasurementReadIndex);
      pendingMeasurementReadIndex = 0;
    }
  }

  function hasPendingMeasurements() {
    return pendingMeasurementItemIdSet.size > 0;
  }

  function discardPendingMeasurement(itemId) {
    pendingMeasurementItemIdSet.delete(itemId);
  }

  function enqueuePendingMeasurement(itemId) {
    if (!deferredMeasurementEnabled || typeof itemId !== 'string' || itemId.length === 0) {
      return;
    }

    if (pendingMeasurementItemIdSet.has(itemId)) {
      return;
    }

    pendingMeasurementItemIdSet.add(itemId);
    pendingMeasurementItemIds.push(itemId);
  }

  function enqueuePendingMeasurementsForAllItems() {
    if (!deferredMeasurementEnabled) {
      return;
    }

    for (const itemId of itemOrder) {
      enqueuePendingMeasurement(itemId);
    }
  }

  function isScrollInteractionActive() {
    return currentTimestampMs() - lastScrollEventTimestampMs < normalizedScrollIdleDelayMs;
  }

  function captureScrollAnchor() {
    if (itemOrder.length === 0) {
      return null;
    }

    const targetOffset = Math.max(0, container.scrollTop);

    for (let index = 0; index < itemOrder.length; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record) {
        continue;
      }

      const itemTop = record.top;
      const itemBottom = itemTop + record.height;
      if (targetOffset <= itemBottom || index === itemOrder.length - 1) {
        return {
          itemId: itemOrder[index],
          offsetWithinItem: Math.max(0, targetOffset - itemTop),
        };
      }
    }

    return null;
  }

  function restoreScrollAnchor(anchor) {
    if (!anchor || typeof anchor.itemId !== 'string' || anchor.itemId.length === 0) {
      return;
    }

    const anchorIndex = itemOrder.indexOf(anchor.itemId);
    if (anchorIndex < 0) {
      return;
    }

    container.scrollTop = Math.max(0, itemTopForIndex(anchorIndex) + anchor.offsetWithinItem);
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
      };
    }

    const viewTop = Math.max(0, container.scrollTop - normalizedOverscanPx);
    const viewBottom = container.scrollTop + viewportHeight() + normalizedOverscanPx;
    let startIndex = 0;
    let foundStart = false;
    let endIndexExclusive = itemOrder.length;

    for (let index = 0; index < itemOrder.length; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record) {
        continue;
      }

      const itemTop = record.top;
      const itemBottom = itemTop + record.height;

      if (!foundStart && itemBottom >= viewTop) {
        startIndex = index;
        foundStart = true;
      }

      if (foundStart && itemTop > viewBottom) {
        endIndexExclusive = index;
        break;
      }
    }

    if (!foundStart) {
      startIndex = Math.max(0, itemOrder.length - 1);
      endIndexExclusive = itemOrder.length;
    }

    if (endIndexExclusive <= startIndex) {
      endIndexExclusive = Math.min(itemOrder.length, startIndex + 1);
    }

    return {
      startIndex,
      endIndexExclusive,
    };
  }

  function mountVisibleRange(range) {
    const rangeKey = `${range.startIndex}:${range.endIndexExclusive}`;
    if (rangeKey !== lastRenderedRangeKey) {
      const fragment = document.createDocumentFragment();
      for (let index = range.startIndex; index < range.endIndexExclusive; index += 1) {
        const record = itemRecordsById.get(itemOrder[index]);
        if (!record) {
          continue;
        }

        fragment.append(ensureRecordElement(record));
      }

      mountedItemsHost.replaceChildren(fragment);
      lastRenderedRangeKey = rangeKey;
    }

    for (let index = range.startIndex; index < range.endIndexExclusive; index += 1) {
      const record = itemRecordsById.get(itemOrder[index]);
      if (!record || !record.element) {
        continue;
      }

      record.element.style.top = `${record.top}px`;
    }

    return range;
  }

  function renderVisibleWindow() {
    if (itemOrder.length === 0) {
      mountedItemsHost.replaceChildren();
      virtualRoot.style.height = '0px';
      mountedItemsHost.style.height = '0px';
      lastRenderedRangeKey = '';
      virtualRoot.hidden = true;
      return;
    }

    virtualRoot.hidden = false;
    updateVirtualCanvasHeight();

    const initialRange = computeVisibleRange();
    mountVisibleRange(initialRange);

    const measuredViewportWidth = mountedItemsHost.clientWidth;
    if (
      Number.isFinite(measuredViewportWidth) &&
      measuredViewportWidth > 0 &&
      lastMeasuredViewportWidth !== null &&
      measuredViewportWidth !== lastMeasuredViewportWidth
    ) {
      lastMeasuredViewportWidth = measuredViewportWidth;
      handleViewportWidthChange();
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

  function requestTimeout(callback, delayMs) {
    if (typeof domWindow.setTimeout === 'function') {
      return domWindow.setTimeout(callback, delayMs);
    }

    callback();
    return null;
  }

  function cancelTimeout(timeoutId) {
    if (timeoutId === null || typeof domWindow.clearTimeout !== 'function') {
      return;
    }

    domWindow.clearTimeout(timeoutId);
  }

  function scheduleBackgroundMeasurement(delayMs = 0) {
    if (!deferredMeasurementEnabled || measurementBatchTimeoutId !== null || !hasPendingMeasurements()) {
      return;
    }

    measurementBatchTimeoutId = requestTimeout(() => {
      measurementBatchTimeoutId = null;
      processBackgroundMeasurementBatch();
    }, delayMs);
  }

  function applyMeasuredHeightsForRecordIds(recordIds) {
    if (!Array.isArray(recordIds) || recordIds.length === 0) {
      return false;
    }

    const shouldStickToBottom = isScrolledNearBottom();
    const scrollAnchor = shouldStickToBottom ? null : captureScrollAnchor();
    let anyHeightChanged = false;

    for (const itemId of recordIds) {
      const record = itemRecordsById.get(itemId);
      if (!record) {
        continue;
      }

      const fallbackHeight = estimateItemHeight(record.item);
      const measuredHeight = measureItemHeight(record.item, fallbackHeight);
      if (measuredHeight !== record.height) {
        record.height = measuredHeight;
        anyHeightChanged = true;
      }
    }

    if (!anyHeightChanged) {
      return false;
    }

    recomputeLayoutMetrics();
    if (scrollAnchor) {
      restoreScrollAnchor(scrollAnchor);
    }
    if (shouldStickToBottom) {
      scrollToBottom();
    }
    renderVisibleWindow();
    return true;
  }

  function measureVisibleRangeSynchronously() {
    if (!deferredMeasurementEnabled || itemOrder.length === 0) {
      return false;
    }

    const range = computeVisibleRange();
    const itemIds = [];
    for (let index = range.startIndex; index < range.endIndexExclusive; index += 1) {
      const itemId = itemOrder[index];
      itemIds.push(itemId);
      discardPendingMeasurement(itemId);
    }

    compactPendingMeasurements();
    return applyMeasuredHeightsForRecordIds(itemIds);
  }

  function processBackgroundMeasurementBatch() {
    if (!deferredMeasurementEnabled || !hasPendingMeasurements()) {
      return;
    }

    if (isScrollInteractionActive()) {
      scheduleBackgroundMeasurement(normalizedScrollIdleDelayMs);
      return;
    }

    const batchItemIds = [];
    while (
      batchItemIds.length < normalizedBackgroundMeasurementBatchSize &&
      pendingMeasurementReadIndex < pendingMeasurementItemIds.length
    ) {
      const itemId = pendingMeasurementItemIds[pendingMeasurementReadIndex++];
      if (!pendingMeasurementItemIdSet.delete(itemId)) {
        continue;
      }

      batchItemIds.push(itemId);
    }

    compactPendingMeasurements();
    applyMeasuredHeightsForRecordIds(batchItemIds);

    if (hasPendingMeasurements()) {
      scheduleBackgroundMeasurement(normalizedScrollIdleDelayMs);
    }
  }

  function handleViewportWidthChange() {
    const shouldStickToBottom = isScrolledNearBottom();
    const scrollAnchor = shouldStickToBottom ? null : captureScrollAnchor();

    resetEstimatedHeights();
    if (scrollAnchor) {
      restoreScrollAnchor(scrollAnchor);
    }
    if (shouldStickToBottom) {
      scrollToBottom();
    }

    updateVirtualCanvasHeight();
    mountVisibleRange(computeVisibleRange());

    if (deferredMeasurementEnabled) {
      enqueuePendingMeasurementsForAllItems();
      measureVisibleRangeSynchronously();
      scheduleBackgroundMeasurement(normalizedScrollIdleDelayMs);
      return;
    }

    remeasureAllHeights();
    if (scrollAnchor) {
      restoreScrollAnchor(scrollAnchor);
    }
    if (shouldStickToBottom) {
      scrollToBottom();
    }

    updateVirtualCanvasHeight();
    mountVisibleRange(computeVisibleRange());
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

    recomputeLayoutMetrics();
    lastMeasuredViewportWidth = null;
  }

  function remeasureAllHeights() {
    for (const itemId of itemOrder) {
      const record = itemRecordsById.get(itemId);
      if (!record) {
        continue;
      }

      record.height = measureItemHeight(record.item, estimateItemHeight(record.item));
    }

    recomputeLayoutMetrics();
    lastMeasuredViewportWidth = null;
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
        const estimatedHeight = estimateItemHeight(normalizedItem);
        itemOrder.push(normalizedItem.id);
        itemRecordsById.set(normalizedItem.id, {
          item: normalizedItem,
          signature,
          element: null,
          height: deferredMeasurementEnabled ? estimatedHeight : measureItemHeight(normalizedItem, estimatedHeight),
          top: 0,
        });
        enqueuePendingMeasurement(normalizedItem.id);
        continue;
      }

      if (record.signature === signature) {
        continue;
      }

      record.item = normalizedItem;
      record.signature = signature;
      {
        const estimatedHeight = estimateItemHeight(normalizedItem);
        record.height = deferredMeasurementEnabled ? estimatedHeight : measureItemHeight(normalizedItem, estimatedHeight);
      }
      enqueuePendingMeasurement(normalizedItem.id);
      if (record.element) {
        renderIntoArticle(record.element, normalizedItem);
      }
    }

    recomputeLayoutMetrics();
    renderVisibleWindow();

    if (shouldStickToBottom) {
      stickToBottom();
      scheduleStickToBottom();
    }

    if (deferredMeasurementEnabled) {
      measureVisibleRangeSynchronously();
      scheduleBackgroundMeasurement(normalizedScrollIdleDelayMs);
    }
  }

  function handleScroll() {
    lastScrollEventTimestampMs = currentTimestampMs();
    scheduleRenderVisibleWindow();
    if (hasPendingMeasurements()) {
      scheduleBackgroundMeasurement(normalizedScrollIdleDelayMs);
    }
  }

  function handleResize() {
    scheduleRenderVisibleWindow();
  }

  container.addEventListener('scroll', handleScroll, { passive: true });
  domWindow.addEventListener('resize', handleResize);

  return {
    upsertItems,
    scrollToEndSoon() {
      requestTimeout(() => {
        scrollToDomBottom();
      }, 0);
      requestTimeout(() => {
        scrollToDomBottom();
      }, 50);
    },
    dispose() {
      cancelFrame(renderFrameId);
      cancelFrame(stickToBottomFrameId);
      cancelTimeout(stickToBottomTimeoutId);
      cancelTimeout(lateStickToBottomTimeoutId);
      cancelTimeout(measurementBatchTimeoutId);
      renderFrameId = null;
      stickToBottomFrameId = null;
      stickToBottomTimeoutId = null;
      lateStickToBottomTimeoutId = null;
      measurementBatchTimeoutId = null;
      container.removeEventListener('scroll', handleScroll);
      domWindow.removeEventListener('resize', handleResize);
      measurementHost.remove();
    },
  };
}

module.exports = {
  createTranscriptView,
};
