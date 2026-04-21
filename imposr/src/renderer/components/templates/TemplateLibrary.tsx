export interface TemplateLibraryProps {
  readonly templates: readonly string[];
}

/** Renders available template ids as beta library list. */
export function TemplateLibrary({ templates }: TemplateLibraryProps): JSX.Element {
  return (
    <ul>
      {templates.map((template) => (
        <li key={template}>{template}</li>
      ))}
    </ul>
  );
}
