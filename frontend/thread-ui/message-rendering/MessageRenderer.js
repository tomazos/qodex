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
      return sanitizeHtmlFragment(parser.render(normalizedText));
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
};
