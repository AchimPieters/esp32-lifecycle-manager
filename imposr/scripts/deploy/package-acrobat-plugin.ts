import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

export interface PackagingInput {
  readonly pluginId: string;
  readonly version: string;
  readonly target: 'win' | 'mac';
}

export interface ManifestInput extends PackagingInput {
  readonly minAcrobatVersion: string;
  readonly displayName: string;
  readonly bridgeEntry: string;
}

export interface PluginManifest {
  readonly id: string;
  readonly name: string;
  readonly version: string;
  readonly target: 'win' | 'mac';
  readonly minAcrobatVersion: string;
  readonly bridgeEntry: string;
  readonly generatedAt: string;
}

/**
 * Creates deterministic plugin package filenames.
 */
export function buildPluginPackageName(input: PackagingInput): string {
  const pluginId = input.pluginId.trim();
  const version = input.version.trim();

  if (!pluginId || !version) {
    throw new Error('pluginId and version are required');
  }

  return `${pluginId}-${normalizeVersion(version)}-${input.target}.zip`;
}

/**
 * Builds an Acrobat plugin manifest payload used during beta packaging.
 */
export function buildPluginManifest(input: ManifestInput, generatedAt = new Date().toISOString()): PluginManifest {
  if (!input.displayName.trim()) {
    throw new Error('displayName is required');
  }

  if (!input.minAcrobatVersion.trim() || !input.bridgeEntry.trim()) {
    throw new Error('minAcrobatVersion and bridgeEntry are required');
  }

  return {
    id: input.pluginId.trim(),
    name: input.displayName.trim(),
    version: normalizeVersion(input.version),
    target: input.target,
    minAcrobatVersion: input.minAcrobatVersion.trim(),
    bridgeEntry: input.bridgeEntry.trim(),
    generatedAt
  };
}

/**
 * Stages a packaging directory containing plugin manifest and file index.
 */
export async function stagePluginPackage(
  outputRoot: string,
  manifest: PluginManifest,
  payloadFiles: readonly string[]
): Promise<string> {
  if (!outputRoot.trim()) {
    throw new Error('outputRoot is required');
  }

  const packageDir = join(outputRoot, `${manifest.id}-${manifest.version}-${manifest.target}`);

  await mkdir(packageDir, { recursive: true });
  await writeFile(join(packageDir, 'manifest.json'), JSON.stringify(manifest, null, 2), 'utf8');
  await writeFile(join(packageDir, 'payload-files.json'), JSON.stringify([...payloadFiles], null, 2), 'utf8');

  return packageDir;
}

function normalizeVersion(version: string): string {
  const trimmed = version.trim();
  if (!/^\d+\.\d+\.\d+(-[a-z0-9.-]+)?$/i.test(trimmed)) {
    throw new Error('version must follow semver format (e.g. 1.2.3 or 1.2.3-beta.1)');
  }

  return trimmed;
}
