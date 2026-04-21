import { AutoUpdater } from '../../../src/updater/AutoUpdater';
import { UpdateChecker } from '../../../src/updater/UpdateChecker';
import { UpdateDownloader } from '../../../src/updater/UpdateDownloader';

describe('updater modules', () => {
  it('checks update availability', () => {
    const checker = new UpdateChecker();

    expect(checker.isUpdateAvailable('1.0.0', '1.1.0')).toBe(true);
    expect(checker.isUpdateAvailable('1.0.0', '1.0.0')).toBe(false);
    expect(() => checker.isUpdateAvailable('', '1.0.0')).toThrow();
  });

  it('downloads update artifact', async () => {
    const downloader = new UpdateDownloader();

    await expect(downloader.download('1.2.0')).resolves.toContain('1.2.0');
    await expect(downloader.download('')).rejects.toThrow();
  });

  it('runs auto-updater flow', async () => {
    const updater = new AutoUpdater();

    await expect(updater.run('1.0.0', '1.0.0')).resolves.toEqual({ updated: false });
    await expect(updater.run('1.0.0', '1.1.0')).resolves.toMatchObject({ updated: true });
  });
});
