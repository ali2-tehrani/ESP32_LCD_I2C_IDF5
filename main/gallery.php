<?php

$dir = __DIR__ . "/images";

$files = glob($dir . "/*.jpg");

usort($files, function ($a, $b) {
    return filemtime($b) - filemtime($a);
});

?>

<!DOCTYPE html>
<html lang="fa">
<head>
    <meta charset="UTF-8">
    <title>آخرین عکس ESP32</title>

    <style>
        body {
            font-family: Tahoma, Arial;
            text-align: center;
            direction: rtl;
            background: #f2f2f2;
        }

        h1 {
            margin-top: 30px;
        }

        img {
            max-width: 90%;
            max-height: 600px;
            border: 3px solid #333;
            border-radius: 10px;
        }

        .info {
            margin: 20px;
            font-size: 18px;
        }
    </style>
</head>

<body>

<h1>آخرین عکس ارسال شده از ESP32</h1>

<?php if (count($files) > 0): ?>

    <?php
        $latest = $files[0];
        $filename = basename($latest);
        $timestamp = filemtime($latest);
    ?>

    <img
        src="images/<?php echo htmlspecialchars($filename); ?>?t=<?php echo $timestamp; ?>"
        alt="آخرین عکس"
    >

    <div class="info">
        نام فایل:
        <?php echo htmlspecialchars($filename); ?>
        <br><br>

        زمان ارسال:
        <?php echo date("Y-m-d H:i:s", $timestamp); ?>
    </div>

<?php else: ?>

    <h2>هنوز هیچ عکسی ارسال نشده است.</h2>

<?php endif; ?>

</body>
</html>
