export interface BatchSettingsProps {
  readonly concurrency: number;
  readonly onConcurrencyChange: (value: number) => void;
}

/** Allows operators to tweak batch worker concurrency in beta. */
export function BatchSettings({ concurrency, onConcurrencyChange }: BatchSettingsProps): JSX.Element {
  if (concurrency <= 0) {
    throw new Error('concurrency must be greater than zero');
  }

  return (
    <label>
      Concurrency
      <input
        type="number"
        min={1}
        value={concurrency}
        onChange={(event) => onConcurrencyChange(Number(event.target.value))}
      />
    </label>
  );
}
