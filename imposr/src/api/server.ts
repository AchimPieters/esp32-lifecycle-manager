import express, { type Express } from 'express';
import { createImposeRouter } from './routes/impose';
import { createJobsRouter } from './routes/jobs';
import { createTemplateRouter } from './routes/templates';
import { createWebhookRouter } from './routes/webhooks';

/**
 * Builds configured API server instance.
 */
export function createApiServer(): Express {
  const app = express();
  app.use(express.json());

  app.use('/impose', createImposeRouter());
  app.use('/templates', createTemplateRouter());
  app.use('/jobs', createJobsRouter());
  app.use('/webhooks', createWebhookRouter());

  app.use((error: Error, _req: express.Request, res: express.Response, _next: express.NextFunction) => {
    res.status(400).json({ error: error.message });
  });

  return app;
}
