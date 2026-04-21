/** Creates deterministic title for output files. */
export function buildOutputName(base: string): string {
  if (!base.trim()) {
    throw new Error('base is required');
  }

  return `${base}-imposed.pdf`;
}
