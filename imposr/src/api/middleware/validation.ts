import type { NextFunction, Request, Response } from 'express';
import type { ZodType } from 'zod';

/**
 * Validates request body against a Zod schema.
 */
export function validateBody<T>(schema: ZodType<T>) {
  return (req: Request, res: Response, next: NextFunction): void => {
    const result = schema.safeParse(req.body);
    if (!result.success) {
      res.status(400).json({ error: 'Validation failed', issues: result.error.issues });
      return;
    }

    req.body = result.data;
    next();
  };
}
