export interface UpgradePromptProps {
  readonly currentTier: string;
  readonly targetTier: string;
  readonly onUpgrade: () => Promise<void>;
}

/**
 * Displays a commercial upsell message and starts the upgrade checkout flow.
 */
export function UpgradePrompt({ currentTier, targetTier, onUpgrade }: UpgradePromptProps): JSX.Element {
  const handleUpgradeClick = async (): Promise<void> => {
    try {
      await onUpgrade();
    } catch (error) {
      // Component keeps rendering state stable while host app handles toast/reporting.
      // eslint-disable-next-line no-console
      console.error('Upgrade flow failed', error);
    }
  };

  return (
    <section aria-label="upgrade-prompt">
      <h2>Upgrade je abonnement</h2>
      <p>
        Je gebruikt nu <strong>{currentTier}</strong> en kunt upgraden naar <strong>{targetTier}</strong>.
      </p>
      <button type="button" onClick={() => void handleUpgradeClick()}>
        Upgrade nu
      </button>
    </section>
  );
}
