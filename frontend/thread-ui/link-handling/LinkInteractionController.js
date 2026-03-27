function findAnchorTarget(target) {
  if (!target || typeof target.closest !== 'function') {
    return null;
  }

  return target.closest('a[href]');
}

function readRawHref(anchor) {
  if (!anchor || typeof anchor.getAttribute !== 'function') {
    return '';
  }

  const rawHref = anchor.getAttribute('href');
  return typeof rawHref === 'string' ? rawHref.trim() : '';
}

function readLinkText(anchor) {
  if (!anchor) {
    return '';
  }

  return typeof anchor.textContent === 'string' ? anchor.textContent.trim() : '';
}

function normalizeOptionalInteger(value) {
  if (typeof value === 'bigint') {
    return Number(value);
  }

  if (typeof value === 'number' && Number.isFinite(value)) {
    return value;
  }

  return 0;
}

function normalizeResolvedLink(rawHref, resolvedLink) {
  if (!resolvedLink || typeof resolvedLink !== 'object') {
    return {
      ok: false,
      rawHref,
      normalizedHref: rawHref,
      tooltip: rawHref,
      defaultAction: 'none',
    };
  }

  return {
    ok: resolvedLink.ok === true,
    message: typeof resolvedLink.message === 'string' ? resolvedLink.message : '',
    rawHref: typeof resolvedLink.rawHref === 'string' && resolvedLink.rawHref.length > 0 ? resolvedLink.rawHref : rawHref,
    normalizedHref: typeof resolvedLink.normalizedHref === 'string' ? resolvedLink.normalizedHref : rawHref,
    tooltip: typeof resolvedLink.tooltip === 'string' && resolvedLink.tooltip.length > 0
      ? resolvedLink.tooltip
      : (typeof resolvedLink.normalizedHref === 'string' && resolvedLink.normalizedHref.length > 0 ? resolvedLink.normalizedHref : rawHref),
    kind: typeof resolvedLink.kind === 'string' ? resolvedLink.kind : 'unknown',
    resolvedPath: typeof resolvedLink.resolvedPath === 'string' ? resolvedLink.resolvedPath : '',
    exists: resolvedLink.exists === true,
    isDirectory: resolvedLink.isDirectory === true,
    hasLine: resolvedLink.hasLine === true,
    line: normalizeOptionalInteger(resolvedLink.line),
    hasColumn: resolvedLink.hasColumn === true,
    column: normalizeOptionalInteger(resolvedLink.column),
    defaultAction: typeof resolvedLink.defaultAction === 'string' ? resolvedLink.defaultAction : 'none',
    canOpen: resolvedLink.canOpen === true,
    canOpenExternally: resolvedLink.canOpenExternally === true,
    canRevealInFolder: resolvedLink.canRevealInFolder === true,
    canCopyResolvedPath: resolvedLink.canCopyResolvedPath === true,
  };
}

function createLinkInteractionController({
  container,
  resolveLink,
  performLinkAction,
  showLinkContextMenu,
}) {
  if (!container) {
    throw new Error('createLinkInteractionController requires a container element.');
  }

  if (typeof resolveLink !== 'function') {
    throw new TypeError('createLinkInteractionController requires a resolveLink function.');
  }

  if (typeof performLinkAction !== 'function') {
    throw new TypeError('createLinkInteractionController requires a performLinkAction function.');
  }

  if (typeof showLinkContextMenu !== 'function') {
    throw new TypeError('createLinkInteractionController requires a showLinkContextMenu function.');
  }

  const resolutionCache = new Map();

  function resolveDescriptor(rawHref) {
    if (typeof rawHref !== 'string' || rawHref.trim() === '') {
      return Promise.resolve(normalizeResolvedLink('', null));
    }

    const trimmedHref = rawHref.trim();
    let pendingResolution = resolutionCache.get(trimmedHref);
    if (!pendingResolution) {
      pendingResolution = Promise.resolve(resolveLink(trimmedHref))
        .then((resolvedLink) => normalizeResolvedLink(trimmedHref, resolvedLink))
        .catch(() => normalizeResolvedLink(trimmedHref, null));
      resolutionCache.set(trimmedHref, pendingResolution);
    }

    return pendingResolution;
  }

  async function updateAnchorTooltip(anchor) {
    const rawHref = readRawHref(anchor);
    if (rawHref.length === 0) {
      return;
    }

    const resolvedLink = await resolveDescriptor(rawHref);
    if (!anchor.isConnected || readRawHref(anchor) !== rawHref) {
      return;
    }

    anchor.title = resolvedLink.tooltip;
  }

  async function handleAnchorClick(event) {
    if (event.defaultPrevented || event.button !== 0) {
      return;
    }

    const anchor = findAnchorTarget(event.target);
    if (!anchor) {
      return;
    }

    const rawHref = readRawHref(anchor);
    if (rawHref.length === 0) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();

    const resolvedLink = await resolveDescriptor(rawHref);
    if (resolvedLink.defaultAction === 'none') {
      return;
    }

    await performLinkAction({
      actionKind: resolvedLink.defaultAction,
      resolvedLink,
      rawHref,
      linkText: readLinkText(anchor),
    });
  }

  async function handleAnchorContextMenu(event) {
    const anchor = findAnchorTarget(event.target);
    if (!anchor) {
      return;
    }

    const rawHref = readRawHref(anchor);
    if (rawHref.length === 0) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();

    const resolvedLink = await resolveDescriptor(rawHref);
    await showLinkContextMenu({
      resolvedLink,
      rawHref,
      linkText: readLinkText(anchor),
    });
  }

  function handlePointerOver(event) {
    const anchor = findAnchorTarget(event.target);
    if (!anchor) {
      return;
    }

    void updateAnchorTooltip(anchor);
  }

  function handleFocusIn(event) {
    const anchor = findAnchorTarget(event.target);
    if (!anchor) {
      return;
    }

    void updateAnchorTooltip(anchor);
  }

  container.addEventListener('click', handleAnchorClick);
  container.addEventListener('contextmenu', handleAnchorContextMenu);
  container.addEventListener('mouseover', handlePointerOver);
  container.addEventListener('focusin', handleFocusIn);

  return {
    dispose() {
      container.removeEventListener('click', handleAnchorClick);
      container.removeEventListener('contextmenu', handleAnchorContextMenu);
      container.removeEventListener('mouseover', handlePointerOver);
      container.removeEventListener('focusin', handleFocusIn);
      resolutionCache.clear();
    },
  };
}

module.exports = {
  createLinkInteractionController,
};
