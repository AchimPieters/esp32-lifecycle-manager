export interface PdfState {
  readonly path: string;
}

export const initialPdfState: PdfState = { path: '' };

/** Updates PDF state path. */
export function setPdfPath(state: PdfState, path: string): PdfState {
  if (!path.trim()) {
    throw new Error('PDF path is required');
  }

  return { ...state, path };
}
