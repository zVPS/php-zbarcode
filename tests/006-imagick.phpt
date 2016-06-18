--TEST--
Check whether imagick integration works
--EXTENSIONS--
imagick
--SKIPIF--
<?php
include dirname(__FILE__) . "/skipif.inc.php";

if (!extension_loaded('imagick')) {
	die("Skip Imagick extension is not loaded");
}

if (ZBarCode::HAVE_IMAGICK !== true) {
	die("Skip Imagick support not enabled");
}

?>
--FILE--
<?php

$scanner = new ZBarCodeScanner();

$image1 = new Imagick(dirname(__FILE__) . "/ean13.jpg");
$image2 = new ZBarCodeImage(dirname(__FILE__) . "/ean13.jpg");

$scanned_image1 = $scanner->scan($image1);
$scanned_image2 = $scanner->scan($image2);

// Allow different quality values.
unset($scanned_image1[0]['quality']);
unset($scanned_image2[0]['quality']);

var_dump(array_diff($scanned_image1[0], $scanned_image2[0]));

?>
--EXPECT--
array(0) {
}