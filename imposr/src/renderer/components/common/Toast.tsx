export interface ToastProps {
  readonly message: string;
}

/** Toast notification component. */
export function Toast({ message }: ToastProps): JSX.Element {
  return <div role="status">{message}</div>;
}
