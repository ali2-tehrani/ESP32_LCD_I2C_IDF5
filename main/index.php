<?php

// =====================================================
// ESP32 CONFIG
// =====================================================

$ESP32_URL = "http://10.171.40.137";


// =====================================================
// PHP PROXY FUNCTION
// =====================================================

function esp32_request($path, $method = "GET")
{
    global $ESP32_URL;

    $url = $ESP32_URL . $path;

    $options = [
        "http" => [
            "method"        => $method,
            "timeout"       => 10,
            "ignore_errors" => true
        ]
    ];

    $context = stream_context_create($options);

    $result = @file_get_contents($url, false, $context);

    if ($result === false) {

        http_response_code(502);

        header("Content-Type: application/json; charset=utf-8");

        echo json_encode([
            "ok"      => false,
            "error"   => "ESP32_CONNECTION_ERROR",
            "message" => "ارتباط با ESP32 برقرار نشد"
        ]);

        exit;
    }

    return $result;
}


// =====================================================
// API PROXY
// =====================================================

if (isset($_GET["api"])) {

    $api = $_GET["api"];

    header("Content-Type: application/json; charset=utf-8");


    // -------------------------------------------------
    // TIME
    // -------------------------------------------------

    if ($api === "time") {

        echo esp32_request("/api/time");
        exit;
    }


    // -------------------------------------------------
    // STATUS
    // -------------------------------------------------

    if ($api === "status") {

        echo esp32_request("/api/status");
        exit;
    }


    // -------------------------------------------------
    // DATABASE
    // -------------------------------------------------

    if ($api === "db") {

        echo esp32_request("/api/db");
        exit;
    }


    // -------------------------------------------------
    // LED
    // -------------------------------------------------

    if ($api === "led") {

        $state = isset($_GET["state"])
               ? intval($_GET["state"])
               : 0;

        if ($state !== 0 && $state !== 1) {

            http_response_code(400);

            echo json_encode([
                "ok" => false,
                "message" => "Invalid LED state"
            ]);

            exit;
        }

        echo esp32_request(
            "/api/led?state=" . $state
        );

        exit;
    }


    // -------------------------------------------------
    // RESET
    // -------------------------------------------------

    if ($api === "reset") {

        echo esp32_request(
            "/api/reset"
        );

        exit;
    }


    // -------------------------------------------------
    // SEND PHOTO
    // -------------------------------------------------

    if ($api === "photo_send") {

        echo esp32_request(
            "/api/photo/send",
            "POST"
        );

        exit;
    }


    // -------------------------------------------------
    // UNKNOWN API
    // -------------------------------------------------

    http_response_code(404);

    echo json_encode([
        "ok" => false,
        "message" => "Unknown API"
    ]);

    exit;
}


// =====================================================
// FIND LATEST PHOTO
// =====================================================

$images_dir = __DIR__ . "/images";

$latest_photo = null;
$latest_time  = 0;

if (is_dir($images_dir)) {

    $files = glob(
        $images_dir . "/*.jpg"
    );

    if ($files !== false) {

        foreach ($files as $file) {

            $time = filemtime($file);

            if ($time > $latest_time) {

                $latest_time = $time;

                $latest_photo =
                    basename($file);
            }
        }
    }
}

?>

<!DOCTYPE html>

<html lang="en">

<head>

    <meta charset="UTF-8">

    <meta name="viewport"
          content="width=device-width, initial-scale=1.0">

    <title>ESP32 Web Server</title>


    <style>

        body {

            font-family: Arial;

            text-align: center;

            margin-top: 50px;

        }


        button {

            padding: 12px 25px;

            margin: 5px;

            font-size: 18px;

            cursor: pointer;

        }


        .value {

            font-size: 32px;

            font-weight: bold;

        }


        .reset {

            margin-top: 25px;

            padding: 12px 30px;

            font-size: 18px;

        }


        #photoStatus {

            font-size: 18px;

            margin-top: 15px;

        }


        .photo-box {

            margin-top: 30px;

        }


        .photo-box img {

            max-width: 90%;

            max-height: 500px;

            border: 2px solid #333;

        }

    </style>

</head>


