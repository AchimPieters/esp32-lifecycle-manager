import { createHash } from 'node:crypto';

/**
 * Generates deterministic machine fingerprints.
 */
export class MachineId {
  /**
   * Hashes platform attributes into a stable machine id.
   */
  public generate(seed: string): string {
    if (!seed.trim()) {
      throw new Error('Machine seed is required');
    }

    return createHash('sha256').update(seed).digest('hex');
  }
}
