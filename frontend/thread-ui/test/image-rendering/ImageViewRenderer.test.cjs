const assert = require('node:assert/strict');
const test = require('node:test');

const { JSDOM } = require('jsdom');

const { createImageViewRenderer } = require('../../image-rendering/ImageViewRenderer');

function renderImageView(imageView) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>');
  const renderer = createImageViewRenderer({ domWindow: dom.window });
  return renderer.renderToElement(imageView);
}

test('renders viewed images from disk path and links to that path', () => {
  const element = renderImageView({
    path: '/tmp/iota-camera-direct-w.png',
  });

  const image = element.querySelector('.image-view__image');
  const link = element.querySelector('.image-view__path-link');

  assert.ok(image);
  assert.equal(image.getAttribute('src'), 'file:///tmp/iota-camera-direct-w.png');
  assert.equal(image.getAttribute('alt'), 'Viewed image iota-camera-direct-w.png');
  assert.ok(link);
  assert.equal(link.getAttribute('href'), '/tmp/iota-camera-direct-w.png');
  assert.equal(link.textContent, '/tmp/iota-camera-direct-w.png');
});

test('shows a placeholder when no image path is available', () => {
  const element = renderImageView({
    path: '',
  });

  assert.equal(element.querySelector('.image-view__image'), null);
  assert.equal(element.querySelector('.image-view__path-link'), null);
  assert.equal(element.querySelector('.image-view__placeholder').textContent, '(image unavailable)');
});