<body>


    <h1>ESP32 Web Server</h1>


    <!-- ========================================= -->
    <!-- TIME -->
    <!-- ========================================= -->

    <h2>Current Time</h2>

    <div id="time"
         class="value">

        --:--:--

    </div>


    <!-- ========================================= -->
    <!-- LED -->
    <!-- ========================================= -->

    <h2>LED</h2>

    <div>

        <button onclick="setLED(1)">
            LED ON
        </button>

        <button onclick="setLED(0)">
            LED OFF
        </button>

    </div>


    <p>

        LED Status:

        <span id="led">
            --
        </span>

    </p>


    <!-- ========================================= -->
    <!-- DATABASE -->
    <!-- ========================================= -->

    <h2>Database</h2>


    <p>

        Date:

        <span id="db_date"
              class="value">

            --

        </span>

    </p>


    <p>

        Time:

        <span id="db_time"
              class="value">

            --

        </span>

    </p>


    <p>

        LED:

        <span id="db_led"
              class="value">

            --

        </span>

    </p>


    <p id="db_status">

        Database: --

    </p>


    <!-- ========================================= -->
    <!-- ESP32 RESET -->
    <!-- ========================================= -->

    <h2>ESP32</h2>


    <button class="reset"
            onclick="resetESP32()">

        ESP32 RESET

    </button>


    <p id="status">

        Ready

    </p>


    <!-- ========================================= -->
    <!-- PHOTO -->
    <!-- ========================================= -->

    <h2>Camera</h2>


    <button onclick="sendPhoto()">

        📷 ارسال عکس

    </button>


    <p id="photoStatus"></p>


    <!-- ========================================= -->
    <!-- LATEST PHOTO -->
    <!-- ========================================= -->

    <?php if ($latest_photo !== null): ?>

        <div class="photo-box">

            <h2>
                آخرین عکس
            </h2>

            <img
                src="images/<?php
                    echo htmlspecialchars(
                        $latest_photo
                    );
                ?>?t=<?php
                    echo $latest_time;
                ?>"
                alt="Latest Photo"
            >

            <p>

                <?php
                echo htmlspecialchars(
                    $latest_photo
                );
                ?>

            </p>

        </div>

    <?php endif; ?>


    <!-- ========================================= -->
    <!-- JAVASCRIPT -->
    <!-- ========================================= -->

    <script>


        // =========================================
        // SEND PHOTO
        // =========================================

        async function sendPhoto()
        {

            const status =
                document.getElementById(
                    "photoStatus"
                );


            status.innerText =
                "در حال ارسال عکس...";


            try
            {

                const response =
                    await fetch(
                        "index.php?api=photo_send",
                        {
                            method: "POST"
                        }
                    );


                if (!response.ok)
                {

                    throw new Error(
                        "HTTP Error " +
                        response.status
                    );

                }


                const result =
                    await response.json();


                if (result.ok === true)
                {

                    status.innerText =
                        "✅ عکس با موفقیت ارسال شد";


                    // بعد از ذخیره عکس،
                    // صفحه را دوباره بارگذاری می‌کنیم
                    setTimeout(
                        function()
                        {
                            location.reload();
                        },
                        1000
                    );

                }
                else
                {

                    status.innerText =
                        "❌ ارسال عکس ناموفق بود";

                    console.log(result);

                }

            }
            catch(error)
            {

                console.log(
                    "Photo error:",
                    error
                );


                status.innerText =
                    "❌ خطا در ارتباط با ESP32";

            }

        }


        // =========================================
        // DATABASE
        // =========================================

        async function updateDatabase()
        {

            try
            {

                const response =
                    await fetch(
                        "index.php?api=db"
                    );


                if (!response.ok)
                {

                    throw new Error(
                        "HTTP Error " +
                        response.status
                    );

                }


                const data =
                    await response.json();


                if (data.error)
                {

                    document.getElementById(
                        "db_status"
                    ).textContent =
                        "Database: No data";

                    return;

                }


                document.getElementById(
                    "db_date"
                ).textContent =
                    data.date || "--";


                document.getElementById(
                    "db_time"
                ).textContent =
                    data.time || "--";


                document.getElementById(
                    "db_led"
                ).textContent =

                    Number(data.led) === 1
                    ? "ON"
                    : "OFF";


                document.getElementById(
                    "db_status"
                ).textContent =
                    "Database: Connected";

            }
            catch(error)
            {

                console.log(
                    "Database error:",
                    error
                );


                document.getElementById(
                    "db_status"
                ).textContent =
                    "Database: ERROR";

            }

        }


        // =========================================
        // TIME
        // =========================================

        async function updateTime()
        {

            try
            {

                const response =
                    await fetch(
                        "index.php?api=time"
                    );


                if (!response.ok)
                {

                    throw new Error(
                        "HTTP Error " +
                        response.status
                    );

                }


                const data =
                    await response.json();


                document.getElementById(
                    "time"
                ).textContent =
                    data.time;

            }
            catch(error)
            {

                console.log(
                    "Time error:",
                    error
                );


                document.getElementById(
                    "time"
                ).textContent =
                    "ERROR";

            }

        }


        // =========================================
        // SET LED
        // =========================================

        async function setLED(state)
        {

            try
            {

                const response =
                    await fetch(
                        "index.php?api=led&state=" +
                        state
                    );


                if (!response.ok)
                {

                    throw new Error(
                        "LED HTTP Error " +
                        response.status
                    );

                }


                updateLED();

            }
            catch(error)
            {

                console.log(
                    "LED error:",
                    error
                );

            }

        }


        // =========================================
        // LED STATUS
        // =========================================

        async function updateLED()
        {

            try
            {

                const response =
                    await fetch(
                        "index.php?api=status"
                    );


                if (!response.ok)
                {

                    throw new Error(
                        "HTTP Error " +
                        response.status
                    );

                }


                const data =
                    await response.json();


                document.getElementById(
                    "led"
                ).textContent =

                    Number(data.led) === 1
                    ? "ON"
                    : "OFF";

            }
            catch(error)
            {

                console.log(
                    "LED status error:",
                    error
                );


                document.getElementById(
                    "led"
                ).textContent =
                    "ERROR";

            }

        }


        // =========================================
        // RESET ESP32
        // =========================================

        async function resetESP32()
        {

            const answer =
                confirm(
                    "Reset ESP32?"
                );


            if (!answer)
                return;


            document.getElementById(
                "status"
            ).textContent =
                "ESP32 is restarting...";


            try
            {

                await fetch(
                    "index.php?api=reset"
                );

            }
            catch(error)
            {

                console.log(error);

            }


            setTimeout(
                function()
                {

                    location.reload();

                },
                3000
            );

        }


        // =========================================
        // INITIAL
        // =========================================

        updateTime();

        updateLED();

        updateDatabase();


        // =========================================
        // PERIODIC UPDATE
        // =========================================

        setInterval(
            updateTime,
            1000
        );


        setInterval(
            updateDatabase,
            5000
        );


    </script>


</body>

</html>