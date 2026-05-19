import request from './api.js';

export const horses = {
  getAll: () =>
    request('/horses'),

  getById: (id) =>
    request(`/horses/${id}`),

  create: (horse) =>
    request('/horses', { method: 'POST', body: JSON.stringify(horse) }),

  delete: (id) =>
    request(`/horses/${id}`, { method: 'DELETE' }),
};
