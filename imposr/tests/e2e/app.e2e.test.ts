import request from 'supertest';
import { createApiServer } from '../../src/api/server';

describe('e2e: app', () => {
  it('serves core endpoints', async () => {
    const app = createApiServer();

    const templates = await request(app).get('/templates');
    const impose = await request(app).post('/impose').send({ pages: 2 });

    expect(templates.status).toBe(200);
    expect(impose.status).toBe(202);
  });
});
