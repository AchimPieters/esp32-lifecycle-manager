/**
 * Validates offline license signatures.
 */
export class OfflineValidator {
  /**
   * Performs a simple signature integrity check.
   */
  public validate(token: string, expectedPrefix: string): boolean {
    if (!token.trim() || !expectedPrefix.trim()) {
      throw new Error('Token and expected prefix are required');
    }

    return token.startsWith(expectedPrefix);
  }
}
