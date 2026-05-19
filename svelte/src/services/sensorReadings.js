import request from './api.js';

export const sensorReadings = {
  getByHorse: (horseId) =>
    request(`/horses/${horseId}/readings`),

  create: (horseId, reading) =>
    request(`/horses/${horseId}/readings`, { method: 'POST', body: JSON.stringify(reading) }),

  createBatch: (horseId, readings) =>
    request(`/horses/${horseId}/readings/batch`, { method: 'POST', body: JSON.stringify(readings) }),
};
