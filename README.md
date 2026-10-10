# FreeRTOS-Multitasking-Sensor-Node
Dual-core ESP32 firmware in Embedded C using ESP-IDF. Sensor sampling (10 Hz) runs on Core 0 and telemetry on Core 1. FreeRTOS queues and a mutex handle thread-safe data sharing between the tasks.
Sensor values are simulated; the focus is the RTOS task design.
