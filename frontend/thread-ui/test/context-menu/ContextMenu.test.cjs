const assert = require('node:assert/strict');
const test = require('node:test');

const { buildContextMenuTemplate } = require('../../context-menu/ContextMenu');

test('builds a standard editable context menu', () => {
  const template = buildContextMenuTemplate({
    isEditable: true,
    selectionText: 'hello',
    editFlags: {
      canUndo: true,
      canRedo: false,
      canCut: true,
      canCopy: true,
      canPaste: true,
      canDelete: true,
      canSelectAll: true,
    },
  });

  assert.deepEqual(
    template.map((item) => item.role || item.type),
    ['undo', 'redo', 'separator', 'cut', 'copy', 'paste', 'delete', 'separator', 'selectAll']
  );
  assert.equal(template[0].enabled, true);
  assert.equal(template[1].enabled, false);
  assert.equal(template[5].enabled, true);
});

test('builds a copy menu for non-editable selected text', () => {
  const template = buildContextMenuTemplate({
    isEditable: false,
    selectionText: 'selected text',
    editFlags: {
      canCopy: true,
      canSelectAll: true,
    },
  });

  assert.deepEqual(
    template.map((item) => item.role || item.type),
    ['copy', 'separator', 'selectAll']
  );
});

test('returns no menu when there is nothing actionable', () => {
  const template = buildContextMenuTemplate({
    isEditable: false,
    selectionText: '',
    editFlags: {
      canCopy: false,
      canSelectAll: false,
    },
  });

  assert.deepEqual(template, []);
});
