/** Validates whether path has .pdf extension. */
export function isPdfPath(path: string): boolean {
  return path.toLowerCase().endsWith('.pdf');
}
