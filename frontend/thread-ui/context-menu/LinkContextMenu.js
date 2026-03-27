function appendSeparator(template) {
  if (template.length === 0) {
    return;
  }

  if (template[template.length - 1].type === 'separator') {
    return;
  }

  template.push({ type: 'separator' });
}

function pushAction(template, label, actionKind, actionPayload, enabled = true) {
  template.push({
    label,
    enabled,
    click: () => {
      if (!enabled || typeof actionPayload?.onAction !== 'function') {
        return;
      }

      void actionPayload.onAction({
        actionKind,
        resolvedLink: actionPayload.resolvedLink,
        rawHref: actionPayload.rawHref,
        linkText: actionPayload.linkText,
      });
    },
  });
}

function buildLinkContextMenuTemplate({ resolvedLink, rawHref, linkText, onAction }) {
  const template = [];
  const actionPayload = {
    resolvedLink,
    rawHref: typeof rawHref === 'string' ? rawHref : '',
    linkText: typeof linkText === 'string' ? linkText : '',
    onAction,
  };

  if (resolvedLink?.canOpen === true) {
    pushAction(
      template,
      resolvedLink.isDirectory ? 'Open Folder' : 'Open',
      'open',
      actionPayload,
      true
    );
  }

  if (resolvedLink?.canOpenExternally === true) {
    pushAction(template, 'Open Externally', 'open_externally', actionPayload, true);
  }

  if (resolvedLink?.canRevealInFolder === true) {
    pushAction(template, 'Reveal in Folder', 'reveal_in_folder', actionPayload, true);
  }

  appendSeparator(template);

  if (actionPayload.linkText.length > 0) {
    pushAction(template, 'Copy Link Text', 'copy_link_text', actionPayload, true);
  }

  const copyAddressEnabled =
    (typeof resolvedLink?.normalizedHref === 'string' && resolvedLink.normalizedHref.length > 0) ||
    actionPayload.rawHref.length > 0;
  pushAction(template, 'Copy Link Address', 'copy_link_address', actionPayload, copyAddressEnabled);

  if (resolvedLink?.canCopyResolvedPath === true && typeof resolvedLink.resolvedPath === 'string' && resolvedLink.resolvedPath.length > 0) {
    pushAction(template, 'Copy Resolved Path', 'copy_resolved_path', actionPayload, true);
  }

  return template.filter((item, index, items) => {
    if (item.type !== 'separator') {
      return true;
    }

    if (index === 0 || index === items.length - 1) {
      return false;
    }

    return items[index - 1].type !== 'separator' && items[index + 1].type !== 'separator';
  });
}

module.exports = {
  buildLinkContextMenuTemplate,
};
