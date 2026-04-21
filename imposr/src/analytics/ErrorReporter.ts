/**
 * Captures errors to reporting sink.
 */
export class ErrorReporter {
  /**
   * Serializes error for transport.
   */
  public report(error: Error): { message: string } {
    if (!error.message.trim()) {
      throw new Error('Error message is required');
    }

    return { message: error.message };
  }
}
