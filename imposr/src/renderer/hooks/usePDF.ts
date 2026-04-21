import { useState } from 'react';

/** Manages selected PDF path state. */
export function usePDF(): { pdfPath: string; setPdfPath: (path: string) => void } {
  const [pdfPath, setPdfPathState] = useState('');

  const setPdfPath = (path: string): void => {
    if (!path.trim()) {
      throw new Error('PDF path is required');
    }

    setPdfPathState(path);
  };

  return { pdfPath, setPdfPath };
}
