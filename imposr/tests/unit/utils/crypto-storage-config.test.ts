import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { getAppConfig } from '../../../src/utils/config';
import { generateSalt, signPayload, verifySignature } from '../../../src/utils/crypto';
import { createJsonStore } from '../../../src/utils/storage';

describe('config/crypto/storage utils', () => {
  it('parses config with defaults and validates values', () => {
    const cfg = getAppConfig({ API_AUTH_TOKEN: ' token ', IMPOSR_STORAGE_DIR: 'store' });
    expect(cfg.apiAuthToken).toBe('token');
    expect(cfg.storageDirectory).toBe('store');

    expect(() => getAppConfig({ API_RATE_LIMIT_WINDOW_MS: '0' })).toThrow(
      'API_RATE_LIMIT_WINDOW_MS must be a positive number'
    );
    expect(() => getAppConfig({ API_RATE_LIMIT_MAX_REQUESTS: '-1' })).toThrow(
      'API_RATE_LIMIT_MAX_REQUESTS must be a positive number'
    );
  });

  it('signs and verifies payloads', () => {
    const salt = generateSalt(8);
    expect(salt).toHaveLength(16);
    expect(() => generateSalt(0)).toThrow('byteLength must be a positive integer');

    const signature = signPayload('payload', 'secret');
    expect(verifySignature(signature, signature)).toBe(true);
    expect(verifySignature(signature, signPayload('other', 'secret'))).toBe(false);
    expect(verifySignature('', signature)).toBe(false);
    expect(() => signPayload('', 'secret')).toThrow('payload is required');
    expect(() => signPayload('payload', '')).toThrow('secret is required');
  });

  it('loads fallback and persists json store', async () => {
    const dir = await mkdtemp(join(tmpdir(), 'imposr-store-'));
    const store = createJsonStore(dir, 'state.json', { count: 0 });

    await expect(store.load()).resolves.toEqual({ count: 0 });
    await store.save({ count: 2 });
    await expect(store.load()).resolves.toEqual({ count: 2 });

    expect(() => createJsonStore(' ', 'x.json', { ok: true })).toThrow('directory and fileName are required');
  });
});
