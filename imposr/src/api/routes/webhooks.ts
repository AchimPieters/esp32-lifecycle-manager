import { Router } from 'express';

/**
 * Creates webhook route module.
 */
export function createWebhookRouter(): Router {
  const router = Router();

  router.post('/license-updated', (req, res) => {
    res.status(202).json({ accepted: true, event: req.body?.event ?? 'unknown' });
  });

  return router;
}
