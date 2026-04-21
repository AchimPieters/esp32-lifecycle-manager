export interface PDFRendererProps {
  readonly activePage: number;
}

/** Renders active page content placeholder for beta preview loop. */
export function PDFRenderer({ activePage }: PDFRendererProps): JSX.Element {
  if (activePage <= 0) {
    throw new Error('activePage must be greater than zero');
  }

  return <div aria-label="pdf-renderer">Rendering pagina {activePage}</div>;
}
