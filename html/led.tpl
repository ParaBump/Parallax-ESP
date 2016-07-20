<html>
<head>
  <title>Test</title>
  <meta content="width=device-width, initial-scale=1" name="viewport">
  <link rel="stylesheet" type="text/css" href="style.css">
</head>
<body>
<div id="page">
  <div id="header">
    <h1>The LED</h1>
  </div>
  <div id="main" class="clearfix">
    <div id="content">
      <p>
        If there's a LED connected to GPIO2, it's now %ledstate%. You can change that using the buttons below.
      </p>
      <form method="post" action="led.cgi">
        <input type="submit" name="led" value="1">
        <input type="submit" name="led" value="0">
      </form>
    </div>
    <nav id='navigation'>
      <input type="checkbox" id="resp" /><label for="resp"></label>
      <ul>
        <li><a href="index.tpl">Home</a></li>
        <li><a href="wifi/wifi.tpl">Networks</a></li>
        <li><a href="update-ffs.html">Files</a></li>
        <li><a href="settings.html">Settings</a></li>
        <li><a href="flash/index.html">Firmware</a></li>
      </ul>
    </nav>
  </div>
  <div id="ack"></div>
  <div id="footer">
    <a href="https://www.parallax.com">
      <img src="logo.png">
    </a>
  </div>
</div>

</body>
</html>
