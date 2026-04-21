/**
 * Normalized payment action abstraction.
 */
export class PaymentHandler {
  /**
   * Simulates checkout creation and returns payment reference.
   */
  public async createCheckout(customerId: string, planId: string): Promise<{ checkoutId: string }> {
    if (!customerId.trim() || !planId.trim()) {
      throw new Error('customerId and planId are required');
    }

    return { checkoutId: `${customerId}-${planId}-checkout` };
  }
}
