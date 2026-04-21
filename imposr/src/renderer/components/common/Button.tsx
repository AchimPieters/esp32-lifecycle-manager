export interface ButtonProps {
  readonly label: string;
  readonly onClick: () => void;
}

/** Reusable button component. */
export function Button({ label, onClick }: ButtonProps): JSX.Element {
  return <button onClick={onClick}>{label}</button>;
}
