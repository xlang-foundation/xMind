'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const css = fs.readFileSync(path.resolve('views/browser/browser.css'), 'utf8');

test('narrow browser layout overrides later workspace and sidebar defaults', () => {
  const mobileOverride = css.lastIndexOf('@media(max-width:760px){.browser-layout>.runtime-pane{display:none}#agent-sidebar{width:100%;min-width:0;max-width:none;border-left:0}#sidebar-divider{display:none}}');
  assert.notEqual(mobileOverride, -1, 'final narrow-window override exists');
  assert.ok(mobileOverride > css.lastIndexOf('.runtime-pane{display:flex'), 'override follows the pane default');
  assert.ok(mobileOverride > css.lastIndexOf('#agent-sidebar{width:var(--xmind-sidebar-width'), 'override follows sidebar width defaults');
});

test('filename search button overrides the full-width file-tree button rule', () => {
  const treeButton = css.lastIndexOf('.file-tree button{width:100%');
  const searchButton = css.lastIndexOf('.file-tree .workspace-find button{width:auto;');
  assert.notEqual(treeButton, -1, 'file-tree buttons use the full-width default');
  assert.notEqual(searchButton, -1, 'filename search has a compact button override');
  assert.ok(searchButton > treeButton, 'the search button override follows the file-tree default');
});
