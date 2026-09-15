```php
<?php

// فقط POST قبول شود
if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    http_response_code(405);
    echo "METHOD_NOT_ALLOWED";
    exit;
}

// دریافت اطلاعات خام تصویر
$image = file_get_contents("php://input");

if ($image === false || strlen($image) == 0) {
    http_response_code(400);
    echo "NO_IMAGE";
    exit;
}

// محدودیت حجم: 2 MB
if (strlen($image) > 2 * 1024 * 1024) {
    http_response_code(413);
    echo "IMAGE_TOO_LARGE";
    exit;
}

// نام فایل بر اساس تاریخ و ساعت
$filename =
    "photo_" .
    date("Ymd_His") .
    "_" .
    uniqid() .
    ".jpg";

$path =
    __DIR__ .
    "/images/" .
    $filename;

// ذخیره عکس
if (file_put_contents($path, $image) === false) {
    http_response_code(500);
    echo "SAVE_ERROR";
    exit;
}

// پاسخ به ESP32
echo "OK:" . $filename;

?>
```
