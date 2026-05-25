// @ts-nocheck
import request from './api.js';

export const sensorReadings = {
  getByHorse: (horseId, date, limit = null) => {
    const params = new URLSearchParams({ date });
    if (limit != null) params.set('limit', limit);
    return request(`/horses/${horseId}/readings?${params}`);
  },

  create: (horseId, reading) =>
    request(`/horses/${horseId}/readings`, { method: 'POST', body: JSON.stringify(reading) }),

  createBatch: (horseId, readings) =>
    request(`/horses/${horseId}/readings/batch`, { method: 'POST', body: JSON.stringify(readings) }),

  delete: (horseId, id) =>
    request(`/horses/${horseId}/readings/${id}`, { method: 'DELETE' }),
};
