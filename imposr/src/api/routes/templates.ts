import { Router } from 'express';
import { TemplateController } from '../controllers/TemplateController';

/**
 * Creates template route module.
 */
export function createTemplateRouter(controller = new TemplateController()): Router {
  const router = Router();

  router.get('/', async (_req, res, next) => {
    try {
      const items = await controller.list();
      res.json({ items });
    } catch (error) {
      next(error);
    }
  });

  return router;
}
