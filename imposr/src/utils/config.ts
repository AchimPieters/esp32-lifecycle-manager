export interface AppConfig {
  readonly apiAuthToken: string | null;
  readonly rateLimitWindowMs: number;
  readonly rateLimitMaxRequests: number;
  readonly storageDirectory: string;
}

/**
 * Reads and validates application configuration from environment variables.
 */
export function getAppConfig(env: NodeJS.ProcessEnv = process.env): AppConfig {
  const windowMs = Number(env.API_RATE_LIMIT_WINDOW_MS ?? '60000');
  const maxRequests = Number(env.API_RATE_LIMIT_MAX_REQUESTS ?? '120');

  if (!Number.isFinite(windowMs) || windowMs <= 0) {
    throw new Error('API_RATE_LIMIT_WINDOW_MS must be a positive number');
  }

  if (!Number.isFinite(maxRequests) || maxRequests <= 0) {
    throw new Error('API_RATE_LIMIT_MAX_REQUESTS must be a positive number');
  }

  const token = env.API_AUTH_TOKEN?.trim() ?? '';
  return {
    apiAuthToken: token.length > 0 ? token : null,
    rateLimitWindowMs: windowMs,
    rateLimitMaxRequests: maxRequests,
    storageDirectory: env.IMPOSR_STORAGE_DIR?.trim() || '.imposr'
  };
}
