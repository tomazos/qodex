const path = require('node:path');
const { pathToFileURL } = require('node:url');

function normalizeText(value) {
  return typeof value === 'string' ? value.trim() : '';
}

function toImageSource(imagePath) {
  const normalizedPath = normalizeText(imagePath);
  if (normalizedPath === '') {
    return '';
  }

  if (/^[a-zA-Z][a-zA-Z\d+.-]*:/.test(normalizedPath)) {
    return normalizedPath;
  }

  try {
    return pathToFileURL(normalizedPath).href;
  } catch {
    return normalizedPath;
  }
}

function imageAltText(imagePath) {
  const normalizedPath = normalizeText(imagePath);
  const basename = path.basename(normalizedPath);
  return basename === '' ? 'Viewed image' : `Viewed image ${basename}`;
}

function createImageViewRenderer({ domWindow }) {
  if (!domWindow || !domWindow.document) {
    throw new Error('createImageViewRenderer requires a DOM window');
  }

  return {
    renderToElement(imageView) {
      const { document } = domWindow;
      const container = document.createElement('div');
      container.className = 'thread-item__image-view';

      const normalizedPath = normalizeText(imageView?.path);
      const imageSource = toImageSource(normalizedPath);
      if (imageSource !== '') {
        const image = document.createElement('img');
        image.className = 'image-view__image';
        image.src = imageSource;
        image.alt = imageAltText(normalizedPath);
        container.append(image);
      } else {
        const placeholder = document.createElement('div');
        placeholder.className = 'image-view__placeholder';
        placeholder.textContent = '(image unavailable)';
        container.append(placeholder);
      }

      if (normalizedPath !== '') {
        const details = document.createElement('div');
        details.className = 'image-view__details';

        const line = document.createElement('div');
        line.className = 'image-view__path';
        line.append(document.createTextNode('Viewed '));

        const link = document.createElement('a');
        link.className = 'image-view__path-link';
        link.href = normalizedPath;
        link.textContent = normalizedPath;
        line.append(link);

        details.append(line);
        container.append(details);
      }

      return container;
    },
  };
}

module.exports = {
  createImageViewRenderer,
};
