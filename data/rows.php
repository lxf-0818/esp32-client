<?php
$servername = "192.168.1.252";

// Your Database name
$dbname = "example_esp_data";
// Your Database user
$username = "example_esp_board";
// Your Database user password
$password = "fullFat2021";
// Create connection
$conn = new mysqli($servername, $username, $password, $dbname);
// Check connection
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
} 

$sql = "SELECT id,  sensor, value3  FROM `SensorData` ";
 
if ($result = $conn->query($sql)) {
    $num_rows = mysqli_num_rows($result);
    echo $num_rows; 
      
    $result->free();
}

$conn->close();
?> 
