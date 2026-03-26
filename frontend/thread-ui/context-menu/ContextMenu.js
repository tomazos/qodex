function hasNonEmptySelection(selectionText) {
  return typeof selectionText === 'string' && selectionText.length > 0;
}

function appendSeparator(template) {
  if (template.length === 0) {
    return;
  }

  if (template[template.length - 1].type === 'separator') {
    return;
  }

  template.push({ type: 'separator' });
}

function buildContextMenuTemplate(params) {
  const editFlags = params?.editFlags || {};
  const template = [];

  if (params?.isEditable) {
    template.push({ role: 'undo', enabled: editFlags.canUndo === true });
    template.push({ role: 'redo', enabled: editFlags.canRedo === true });

    appendSeparator(template);

    template.push({ role: 'cut', enabled: editFlags.canCut === true });
    template.push({ role: 'copy', enabled: editFlags.canCopy === true });
    template.push({ role: 'paste', enabled: editFlags.canPaste === true });
    template.push({ role: 'delete', enabled: editFlags.canDelete === true });

    appendSeparator(template);

    template.push({ role: 'selectAll', enabled: editFlags.canSelectAll === true });
    return template;
  }

  if (hasNonEmptySelection(params?.selectionText)) {
    template.push({ role: 'copy', enabled: editFlags.canCopy !== false });

    if (editFlags.canSelectAll === true) {
      appendSeparator(template);
      template.push({ role: 'selectAll', enabled: true });
    }
  }

  return template;
}

module.exports = {
  buildContextMenuTemplate,
};
