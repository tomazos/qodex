const commonmarkSpec = require('commonmark-spec');
const { JSDOM } = require('jsdom');

const { createMessageRenderer } = require('../../message-rendering/MessageRenderer');
const { expectationForCase } = require('./commonmark_expectations.cjs');

function decodeCommonMarkVisibleTabs(text) {
  return text.replaceAll('→', '\t');
}

function createConformanceRenderer() {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createMessageRenderer({ domWindow: dom.window });

  return {
    renderMarkdown(text) {
      return renderer.renderToHtmlFragment(text);
    },
    normalizeHtml(html) {
      return renderer.normalizeHtmlFragment(html);
    },
  };
}

function evaluateCommonMarkCase(specCase, renderer) {
  const expectation = expectationForCase(specCase);
  const actual = renderer.normalizeHtml(
    renderer.renderMarkdown(decodeCommonMarkVisibleTabs(specCase.markdown))
  );
  const expected = renderer.normalizeHtml(decodeCommonMarkVisibleTabs(specCase.html));
  const matchedCommonMark = actual === expected;
  const passed = matchedCommonMark === expectation.expectCommonMarkMatch;

  return {
    number: specCase.number,
    section: specCase.section,
    matchedCommonMark,
    passed,
    expectation,
    actual,
    expected,
  };
}

function runCommonMarkConformance() {
  const renderer = createConformanceRenderer();
  const caseResults = commonmarkSpec.tests.map((specCase) => evaluateCommonMarkCase(specCase, renderer));
  const failures = caseResults.filter((result) => !result.passed);
  const expectedMatches = caseResults.filter((result) => result.expectation.expectCommonMarkMatch);
  const expectedRejections = caseResults.filter((result) => !result.expectation.expectCommonMarkMatch);

  return {
    totalCases: caseResults.length,
    caseResults,
    failures,
    expectedMatches,
    expectedRejections,
    matchedCount: caseResults.filter((result) => result.matchedCommonMark).length,
  };
}

function formatFailure(result) {
  return [
    `- Example #${result.number} (${result.section})`,
    `  expected CommonMark match: ${result.expectation.expectCommonMarkMatch}`,
    `  reason: ${result.expectation.reason}`,
    `  source: ${result.expectation.source}`,
    `  actual: ${JSON.stringify(result.actual)}`,
    `  expected CommonMark: ${JSON.stringify(result.expected)}`,
  ].join('\n');
}

function formatConformanceReport(summary) {
  const lines = [
    'ThreadUI CommonMark conformance summary',
    `- total examples: ${summary.totalCases}`,
    `- expected CommonMark matches: ${summary.expectedMatches.length}`,
    `- expected deliberate deviations: ${summary.expectedRejections.length}`,
    `- examples currently matching CommonMark: ${summary.matchedCount}`,
    `- failures: ${summary.failures.length}`,
  ];

  if (summary.failures.length > 0) {
    lines.push('', 'Failures:');
    for (const failure of summary.failures) {
      lines.push(formatFailure(failure));
    }
  }

  return `${lines.join('\n')}\n`;
}

module.exports = {
  runCommonMarkConformance,
  formatConformanceReport,
};
