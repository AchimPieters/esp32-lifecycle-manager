import { useState } from 'react';

/** Manages license tier state. */
export function useLicense(): { tier: string; setTier: (tier: string) => void } {
  const [tier, setTierState] = useState('trial');

  const setTier = (value: string): void => {
    if (!value.trim()) {
      throw new Error('License tier is required');
    }

    setTierState(value);
  };

  return { tier, setTier };
}
