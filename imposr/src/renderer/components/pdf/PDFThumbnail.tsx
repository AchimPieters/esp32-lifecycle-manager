export interface PDFThumbnailProps {
  readonly pageNumber: number;
  readonly selected?: boolean;
}

/** Displays a compact selectable thumbnail tile for a single page. */
export function PDFThumbnail({ pageNumber, selected = false }: PDFThumbnailProps): JSX.Element {
  if (pageNumber <= 0) {
    throw new Error('pageNumber must be greater than zero');
  }

  return <button aria-pressed={selected}>Pagina {pageNumber}</button>;
}
