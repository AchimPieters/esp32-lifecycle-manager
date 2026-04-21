import { createHmac, randomBytes, timingSafeEqual } from 'node:crypto';

/**
 * Creates a random hex salt for signing operations.
 */
export function generateSalt(byteLength = 16): string {
  if (!Number.isInteger(byteLength) || byteLength <= 0) {
    throw new Error('byteLength must be a positive integer');
  }

  return randomBytes(byteLength).toString('hex');
}

/**
 * Generates deterministic HMAC SHA-256 signature for a payload.
 */
export function signPayload(payload: string, secret: string): string {
  if (!payload) {
    throw new Error('payload is required');
  }
  if (!secret) {
    throw new Error('secret is required');
  }

  return createHmac('sha256', secret).update(payload).digest('hex');
}

/**
 * Compares signatures using timing-safe verification.
 */
export function verifySignature(signature: string, expectedSignature: string): boolean {
  if (!signature || !expectedSignature) {
    return false;
  }

  const actual = Buffer.from(signature, 'hex');
  const expected = Buffer.from(expectedSignature, 'hex');

  if (actual.length !== expected.length) {
    return false;
  }

  return timingSafeEqual(actual, expected);
}
