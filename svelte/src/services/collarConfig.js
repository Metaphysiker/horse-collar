// @ts-nocheck
import request from './api.js';

export const collarConfig = {
  get: (horseId) =>
    request(`/horses/${horseId}/config`),

  update: (horseId, config) =>
    request(`/horses/${horseId}/config`, { method: 'PUT', body: JSON.stringify(config) }),

  triggerRecalibrate: (horseId) =>
    request(`/horses/${horseId}/config/recalibrate`, { method: 'POST' }),

  triggerReboot: (horseId) =>
    request(`/horses/${horseId}/config/reboot`, { method: 'POST' }),
};
