/**
 * Minimal CLI logger abstraction.
 */
export class CliLogger {
  public info(message: string): string {
    return `[INFO] ${message}`;
  }

  public error(message: string): string {
    return `[ERROR] ${message}`;
  }
}
