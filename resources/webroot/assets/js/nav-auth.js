/**
 * nav-auth.js - 导航栏登录状态管理脚本
 * 
 * 功能:
 * - 检测Cookie中的用户登录状态
 * - 动态更新导航栏的登录/注册按钮
 * - 已登录: 显示用户名+退出按钮
 * - 未登录: 显示登录+注册按钮
 * 
 * Cookie说明:
 * - ws_user: 登录成功后由服务器设置的用户名
 * 
 * 使用方式:
 * - 在HTML底部引入: <script src="/assets/js/nav-auth.js"></script>
 * - 要求页面包含 class="nav-auth" 的元素
 */
(() => {
  /**
   * 获取指定名称的Cookie值
   * @param {string} name - Cookie名称
   * @returns {string} Cookie值，不存在则返回空字符串
   * 
   * Cookie格式: "name1=value1; name2=value2; ..."
   */
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

  /**
   * HTML实体转义，防止XSS攻击
   * @param {string} value - 原始字符串
   * @returns {string} 转义后的安全字符串
   * 
   * 转义规则:
   * - & -> &amp;
   * - < -> &lt;
   * - > -> &gt;
   * - " -> &quot;
   */
  const escapeHtml = (value) =>
    value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');

  // 获取导航栏认证区域元素
  const auth = document.querySelector('.nav-auth');
  if (!auth) return;  // 元素不存在则不处理

  // 读取登录Cookie
  const user = getCookie('ws_user');
  
  if (user) {
    // 已登录: 显示退出按钮和用户名
    auth.innerHTML =
      '<a class="btn ghost" href="/logout">&#x9000;&#x51fa;</a>' +
      '<span class="tag">&#x5df2;&#x767b;&#x5f55;&#xff1a' +
      escapeHtml(user) +
      '</span>';
  } else {
    // 未登录: 显示登录和注册按钮
    auth.innerHTML =
      '<a class="btn ghost" href="/pages/log.html">&#x767b;&#x5f55;</a>' +
      '<a class="btn primary" href="/pages/register.html">&#x6ce8;&#x518c;</a>';
  }
})();
