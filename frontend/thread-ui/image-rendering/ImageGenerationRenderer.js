const path = require('node:path');
const { pathToFileURL } = require('node:url');

function normalizeText(value) {
  return typeof value === 'string' ? value.trim() : '';
}

function inferMimeType(savedPath) {
  const extension = path.extname(normalizeText(savedPath)).toLowerCase();
  switch (extension) {
    case '.jpg':
    case '.jpeg':
      return 'image/jpeg';
    case '.webp':
      return 'image/webp';
    case '.gif':
      return 'image/gif';
    case '.bmp':
      return 'image/bmp';
    case '.svg':
      return 'image/svg+xml';
    case '.png':
    default:
      return 'image/png';
  }
}

function toImageSource(savedPath, result) {
  const normalizedSavedPath = normalizeText(savedPath);
  if (normalizedSavedPath !== '') {
    if (/^[a-zA-Z][a-zA-Z\d+.-]*:/.test(normalizedSavedPath)) {
      return normalizedSavedPath;
    }

    try {
      return pathToFileURL(normalizedSavedPath).href;
    } catch {
      return normalizedSavedPath;
    }
  }

  const normalizedResult = normalizeText(result);
  if (normalizedResult !== '') {
    return `data:${inferMimeType(savedPath)};base64,${normalizedResult}`;
  }

  return '';
}

function appendSavedPath(document, container, savedPath) {
  const normalizedSavedPath = normalizeText(savedPath);
  if (normalizedSavedPath === '') {
    return;
  }

  const line = document.createElement('div');
  line.className = 'image-generation__saved-path';
  line.append(document.createTextNode('Saved to '));

  const link = document.createElement('a');
  link.className = 'image-generation__saved-path-link';
  link.href = normalizedSavedPath;
  link.textContent = normalizedSavedPath;
  line.append(link);

  container.append(line);
}

function appendPrompt(document, container, revisedPrompt) {
  const normalizedPrompt = normalizeText(revisedPrompt);
  if (normalizedPrompt === '') {
    return;
  }

  const prompt = document.createElement('div');
  prompt.className = 'image-generation__prompt';

  const label = document.createElement('strong');
  label.textContent = 'Prompt';
  prompt.append(label);
  prompt.append(document.createTextNode(` ${normalizedPrompt}`));

  container.append(prompt);
}

function appendStatus(document, container, status) {
  const normalizedStatus = normalizeText(status);
  if (normalizedStatus === '') {
    return;
  }

  const statusLine = document.createElement('div');
  statusLine.className = 'image-generation__status';
  statusLine.textContent = normalizedStatus
    .split('_')
    .map((part) => (part.length === 0 ? part : `${part[0].toUpperCase()}${part.slice(1)}`))
    .join(' ');
  container.append(statusLine);
}

function createImageGenerationRenderer({ domWindow }) {
  if (!domWindow || !domWindow.document) {
    throw new Error('createImageGenerationRenderer requires a DOM window');
  }

  return {
    renderToElement(imageGeneration) {
      const { document } = domWindow;
      const container = document.createElement('div');
      container.className = 'thread-item__image-generation';

      const imageSource = toImageSource(imageGeneration?.savedPath, imageGeneration?.result);
      if (imageSource !== '') {
        const image = document.createElement('img');
        image.className = 'image-generation__image';
        image.src = imageSource;
        image.alt = normalizeText(imageGeneration?.revisedPrompt) || 'Generated image';
        container.append(image);
      } else {
        const placeholder = document.createElement('div');
        placeholder.className = 'image-generation__placeholder';
        placeholder.textContent = '(image unavailable)';
        container.append(placeholder);
      }

      const details = document.createElement('div');
      details.className = 'image-generation__details';
      appendSavedPath(document, details, imageGeneration?.savedPath);
      appendPrompt(document, details, imageGeneration?.revisedPrompt);
      appendStatus(document, details, imageGeneration?.status);

      if (details.childElementCount > 0) {
        container.append(details);
      }

      return container;
    },
  };
}

module.exports = {
  createImageGenerationRenderer,
};
