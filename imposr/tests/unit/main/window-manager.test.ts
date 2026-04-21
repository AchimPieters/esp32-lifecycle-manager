import { WindowManager, type AppWindow } from '../../../src/main/window-manager';
import { ImposrError } from '../../../src/utils/errors';

function createWindow(): AppWindow {
  return {
    isDestroyed: jest.fn(() => false),
    show: jest.fn(),
    hide: jest.fn(),
    loadURL: jest.fn(async () => undefined)
  };
}

describe('WindowManager', () => {
  it('initializes and returns window', async () => {
    const window = createWindow();
    const manager = new WindowManager({
      createMainWindow: jest.fn(() => window)
    });

    const result = await manager.initialize('http://localhost:3000');

    expect(result).toBe(window);
    expect(window.loadURL).toHaveBeenCalled();
  });

  it('throws ImposrError when loadURL fails', async () => {
    const window = createWindow();
    (window.loadURL as jest.Mock).mockRejectedValueOnce(new Error('boom'));
    const manager = new WindowManager({
      createMainWindow: jest.fn(() => window)
    });

    await expect(manager.initialize('url')).rejects.toBeInstanceOf(ImposrError);
  });

  it('shows and hides initialized window', async () => {
    const window = createWindow();
    const manager = new WindowManager({ createMainWindow: jest.fn(() => window) });
    await manager.initialize('url');

    manager.show();
    manager.hide();

    expect(window.show).toHaveBeenCalled();
    expect(window.hide).toHaveBeenCalled();
  });
});
