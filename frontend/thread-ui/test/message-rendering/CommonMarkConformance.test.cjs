const assert = require('node:assert/strict');
const test = require('node:test');

const {
  formatConformanceReport,
  runCommonMarkConformance,
} = require('../../test-support/message-rendering/commonmark_runner.cjs');

test('ThreadUI markdown renderer matches the explicit CommonMark expectation set', () => {
  const summary = runCommonMarkConformance();
  assert.equal(summary.failures.length, 0, formatConformanceReport(summary));
});
