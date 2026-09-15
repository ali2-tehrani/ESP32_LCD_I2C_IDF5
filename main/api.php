<?php

$host = "localhost";
$user = "root";
$password = "";
$dbname = "esp32_db";

$conn = new mysqli($host, $user, $password, $dbname,55555);

if ($conn->connect_error) {
    die("Database connection failed");
}

/* =========================
   دریافت اطلاعات از ESP32
   ========================= */

if (isset($_GET["date"]) &&
    isset($_GET["time"]) &&
    isset($_GET["led"])) {

    $date = $_GET["date"];
    $time = $_GET["time"];
    $led  = intval($_GET["led"]);

    $sql = "INSERT INTO esp32_data
            (date_value, time_value, led_state)
            VALUES (?, ?, ?)";

    $stmt = $conn->prepare($sql);

    $stmt->bind_param(
        "ssi",
        $date,
        $time,
        $led
    );

    if ($stmt->execute()) {
        echo "OK";
    } else {
        echo "ERROR";
    }

    $stmt->close();
}


/* =========================
   ارسال آخرین اطلاعات به ESP32
   ========================= */

elseif (isset($_GET["get_last"])) {

    $sql = "SELECT date_value,
                   time_value,
                   led_state
            FROM esp32_data
            ORDER BY id DESC
            LIMIT 1";

    $result = $conn->query($sql);

    if ($result && $row = $result->fetch_assoc()) {

        header("Content-Type: application/json");

        echo json_encode([
            "date" => $row["date_value"],
            "time" => $row["time_value"],
            "led"  => intval($row["led_state"])
        ]);

    } else {

        header("Content-Type: application/json");

        echo json_encode([
            "error" => "No data"
        ]);
    }
}

$conn->close();
?>
