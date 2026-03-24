const { app, BrowserWindow, ipcMain, Menu, shell } = require('electron');
const readline = require('node:readline');
const { URL, fileURLToPath } = require('node:url');
const path = require('path');

const windowIconPath = path.join(__dirname, 'assets', 'qodex-icon-256x256.png');

function readCommandLineValue(name) {
  const prefix = `--${name}=`;
  const matchingArgument = process.argv.find((argument) => argument.startsWith(prefix));
  return matchingArgument ? matchingArgument.slice(prefix.length) : null;
}

function hasCommandLineSwitch(name) {
  return process.argv.includes(`--${name}`);
}

const instanceInfo = {
  title: readCommandLineValue('qodex-title') || 'Qodex Thread UI',
  smokeTest: hasCommandLineSwitch('smoke-test'),
};

let mainWindow = null;
let pendingActivationRequest = false;
let smokeTestFinished = false;

function getWindowState(window) {
  return {
    isMaximized: window.isMaximized(),
  };
}

function sendWindowState(window) {
  if (window.isDestroyed()) {
    return;
  }

  window.webContents.send('window:state-changed', getWindowState(window));
}

const appEntryPath = path.resolve(__dirname, 'index.html');

function isAppEntryUrl(url) {
  const parsedUrl = new URL(url);

  if (parsedUrl.protocol !== 'file:') {
    return false;
  }

  return path.resolve(fileURLToPath(parsedUrl)) === appEntryPath;
}

async function openOutsideThreadUi(url) {
  const parsedUrl = new URL(url);

  if (parsedUrl.protocol === 'http:' || parsedUrl.protocol === 'https:') {
    await shell.openExternal(parsedUrl.toString());
    return;
  }

  if (parsedUrl.protocol === 'file:') {
    const error = await shell.openPath(fileURLToPath(parsedUrl));

    if (error) {
      console.error(`Failed to open external file URL ${url}: ${error}`);
    }
  }
}

function installExternalNavigationPolicy(window) {
  window.webContents.on('will-navigate', (event, navigationUrl) => {
    if (isAppEntryUrl(navigationUrl)) {
      return;
    }

    event.preventDefault();
    void openOutsideThreadUi(navigationUrl);
  });

  window.webContents.setWindowOpenHandler(({ url }) => {
    if (!isAppEntryUrl(url)) {
      void openOutsideThreadUi(url);
    }

    return { action: 'deny' };
  });
}

function focusMainWindow() {
  if (!mainWindow || mainWindow.isDestroyed()) {
    pendingActivationRequest = true;
    return;
  }

  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }

  mainWindow.show();
  app.focus();
  mainWindow.focus();
}

function installActivationControlChannel() {
  if (!process.stdin || process.stdin.isTTY) {
    return;
  }

  const commandReader = readline.createInterface({
    input: process.stdin,
    crlfDelay: Infinity,
  });

  commandReader.on('line', (line) => {
    if (line.trim() === 'activate') {
      focusMainWindow();
    }
  });

  process.stdin.resume();
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1000,
    height: 720,
    minWidth: 640,
    minHeight: 480,
    frame: false,
    autoHideMenuBar: true,
    icon: windowIconPath,
    show: !instanceInfo.smokeTest,
    title: instanceInfo.title,
    webPreferences: {
      contextIsolation: false,
      nodeIntegration: true,
      backgroundThrottling: false,
      sandbox: false,
    },
  });

  mainWindow.setMenuBarVisibility(false);
  mainWindow.removeMenu();
  installExternalNavigationPolicy(mainWindow);

  if (instanceInfo.smokeTest) {
    const failSmokeTest = (message) => {
      if (smokeTestFinished) {
        return;
      }

      smokeTestFinished = true;
      console.error(message);
      app.exit(1);
    };

    mainWindow.webContents.on('did-fail-load', (_event, errorCode, errorDescription) => {
      failSmokeTest(`Thread UI smoke test failed to load: ${errorCode} ${errorDescription}`);
    });

    mainWindow.webContents.on('render-process-gone', (_event, details) => {
      failSmokeTest(`Thread UI smoke test renderer exited unexpectedly: ${details.reason}`);
    });
  }

  mainWindow.on('maximize', () => {
    sendWindowState(mainWindow);
  });

  mainWindow.on('unmaximize', () => {
    sendWindowState(mainWindow);
  });

  mainWindow.loadFile(path.join(__dirname, 'index.html'));

  mainWindow.webContents.once('did-finish-load', () => {
    mainWindow.webContents.send('thread-ui:instance-info', instanceInfo);
    sendWindowState(mainWindow);
    if (pendingActivationRequest) {
      pendingActivationRequest = false;
      focusMainWindow();
    }
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

ipcMain.handle('window:get-state', (event) => {
  const window = BrowserWindow.fromWebContents(event.sender);
  return getWindowState(window);
});

ipcMain.handle('window:minimize', (event) => {
  const window = BrowserWindow.fromWebContents(event.sender);
  window.minimize();
});

ipcMain.handle('window:toggle-maximize', (event) => {
  const window = BrowserWindow.fromWebContents(event.sender);

  if (window.isMaximized()) {
    window.unmaximize();
  } else {
    window.maximize();
  }

  return getWindowState(window);
});

ipcMain.handle('window:close', (event) => {
  const window = BrowserWindow.fromWebContents(event.sender);
  window.close();
});

ipcMain.handle('thread-ui:get-instance-info', () => instanceInfo);

ipcMain.handle('thread-ui:notify-ready', () => {
  if (instanceInfo.smokeTest && !smokeTestFinished) {
    smokeTestFinished = true;
    setImmediate(() => {
      app.exit(0);
    });
  }
});

app.whenReady().then(() => {
  app.setName('Qodex');
  Menu.setApplicationMenu(null);
  installActivationControlChannel();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    } else {
      focusMainWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
