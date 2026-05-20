// @ts-nocheck
import request from './api.js';

export const deviceStatus = {
  getLatest: (horseId) => request(`/horses/${horseId}/status/latest`),
};
