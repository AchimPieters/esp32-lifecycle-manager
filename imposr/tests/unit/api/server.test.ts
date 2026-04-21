import request from 'supertest';
import { createApiServer } from '../../../src/api/server';

describe('api server', () => {
  it('serves impose route', async () => {
    const app = createApiServer();
    const response = await request(app).post('/impose').send({ pages: 4 });

    expect(response.status).toBe(202);
    expect(response.body).toEqual({ status: 'queued', pages: 4 });
  });

  it('returns validation error through middleware', async () => {
    const app = createApiServer();
    const response = await request(app).post('/impose').send({ pages: 0 });

    expect(response.status).toBe(400);
    expect(response.body.error).toBe('Validation failed');
    expect(response.body.issues[0].path).toContain('pages');
  });

  it('serves templates, jobs and webhooks routes', async () => {
    const app = createApiServer();

    const templates = await request(app).get('/templates');
    const job = await request(app).get('/jobs/job-1');
    const webhook = await request(app).post('/webhooks/license-updated').send({ event: 'renewal' });

    expect(templates.status).toBe(200);
    expect(templates.body.items).toContain('2up-a4-a3');
    expect(job.status).toBe(200);
    expect(job.body).toEqual({ id: 'job-1', status: 'processing' });
    expect(webhook.status).toBe(202);
    expect(webhook.body.accepted).toBe(true);
  });
});
