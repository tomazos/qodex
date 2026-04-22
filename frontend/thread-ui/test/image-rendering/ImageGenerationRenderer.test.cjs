const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createImageGenerationRenderer } = require('../../image-rendering/ImageGenerationRenderer');

function renderImageGeneration(imageGeneration) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createImageGenerationRenderer({ domWindow: dom.window });
  return renderer.renderToElement(imageGeneration);
}

test('renders generated images from the saved disk path and links to that path', () => {
  const element = renderImageGeneration({
    result: 'ZmFrZS1iYXNlNjQ=',
    revisedPrompt: 'Make it cinematic',
    savedPath: '/home/zos/.codex/generated_images/thread/image-1.png',
    status: 'completed',
  });

  const image = element.querySelector('.image-generation__image');
  const link = element.querySelector('.image-generation__saved-path-link');

  assert.ok(image);
  assert.match(image.getAttribute('src'), /^file:\/\/\/home\/zos\/\.codex\/generated_images\/thread\/image-1\.png$/);
  assert.ok(link);
  assert.equal(link.getAttribute('href'), '/home/zos/.codex/generated_images/thread/image-1.png');
  assert.match(element.querySelector('.image-generation__prompt').textContent, /Make it cinematic/);
  assert.equal(element.querySelector('.image-generation__status').textContent, 'Completed');
});

test('falls back to an inline data url when no saved path is available', () => {
  const element = renderImageGeneration({
    result: 'ZmFrZS1iYXNlNjQ=',
    revisedPrompt: '',
    savedPath: '',
    status: '',
  });

  const image = element.querySelector('.image-generation__image');
  assert.ok(image);
  assert.equal(image.getAttribute('src'), 'data:image/png;base64,ZmFrZS1iYXNlNjQ=');
  assert.equal(element.querySelector('.image-generation__saved-path-link'), null);
});
