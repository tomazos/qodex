const MarkdownIt = require('markdown-it');
const { katex: katexPlugin } = require('@mdit/plugin-katex');
const createDOMPurify = require('dompurify');

const SANITIZE_OPTIONS = Object.freeze({
  USE_PROFILES: {
    html: true,
    mathMl: true,
    svg: true,
  },
  ALLOWED_URI_REGEXP: /^(?:(?:https?|mailto|tel|file):|[^a-z]|[a-z+.\-]+(?:[^a-z+.\-:]|$))/i,
});

function escapeHtml(value) {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function normalizeInput(rawText) {
  if (typeof rawText !== 'string') {
    return '';
  }

  return rawText.replace(/\r\n?/g, '\n');
}

function renderEscapedFallback(rawText) {
  if (rawText.length === 0) {
    return '';
  }

  return `<pre>${escapeHtml(rawText)}</pre>`;
}

function isEscaped(text, index) {
  let backslashCount = 0;

  for (let cursor = index - 1; cursor >= 0 && text[cursor] === '\\'; cursor -= 1) {
    backslashCount += 1;
  }

  return (backslashCount % 2) === 1;
}

function findClosingDelimiter(text, startIndex, closingDelimiter) {
  let searchIndex = startIndex;

  while (searchIndex < text.length) {
    const matchIndex = text.indexOf(closingDelimiter, searchIndex);
    if (matchIndex === -1) {
      return -1;
    }

    if (!isEscaped(text, matchIndex)) {
      return matchIndex;
    }

    searchIndex = matchIndex + closingDelimiter.length;
  }

  return -1;
}

function escapeDollarSigns(text) {
  return text.replaceAll('$', '\\$');
}

function isWhitespaceOnlyRange(text, startIndex, endIndex) {
  for (let index = startIndex; index < endIndex; index += 1) {
    const character = text[index];
    if (character !== ' ' && character !== '\t') {
      return false;
    }
  }

  return true;
}

function isAtLineStartIgnoringSpaces(text, index) {
  let lineStart = text.lastIndexOf('\n', index - 1);
  lineStart = lineStart === -1 ? 0 : lineStart + 1;
  return isWhitespaceOnlyRange(text, lineStart, index);
}

function isAtLineEndIgnoringSpaces(text, index) {
  let lineEnd = text.indexOf('\n', index);
  lineEnd = lineEnd === -1 ? text.length : lineEnd;
  return isWhitespaceOnlyRange(text, index, lineEnd);
}

function isInlineMathOpeningBoundary(text, index) {
  if (index === 0) {
    return true;
  }

  return /\s/.test(text[index - 1]);
}

function isInlineMathClosingBoundary(text, index) {
  if (index >= text.length) {
    return true;
  }

  return /[\s.,;:!?)}\]"']/.test(text[index]);
}

function rewriteLatexMathDelimiters(rawText) {
  let rewritten = '';

  for (let index = 0; index < rawText.length;) {
    if (
      rawText.startsWith('\\[', index) &&
      !isEscaped(rawText, index) &&
      isAtLineStartIgnoringSpaces(rawText, index)
    ) {
      const closingIndex = findClosingDelimiter(rawText, index + 2, '\\]');
      if (closingIndex !== -1) {
        const content = rawText.slice(index + 2, closingIndex);
        if (content.trim().length > 0 && isAtLineEndIgnoringSpaces(rawText, closingIndex + 2)) {
          rewritten += `$$\n${escapeDollarSigns(content)}\n$$`;
          index = closingIndex + 2;
          continue;
        }
      }
    }

    if (
      rawText.startsWith('\\(', index) &&
      !isEscaped(rawText, index) &&
      isInlineMathOpeningBoundary(rawText, index)
    ) {
      const closingIndex = findClosingDelimiter(rawText, index + 2, '\\)');
      if (closingIndex !== -1) {
        const content = rawText.slice(index + 2, closingIndex);
        if (
          content.trim().length > 0 &&
          !content.includes('\n') &&
          isInlineMathClosingBoundary(rawText, closingIndex + 2)
        ) {
          rewritten += `$${escapeDollarSigns(content)}$`;
          index = closingIndex + 2;
          continue;
        }
      }
    }

    rewritten += rawText[index];
    index += 1;
  }

  return rewritten;
}

function createMarkdownParser() {
  const parser = new MarkdownIt('commonmark', {
    breaks: false,
    html: false,
    linkify: false,
    typographer: false,
  })
    .enable('table')
    .use(katexPlugin, {
      allowInlineWithSpace: false,
      mathFence: true,
      output: 'htmlAndMathml',
      strict: 'ignore',
      throwOnError: false,
      trust: false,
    });

  const originalValidateLink = parser.validateLink.bind(parser);
  parser.validateLink = (url) => {
    if (typeof url === 'string' && url.toLowerCase().startsWith('file:')) {
      return true;
    }

    return originalValidateLink(url);
  };

  return parser;
}

function collapseInterTagWhitespace(html) {
  return html.replace(/>\s+</g, '><').trim();
}

function createMessageRenderer({ domWindow } = {}) {
  if (domWindow == null || domWindow.document == null) {
    throw new TypeError('createMessageRenderer requires a DOM window');
  }

  const parser = createMarkdownParser();
  const DOMPurify = createDOMPurify(domWindow);

  function sanitizeHtmlFragment(html) {
    return DOMPurify.sanitize(html, SANITIZE_OPTIONS);
  }

  function normalizeHtmlFragment(html) {
    const container = domWindow.document.createElement('div');
    container.innerHTML = sanitizeHtmlFragment(typeof html === 'string' ? html : '');
    return collapseInterTagWhitespace(container.innerHTML);
  }

  function renderToHtmlFragment(rawText) {
    const normalizedText = normalizeInput(rawText);
    if (normalizedText.length === 0) {
      return '';
    }

    try {
      return sanitizeHtmlFragment(parser.render(rewriteLatexMathDelimiters(normalizedText)));
    } catch {
      return renderEscapedFallback(normalizedText);
    }
  }

  return {
    normalizeHtmlFragment,
    renderToHtmlFragment,
  };
}

module.exports = {
  createMessageRenderer,
  normalizeInput,
  rewriteLatexMathDelimiters,
};
