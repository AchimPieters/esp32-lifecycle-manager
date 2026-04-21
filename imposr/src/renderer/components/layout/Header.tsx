export interface HeaderProps {
  readonly title: string;
}

/** Header component. */
export function Header({ title }: HeaderProps): JSX.Element {
  return <header>{title}</header>;
}
