# Servo Driver 사용법  

1. 시작할 때 Servo_Init() 호출
2. 브레이크를 걸 때 break_on() 호출
3. 브레이크를 해제할 때 break_off() 호출
4. 메인 루프나 주기 함수에서 break_update()를 계속 호출
5. 현재 상태 확인이 필요하면 get_break_state() 사용

```
Servo_Init();

while (1)
{
    if (조건)
        break_on();
    else
        break_off();

    break_update();
}
```