export interface LicenseState {
  readonly tier: string;
}

export const initialLicenseState: LicenseState = { tier: 'trial' };

/** Updates license tier. */
export function setLicenseTier(state: LicenseState, tier: string): LicenseState {
  if (!tier.trim()) {
    throw new Error('Tier is required');
  }

  return { ...state, tier };
}
