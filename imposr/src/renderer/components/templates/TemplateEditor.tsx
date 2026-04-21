export interface TemplateEditorProps {
  readonly initialName: string;
  readonly onSave: (name: string) => void;
}

/** Simple template naming editor for beta customization tests. */
export function TemplateEditor({ initialName, onSave }: TemplateEditorProps): JSX.Element {
  const handleSave = (): void => {
    const name = initialName.trim();
    if (!name) {
      throw new Error('Template name is required');
    }

    onSave(name);
  };

  return (
    <div>
      <span>{initialName}</span>
      <button type="button" onClick={handleSave}>
        Opslaan
      </button>
    </div>
  );
}
