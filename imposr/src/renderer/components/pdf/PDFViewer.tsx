export interface PDFViewerProps {
  readonly title: string;
  readonly pageCount: number;
}

/** Renders high-level PDF document metadata in the beta UI. */
export function PDFViewer({ title, pageCount }: PDFViewerProps): JSX.Element {
  if (!title.trim()) {
    throw new Error('title is required');
  }

  return (
    <section aria-label="pdf-viewer">
      <h2>{title}</h2>
      <p>Paginas: {pageCount}</p>
    </section>
  );
}
