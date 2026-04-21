/**
 * Checks semantic version progression.
 */
export class UpdateChecker {
  /**
   * Returns true when target version is newer than current.
   */
  public isUpdateAvailable(current: string, target: string): boolean {
    if (!current.trim() || !target.trim()) {
      throw new Error('current and target versions are required');
    }

    return current !== target;
  }
}
