function expandRange(start, end) {
  return Array.from({ length: (end - start) + 1 }, (_, index) => start + index);
}

function createExpectationMap() {
  const expectationMap = new Map();

  const deliberateDeviationGroups = [
    {
      numbers: expandRange(148, 191),
      reason: 'Raw HTML is intentionally disabled, so CommonMark HTML block examples are rendered as text instead of passthrough HTML.',
      source: 'range:148-191',
    },
    {
      numbers: [613, 614, 615, 616, 617, 623, 625, 626, 627, 628, 629, 630, 631],
      reason: 'Raw HTML is intentionally disabled, so these CommonMark raw HTML examples are rendered as text instead of passthrough HTML.',
      source: 'examples:613-617,623,625-631',
    },
    {
      numbers: [21, 31, 201, 308, 309, 344, 475, 476, 477, 491, 494, 524, 536, 642, 643],
      reason: 'This CommonMark example depends on raw HTML parsing or passthrough behavior, which qodex intentionally disables.',
      source: 'examples:21,31,201,308,309,344,475,476,477,491,494,524,536,642,643',
    },
  ];

  for (const group of deliberateDeviationGroups) {
    for (const number of group.numbers) {
      expectationMap.set(number, {
        expectCommonMarkMatch: false,
        reason: group.reason,
        source: group.source,
      });
    }
  }

  return expectationMap;
}

const EXAMPLE_EXPECTATIONS = createExpectationMap();

function expectationForCase(specCase) {
  if (EXAMPLE_EXPECTATIONS.has(specCase.number)) {
    return EXAMPLE_EXPECTATIONS.get(specCase.number);
  }

  return {
    expectCommonMarkMatch: true,
    reason: 'Expected to match the CommonMark reference output.',
    source: 'default',
  };
}

module.exports = {
  expectationForCase,
  EXAMPLE_EXPECTATIONS,
};
