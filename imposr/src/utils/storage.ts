import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

export interface JsonStore<TData extends Record<string, unknown>> {
  readonly load: () => Promise<TData>;
  readonly save: (payload: TData) => Promise<void>;
}

/**
 * Creates a typed JSON file store with safe defaults.
 */
export function createJsonStore<TData extends Record<string, unknown>>(
  directory: string,
  fileName: string,
  fallback: TData
): JsonStore<TData> {
  if (!directory.trim() || !fileName.trim()) {
    throw new Error('directory and fileName are required');
  }

  const filePath = join(directory, fileName);

  return {
    load: async (): Promise<TData> => {
      try {
        const raw = await readFile(filePath, 'utf8');
        return JSON.parse(raw) as TData;
      } catch (error) {
        if ((error as NodeJS.ErrnoException).code === 'ENOENT') {
          return fallback;
        }

        throw new Error(`Unable to load ${filePath}: ${(error as Error).message}`);
      }
    },
    save: async (payload: TData): Promise<void> => {
      try {
        await mkdir(directory, { recursive: true });
        await writeFile(filePath, JSON.stringify(payload, null, 2), 'utf8');
      } catch (error) {
        throw new Error(`Unable to save ${filePath}: ${(error as Error).message}`);
      }
    }
  };
}
