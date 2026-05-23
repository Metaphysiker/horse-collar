// @ts-nocheck
import request from './api.js';

export const collarConfig = {
  get: (horseId) =>
    request(`/horses/${horseId}/config`),

  update: (horseId, config) =>
    request(`/horses/${horseId}/config`, { method: 'PUT', body: JSON.stringify(config) }),

  triggerReboot: (horseId) =>
    request(`/horses/${horseId}/config/reboot`, { method: 'POST' }),

  testNotification: (horseId) =>
    request(`/horses/${horseId}/config/test-notification`, { method: 'POST' }),
};
