const baseUrl = import.meta.env.VITE_API_URL;

async function request(path, options = {}) {
  const response = await fetch(`${baseUrl}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });

  if (!response.ok) throw new Error(`${options.method ?? 'GET'} ${path} failed: ${response.status}`);
  if (response.status === 204) return null;

  return response.json();
}

export default request;
