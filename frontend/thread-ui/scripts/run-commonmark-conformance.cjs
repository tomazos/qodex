#!/usr/bin/env node

const {
  formatConformanceReport,
  runCommonMarkConformance,
} = require('../test-support/message-rendering/commonmark_runner.cjs');

const summary = runCommonMarkConformance();
process.stdout.write(formatConformanceReport(summary));

if (summary.failures.length > 0) {
  process.exitCode = 1;
}
