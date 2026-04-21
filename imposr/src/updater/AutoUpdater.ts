import { UpdateChecker } from './UpdateChecker';
import { UpdateDownloader } from './UpdateDownloader';

/**
 * Orchestrates checking and downloading updates.
 */
export class AutoUpdater {
  constructor(
    private readonly checker = new UpdateChecker(),
    private readonly downloader = new UpdateDownloader()
  ) {}

  /**
   * Performs update flow when a newer version is available.
   */
  public async run(current: string, target: string): Promise<{ updated: boolean; artifact?: string }> {
    if (!this.checker.isUpdateAvailable(current, target)) {
      return { updated: false };
    }

    const artifact = await this.downloader.download(target);
    return { updated: true, artifact };
  }
}
