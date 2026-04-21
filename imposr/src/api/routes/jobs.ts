import { Router } from 'express';
import { JobController } from '../controllers/JobController';

/**
 * Creates job status route module.
 */
export function createJobsRouter(controller = new JobController()): Router {
  const router = Router();

  router.get('/:id', async (req, res, next) => {
    try {
      const result = await controller.getStatus(req.params.id);
      res.json(result);
    } catch (error) {
      next(error);
    }
  });

  return router;
}
