import type { ReactNode } from 'react';

export interface ModalProps {
  readonly open: boolean;
  readonly children: ReactNode;
}

/** Reusable modal container. */
export function Modal({ open, children }: ModalProps): JSX.Element | null {
  return open ? <div role="dialog">{children}</div> : null;
}
