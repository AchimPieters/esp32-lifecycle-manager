import { bootstrapMainProcess } from '../../../src/main';

describe('bootstrapMainProcess', () => {
  it('bootstraps without errors', async () => {
    const window = {
      isDestroyed: jest.fn(() => false),
      show: jest.fn(),
      hide: jest.fn(),
      loadURL: jest.fn(async () => undefined)
    };

    const register = jest.fn();

    await expect(
      bootstrapMainProcess({
        windowFactory: { createMainWindow: () => window },
        ipcRegistry: { register },
        startUrl: 'http://localhost:3000'
      })
    ).resolves.toBeUndefined();

    expect(window.show).toHaveBeenCalled();
    expect(register).toHaveBeenCalledTimes(2);
  });
});
