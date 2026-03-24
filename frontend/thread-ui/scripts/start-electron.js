#!/usr/bin/env node

const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const electronBinary = require('electron');

function needsNoSandbox() {
  if (process.platform !== 'linux') {
    return false;
  }

  const chromeSandboxPath = path.join(path.dirname(electronBinary), 'chrome-sandbox');

  try {
    const stat = fs.statSync(chromeSandboxPath);
    return stat.uid !== 0 || (stat.mode & 0o4777) !== 0o4755;
  } catch {
    return true;
  }
}

const args = [];

if (needsNoSandbox()) {
  args.push('--no-sandbox');
}

args.push(path.resolve(__dirname, '..'));
args.push(...process.argv.slice(2));

const child = spawn(electronBinary, args, {
  stdio: 'inherit',
});

child.on('exit', (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }

  process.exit(code ?? 0);
});
