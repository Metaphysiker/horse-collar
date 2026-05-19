// @ts-nocheck
import request from './api.js';

export const sensorReadings = {
  getByHorse: (horseId, date) =>
    request(`/horses/${horseId}/readings?date=${date}`),

  create: (horseId, reading) =>
    request(`/horses/${horseId}/readings`, { method: 'POST', body: JSON.stringify(reading) }),

  createBatch: (horseId, readings) =>
    request(`/horses/${horseId}/readings/batch`, { method: 'POST', body: JSON.stringify(readings) }),

  delete: (horseId, id) =>
    request(`/horses/${horseId}/readings/${id}`, { method: 'DELETE' }),
};
