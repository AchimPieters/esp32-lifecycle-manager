export interface TemplatePreviewProps {
  readonly templateId: string;
  readonly description: string;
}

/** Displays selected template summary for operator confirmation. */
export function TemplatePreview({ templateId, description }: TemplatePreviewProps): JSX.Element {
  return (
    <section aria-label="template-preview">
      <h3>{templateId}</h3>
      <p>{description}</p>
    </section>
  );
}
