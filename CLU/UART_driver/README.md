# UART Driver 사용법  

1. 시작할 때 Driver_Asc0_Init() 호출
2. 데이터 보낼 때 sendData(warning, brake, gear, door, driver, speed) 호출
3. 문자열 직접 보낼 때만 UART_sendText()

```
Driver_Asc0_Init();

sendData(warning, brake, gear, door, driver, speed);
```