export interface StatusBarProps {
  readonly message: string;
}

/** Status bar component. */
export function StatusBar({ message }: StatusBarProps): JSX.Element {
  return <footer>{message}</footer>;
}
