import { FeatureNotAvailableError } from '@utils/errors';

export type LicenseTier = 'trial' | 'pro' | 'enterprise';

const tierRank: Record<LicenseTier, number> = {
  trial: 1,
  pro: 2,
  enterprise: 3
};

/**
 * Authorizes feature access by license tier.
 */
export class FeatureGate {
  /**
   * Throws when current tier is below required tier.
   */
  public assertFeature(feature: string, currentTier: LicenseTier, requiredTier: LicenseTier): void {
    if (tierRank[currentTier] < tierRank[requiredTier]) {
      throw new FeatureNotAvailableError(feature, requiredTier);
    }
  }
}
