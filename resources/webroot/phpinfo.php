<?php
$time = date('Y-m-d H:i:s');
$version = phpversion();
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="icon" href="/assets/media/favicon.ico">
  <link rel="stylesheet" href="/assets/css/site.css">
  <title>WebServer | PHP</title>
</head>
<body>
<div class="page">
  <div class="nav">
    <div class="brand">WebServer &#x5b9e;&#x9a8c;&#x7ad9;</div>
    <div class="nav-links">
      <a href="/">&#x9996;&#x9875;</a>
      <a href="/uploads/list">&#x6211;&#x7684;&#x4e0a;&#x4f20;</a>
      <a href="/pages/status.html">&#x76d1;&#x63a7;</a>
    </div>
    <div class="nav-auth">
      <a class="btn ghost" href="/pages/log.html">&#x767b;&#x5f55;</a>
      <a class="btn primary" href="/pages/register.html">&#x6ce8;&#x518c;</a>
    </div>
  </div>

  <section class="panel" style="max-width: 860px; margin: 0 auto;">
    <h2 style="font-size: 26px;">PHP &#x8fd0;&#x884c;&#x4fe1;&#x606f;</h2>
    <p style="color: var(--muted); margin-top: 10px;">&#x8fd9;&#x91cc;&#x5c55;&#x793a;&#x7cbe;&#x7b80;&#x7684; PHP &#x8fd0;&#x884c;&#x6458;&#x8981;&#x3002;</p>
    <div class="grid" style="margin-top: 18px;">
      <div class="card"><h3>&#x5f53;&#x524d;&#x65f6;&#x95f4;</h3><p><?php echo htmlspecialchars($time, ENT_QUOTES, 'UTF-8'); ?></p></div>
      <div class="card"><h3>PHP &#x7248;&#x672c;</h3><p><?php echo htmlspecialchars($version, ENT_QUOTES, 'UTF-8'); ?></p></div>
    </div>
  </section>
</div>
<script src="/assets/js/nav-auth.js"></script>
</body>
</html>