export interface TemplateSelectorProps {
  readonly templateIds: readonly string[];
  readonly selectedTemplate: string;
  readonly onSelect: (templateId: string) => void;
}

/** Dropdown selector for imposition template choices. */
export function TemplateSelector({ templateIds, selectedTemplate, onSelect }: TemplateSelectorProps): JSX.Element {
  if (templateIds.length === 0) {
    throw new Error('templateIds cannot be empty');
  }

  return (
    <select value={selectedTemplate} onChange={(event) => onSelect(event.target.value)}>
      {templateIds.map((templateId) => (
        <option key={templateId} value={templateId}>
          {templateId}
        </option>
      ))}
    </select>
  );
}
