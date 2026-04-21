import type { NextFunction, Request, Response } from 'express';

/**
 * Enforces bearer-token authentication when a token is configured.
 */
export function createAuthMiddleware(configuredToken: string | null) {
  return (req: Request, res: Response, next: NextFunction): void => {
    if (!configuredToken) {
      next();
      return;
    }

    const header = req.header('authorization');
    if (!header?.startsWith('Bearer ')) {
      res.status(401).json({ error: 'Missing bearer token' });
      return;
    }

    const token = header.slice('Bearer '.length).trim();
    if (token !== configuredToken) {
      res.status(403).json({ error: 'Invalid bearer token' });
      return;
    }

    next();
  };
}
