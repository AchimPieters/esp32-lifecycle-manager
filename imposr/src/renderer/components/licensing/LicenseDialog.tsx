import { useState } from 'react';
import { Modal } from '../common/Modal';
import { ActivationForm } from './ActivationForm';
import { UpgradePrompt } from './UpgradePrompt';

export interface LicenseDialogProps {
  readonly open: boolean;
  readonly currentTier: string;
  readonly onActivate: (licenseKey: string) => Promise<void>;
  readonly onUpgrade: () => Promise<void>;
  readonly onClose: () => void;
}

/**
 * Unified licensing dialog for activation and plan upgrades.
 */
export function LicenseDialog({
  open,
  currentTier,
  onActivate,
  onUpgrade,
  onClose
}: LicenseDialogProps): JSX.Element | null {
  const [loading, setLoading] = useState(false);

  const handleActivate = async (licenseKey: string): Promise<void> => {
    try {
      setLoading(true);
      await onActivate(licenseKey);
      onClose();
    } finally {
      setLoading(false);
    }
  };

  return (
    <Modal open={open}>
      <article>
        <header>
          <h1>Licentiebeheer</h1>
          <button type="button" onClick={onClose} aria-label="Sluit licentievenster">
            Sluiten
          </button>
        </header>
        <p>Huidige licentie: {currentTier}</p>
        <ActivationForm loading={loading} onActivate={handleActivate} />
        {currentTier === 'enterprise' ? null : (
          <UpgradePrompt currentTier={currentTier} targetTier="enterprise" onUpgrade={onUpgrade} />
        )}
      </article>
    </Modal>
  );
}
