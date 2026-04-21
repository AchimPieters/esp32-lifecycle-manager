import type { NextFunction, Request, Response } from 'express';

/**
 * Normalizes thrown errors into consistent JSON API responses.
 */
export function errorHandler(error: Error, _req: Request, res: Response, _next: NextFunction): void {
  const status = (error as { status?: number }).status ?? 400;
  res.status(status).json({ error: error.message });
}
