export interface InputProps {
  readonly value: string;
  readonly onChange: (value: string) => void;
}

/** Reusable text input component. */
export function Input({ value, onChange }: InputProps): JSX.Element {
  return <input value={value} onChange={(event) => onChange(event.target.value)} />;
}
