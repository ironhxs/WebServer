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
    <div class="brand">WebServer ???</div>
    <div class="nav-links">
      <a href="/">??</a>
      <a href="/uploads/list">????</a>
      <a href="/pages/status.html">??</a>
    </div>
    <div class="nav-auth">
      <a class="btn ghost" href="/pages/log.html">??</a>
      <a class="btn primary" href="/pages/register.html">??</a>
    </div>
  </div>

  <section class="panel" style="max-width: 860px; margin: 0 auto;">
    <h2 style="font-size: 26px;">PHP ????</h2>
    <p style="color: var(--muted); margin-top: 10px;">??????? PHP ?????</p>
    <div class="grid" style="margin-top: 18px;">
      <div class="card"><h3>????</h3><p><?php echo htmlspecialchars($time, ENT_QUOTES, 'UTF-8'); ?></p></div>
      <div class="card"><h3>PHP ??</h3><p><?php echo htmlspecialchars($version, ENT_QUOTES, 'UTF-8'); ?></p></div>
    </div>
  </section>
</div>
<script src="/assets/js/nav-auth.js"></script>
</body>
</html>