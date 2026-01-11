(() => {
  const getCookie = (name) => {
    const parts = document.cookie.split(';');
    for (const part of parts) {
      const trimmed = part.trim();
      if (!trimmed) continue;
      const eq = trimmed.indexOf('=');
      if (eq === -1) continue;
      const key = trimmed.slice(0, eq);
      const val = trimmed.slice(eq + 1);
      if (key === name) return decodeURIComponent(val);
    }
    return '';
  };

  const escapeHtml = (value) =>
    value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');

  const auth = document.querySelector('.nav-auth');
  if (!auth) return;

  const user = getCookie('ws_user');
  if (user) {
    auth.innerHTML =
      '<a class="btn ghost" href="/logout">&#x9000;&#x51fa;</a>' +
      '<span class="tag">&#x5df2;&#x767b;&#x5f55;&#xff1a' +
      escapeHtml(user) +
      '</span>';
  } else {
    auth.innerHTML =
      '<a class="btn ghost" href="/pages/log.html">&#x767b;&#x5f55;</a>' +
      '<a class="btn primary" href="/pages/register.html">&#x6ce8;&#x518c;</a>';
  }
})();
