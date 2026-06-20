// @ts-nocheck
import request from './api.js';

export const sensorReadingsV2 = {
  getByHorse: (horseId, date, limit = null) => {
    const params = new URLSearchParams({ date });
    if (limit != null) params.set('limit', limit);
    return request(`/v2/horses/${horseId}/readings?${params}`);
  },

  create: (horseId, reading) =>
    request(`/v2/horses/${horseId}/readings`, { method: 'POST', body: JSON.stringify(reading) }),

  createBatch: (horseId, readings) =>
    request(`/v2/horses/${horseId}/readings/batch`, { method: 'POST', body: JSON.stringify(readings) }),

  delete: (horseId, id) =>
    request(`/v2/horses/${horseId}/readings/${id}`, { method: 'DELETE' }),
};
