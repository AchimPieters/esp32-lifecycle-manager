import { useState } from 'react';

export interface ActivationFormProps {
  readonly loading?: boolean;
  readonly onActivate: (licenseKey: string) => Promise<void>;
}

/**
 * Controlled activation form that validates license input before submitting.
 */
export function ActivationForm({ loading = false, onActivate }: ActivationFormProps): JSX.Element {
  const [licenseKey, setLicenseKey] = useState('');
  const [error, setError] = useState<string | null>(null);

  const handleSubmit = async (event: React.FormEvent<HTMLFormElement>): Promise<void> => {
    event.preventDefault();

    const trimmedKey = licenseKey.trim();
    if (!trimmedKey) {
      setError('Licentiesleutel is verplicht.');
      return;
    }

    try {
      setError(null);
      await onActivate(trimmedKey);
      setLicenseKey('');
    } catch (submitError) {
      const message = submitError instanceof Error ? submitError.message : 'Activatie mislukt.';
      setError(message);
    }
  };

  return (
    <form onSubmit={handleSubmit} aria-label="license-activation-form">
      <label htmlFor="license-key">Licentiesleutel</label>
      <input
        id="license-key"
        name="license-key"
        value={licenseKey}
        onChange={(event) => setLicenseKey(event.target.value)}
        disabled={loading}
      />
      <button type="submit" disabled={loading}>
        {loading ? 'Activeren…' : 'Activeer licentie'}
      </button>
      {error ? <p role="alert">{error}</p> : null}
    </form>
  );
}
