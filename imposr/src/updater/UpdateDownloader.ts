/**
 * Handles update artifact fetching.
 */
export class UpdateDownloader {
  /**
   * Simulates download and returns local artifact path.
   */
  public async download(version: string): Promise<string> {
    if (!version.trim()) {
      throw new Error('version is required');
    }

    return `/tmp/imposr-update-${version}.zip`;
  }
}
