import { Router } from 'express';
import { ImposeController } from '../controllers/ImposeController';

/**
 * Creates imposition route module.
 */
export function createImposeRouter(controller = new ImposeController()): Router {
  const router = Router();

  router.post('/', async (req, res, next) => {
    try {
      const result = await controller.impose(req.body as { pages: number });
      res.status(202).json(result);
    } catch (error) {
      next(error);
    }
  });

  return router;
}
