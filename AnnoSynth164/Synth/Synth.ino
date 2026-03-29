void setup()
{
    Serial.begin(115200);
    delay(2000);
}

void loop()
{
    Serial.println("Hello World vom ESP32-P4!");
    delay(1000);
}