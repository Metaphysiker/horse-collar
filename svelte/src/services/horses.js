// @ts-nocheck
import request from './api.js';

export const horses = {
  getAll: () =>
    request('/horses'),

  getArchived: () =>
    request('/horses/archived'),

  getById: (id) =>
    request(`/horses/${id}`),

  create: (horse) =>
    request('/horses', { method: 'POST', body: JSON.stringify(horse) }),

  archive: (id) =>
    request(`/horses/${id}/archive`, { method: 'POST' }),

  unarchive: (id) =>
    request(`/horses/${id}/unarchive`, { method: 'POST' }),
};
