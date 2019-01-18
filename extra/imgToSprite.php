<?php
function open_image($file) {
    //detect type and process accordinally
    global $type;
    $size=getimagesize($file);
    switch($size["mime"]){
        case "image/jpeg":
            $im = imagecreatefromjpeg($file); //jpeg file
        break;
        case "image/gif":
            $im = imagecreatefromgif($file); //gif file
      break;
      case "image/png":
          $im = imagecreatefrompng($file); //png file
      break;
    default:
        $im=false;
    break;
    }
    return $im;
}

$sourceImage = open_image($argv[1]); // imagecreatefromjpeg/png/
$image = ImageCreateTrueColor(32, 18);
$w = imagesx($sourceImage);
$h = imagesy($sourceImage);
imagecopyResampled ($image, $sourceImage, 0, 0, 0, 0, 32, 18, $w, $h);
echo "11111\n";
echo "10001\n";

$width = imagesx($image);
$height = imagesy($image);

$rMax = 0;
$gMax = 0;
$bMax = 0;

$rMin = 255;
$gMin = 255;
$bMin = 255;


for ($y = 0; $y < $height; $y++) {

	for ($x = 0; $x < $width; $x++) {
		$rgb = imagecolorat($image, $x, $y);
		$r = ($rgb >> 16) & 0xFF;
		$g = ($rgb >> 8) & 0xFF;
		$b = $rgb & 0xFF;

		if($r > $rMax) $rMax = $r;
		if($g > $gMax) $gMax = $g;
		if($b > $bMax) $bMax = $b;
		
		if($r < $rMin) $rMin = $r;
		if($g < $gMin) $gMin = $g;
		if($b < $bMin) $bMin = $b;
	} 
}

//*
$rThresh = ($rMin + $rMax) / 2;
$gThresh = ($gMin + $gMax) / 2;
$bThresh = ($bMin + $bMax) / 2;
/*/
$rThresh = 127;
$gThresh = 127;
$bThresh = 127;
//*/

for ($y = 0; $y < $height; $y++) {

	for ($x = 0; $x < $width; $x++) {
		$rgb = imagecolorat($image, $x, $y);
		$r = ($rgb >> 16) & 0xFF;
		$g = ($rgb >> 8) & 0xFF;
		$b = $rgb & 0xFF;

		$rBool = $r > $rThresh ? 1 : 0;
		$gBool = $g > $gThresh ? 1 : 0;
		$bBool = $b > $bThresh ? 1 : 0;
		
		echo $rBool . $gBool . $bBool . "\n";
	} 
}



// transparency color (we get it from top left, unlike device format)
$rgb = imagecolorat($sourceImage, 0, 0);
$r = ($rgb >> 16) & 0xFF;
$g = ($rgb >> 8) & 0xFF;
$b = $rgb & 0xFF;

$rBool = $r > $rThresh ? 1 : 0;
$gBool = $g > $gThresh ? 1 : 0;
$bBool = $b > $bThresh ? 1 : 0;

echo $rBool . $gBool . $bBool . "\n";
?>
