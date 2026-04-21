/** Validates license tier during state transitions. */
export function licenseMiddleware(tier: string): string {
  if (!tier.trim()) {
    throw new Error('Tier is required');
  }

  return tier;
}
