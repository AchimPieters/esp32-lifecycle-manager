import type { NextFunction, Request, Response } from 'express';

interface Bucket {
  count: number;
  resetAt: number;
}

/**
 * Creates lightweight in-memory rate limiting middleware.
 */
export function createRateLimitMiddleware(windowMs: number, maxRequests: number) {
  if (windowMs <= 0 || maxRequests <= 0) {
    throw new Error('windowMs and maxRequests must be positive');
  }

  const buckets = new Map<string, Bucket>();

  return (req: Request, res: Response, next: NextFunction): void => {
    const key = req.ip || req.socket.remoteAddress || 'unknown';
    const now = Date.now();
    const current = buckets.get(key);

    if (!current || now >= current.resetAt) {
      buckets.set(key, { count: 1, resetAt: now + windowMs });
      next();
      return;
    }

    if (current.count >= maxRequests) {
      res.status(429).json({ error: 'Rate limit exceeded', retryAfterMs: current.resetAt - now });
      return;
    }

    current.count += 1;
    next();
  };
}
