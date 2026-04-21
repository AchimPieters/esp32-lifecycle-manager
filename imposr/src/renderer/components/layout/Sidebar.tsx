export interface SidebarProps {
  readonly sections: string[];
}

/** Sidebar navigation component. */
export function Sidebar({ sections }: SidebarProps): JSX.Element {
  return (
    <aside>
      {sections.map((section) => (
        <div key={section}>{section}</div>
      ))}
    </aside>
  );
}
