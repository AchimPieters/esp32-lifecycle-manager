/** Formats bytes to human-readable KB string. */
export function formatKilobytes(bytes: number): string {
  if (bytes < 0) {
    throw new Error('bytes cannot be negative');
  }

  return `${(bytes / 1024).toFixed(2)} KB`;
}
