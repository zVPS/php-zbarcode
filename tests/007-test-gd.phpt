--TEST--
Test GD support
--EXTENSIONS--
gd
--SKIPIF--
<?php
include dirname(__FILE__) . "/skipif.inc.php";

if (!extension_loaded('gd')) {
	die("Skip GD extension is not loaded");
}

if (ZBarCode::HAVE_GD !== true) {
	die("Skip GD support not enabled");
}
?>
--FILE--
<?php
$scanner = new ZbarCodeScanner();

$image1 = imagecreatefromjpeg(dirname(__FILE__) . '/ean13.jpg');
$image2 = new ZBarCodeImage(dirname(__FILE__) . "/ean13.jpg");

$scanned_image1 = $scanner->scan($image1);
$scanned_image2 = $scanner->scan($image2);

// Allow different quality values.
unset($scanned_image1[1][0]['quality']);
unset($scanned_image2[0]['quality']);

var_dump(array_diff($scanned_image1[1][0], $scanned_image2[0]));

var_dump($scanned_image1);

?>
--EXPECT--
array(0) {
}
array(1) {
  [1]=>
  array(1) {
    [0]=>
    array(2) {
      ["data"]=>
      string(13) "5901234123457"
      ["type"]=>
      string(6) "EAN-13"
    }
  }
}