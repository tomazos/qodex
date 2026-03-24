const { app, BrowserWindow, ipcMain, Menu, shell } = require('electron');
const { URL, fileURLToPath } = require('node:url');
const path = require('path');

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

function createWindow() {
  const mainWindow = new BrowserWindow({
    width: 1000,
    height: 720,
    minWidth: 640,
    minHeight: 480,
    frame: false,
    autoHideMenuBar: true,
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

  mainWindow.on('maximize', () => {
    sendWindowState(mainWindow);
  });

  mainWindow.on('unmaximize', () => {
    sendWindowState(mainWindow);
  });

  mainWindow.loadFile(path.join(__dirname, 'index.html'));

  mainWindow.webContents.once('did-finish-load', () => {
    sendWindowState(mainWindow);
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

app.whenReady().then(() => {
  Menu.setApplicationMenu(null);
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
