#include <stdio.h>

#include "lcd.h"

/* 32자 LCD 버퍼를 16자씩 2줄로 나눠 콘솔에 출력하는 함수 */
void print_lcd(const char* lcd1, const char* lcd2) {
    printf("[LCD1]\n");
    printf(" ----------------\n");
    printf("|%.16s|\n", lcd1);
    printf("|%.16s|\n", lcd1 + 16);
    printf(" ----------------\n");

    printf("[LCD2]\n");
    printf(" ----------------\n");
    printf("|%.16s|\n", lcd2);
    printf("|%.16s|\n", lcd2 + 16);
    printf(" ----------------\n");
}

int main(void) {
    char lcd1[33], lcd2[33];

    /*
     * lcd_make_strings() 테스트
     *
     * 모든 조합을 순회하면서 LCD 문자열이 정상 생성되는지 확인한다.
     *
     * n1: warning  (0~3)
     * n2: brake    (0~3)
     * n3: gear     (0~3)
     * n4: door     (0~1)
     * n5: driver   (0~1)
     * speed: 255 고정
     */
    for (int n1 = 0; n1 < 4; ++n1)
        for (int n2 = 0; n2 < 4; ++n2)
            for (int n3 = 0; n3 < 4; ++n3)
                for (int n4 = 0; n4 < 2; ++n4)
                    for (int n5 = 0; n5 < 2; ++n5) {
                        lcd_make_strings(n1, n2, n3, n4, n5, 255, lcd1, lcd2);
                        print_lcd(lcd1, lcd2);
                    }

    return 0;
}