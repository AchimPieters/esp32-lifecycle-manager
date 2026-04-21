export interface PackagingInput {
  readonly pluginId: string;
  readonly version: string;
  readonly target: 'win' | 'mac';
}

/**
 * Creates deterministic plugin package filenames.
 */
export function buildPluginPackageName(input: PackagingInput): string {
  if (!input.pluginId.trim() || !input.version.trim()) {
    throw new Error('pluginId and version are required');
  }

  return `${input.pluginId}-${input.version}-${input.target}.zip`;
}
