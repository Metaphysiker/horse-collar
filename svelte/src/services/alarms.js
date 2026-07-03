import request from './api.js';

export const alarms = {
  getActive: () =>
    request('/cronjobs/active-alarms'),

  clearAll: () =>
    request('/cronjobs/clear-all-alarms', {
      method: 'POST',
      body: JSON.stringify({})
    }),
};
