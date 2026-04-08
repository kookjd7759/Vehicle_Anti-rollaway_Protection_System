<div align="center">

# VAPS (Vehicle Anti-rollaway Protection System)

### 무인 이동 방지 자동 제어 시스템

<p>
  <img src="https://img.shields.io/badge/Infineon-TC375-005B95?style=for-the-badge" alt="TC375" />
  <img src="https://img.shields.io/badge/Infineon-TC275-0B7285?style=for-the-badge" alt="TC275" />
  <img src="https://img.shields.io/badge/Raspberry%20Pi-4-C51A4A?style=for-the-badge" alt="Raspberry Pi 4" />
</p>
<p>
  <img src="https://img.shields.io/badge/%EC%B4%9D%20%ED%85%8C%EC%8A%A4%ED%8A%B8%20%EC%88%98-373-111827?style=for-the-badge" alt="총 테스트 수 373" />
  <img src="https://img.shields.io/badge/Pass-361-15803D?style=for-the-badge" alt="Pass 361" />
  <img src="https://img.shields.io/badge/Fail-3-B91C1C?style=for-the-badge" alt="Fail 3" />
  <img src="https://img.shields.io/badge/N%2FA-9-7C3AED?style=for-the-badge" alt="N/A 9" />
</p>
<a href="https://youtube.com/watch?v=FU-7IzhxiU4&feature=youtu.be" target="_blank">
    <img src="https://img.shields.io/badge/YouTube-Demo-red?logo=youtube&logoColor=white&style=for-the-badge" alt="Youtube" />
  </a>
</div>

---

## Contributors

<div align="center">
<table>
  <tr>
    <td align="center">
      <a href="https://github.com/Gon0304">
        <img src="https://github.com/Gon0304.png" width="100px;" alt="김태곤"/>
        <br />
        <sub><b>김태곤</b></sub>
      </a>
      <br />
      ACT
    </td>
    <td align="center">
      <a href="https://github.com/kookjd7759">
        <img src="https://github.com/kookjd7759.png" width="100px;" alt="국동균"/>
        <br />
        <sub><b>국동균</b></sub>
      </a>
      <br />
      RPI
    </td>
    <td align="center">
      <a href="https://github.com/LSA31">
        <img src="https://github.com/LSA31.png" width="100px;" alt="이승아"/>
        <br />
        <sub><b>이승아</b></sub>
      </a>
      <br />
      MAIN
    </td>
    <td align="center">
      <a href="https://github.com/cenway">
        <img src="https://github.com/cenway.png" width="100px;" alt="윤한준"/>
        <br />
        <sub><b>윤한준</b></sub>
      </a>
      <br />
      CLU
    </td>
    <td align="center">
      <a href="https://github.com/chohabin">
        <img src="https://github.com/chohabin.png" width="100px;" alt="조하빈"/>
        <br />
        <sub><b>조하빈</b></sub>
      </a>
      <br />
      MAIN
    </td>
  </tr>
</table>
</div>

---


## 1. 프로젝트 소개

> VAPS는 **운전자의 부재를 감지하고 차량의 위험 상태를 판단하여, 경고와 자동 제동으로 차량의 무인 이동을 방지하는 시스템**입니다.  
> 주차 또는 정차 상황에서 운전자가 차량을 완전히 안전한 상태로 두지 않고 하차했을 때 발생할 수 있는 사고를 줄이기 위해 개발했습니다.

<table> <tr> <td>
      특히 다음과 같은 위험 상황을 대상으로 합니다.
      <br /><br />
      - D/R단 상태에서 하차 시도<br />
      - N단 상태에서 차량 밀림 위험 발생<br />
      - 운전자 부재 상태에서 차량 이동 발생<br />
      - 경고 및 제어 이력의 기록과 모니터링 필요 상황
    </td> </tr> </table>

---

## 2. 프로젝트 목표
- 운전자 하차 후 차량 오동작 사고 예방
-  위험 상황의 신속한 감지 및 판단
- 경고 및 자동 제동을 통한 즉각 대응
- 운전자, 보행자, 주변 차량의 안전 확보
- 실시간 모니터링 및 사고 기록 체계 구축

---

## 3. 주요 기능

| 구분 | 내용 |
| --- | --- |
| **3-1. 운전자 부재 감지** | **도어 상태 감지**: 운전석 도어의 열림/닫힘 상태를 실시간으로 감지<br/>**시트 착좌 감지**: 압력 센서를 통해 운전자 탑승 여부 확인<br/>**ToF / 초음파 보완 감지**: 단일 센서 오작동을 줄이기 위해 거리 센서 기반 교차 검증 수행 |
| **3-2. 차량 이동 감지 및 위험 판단** | **엔코더 기반 바퀴 회전 감지**로 차량 이동 여부 확인<br/>**IMU 가속도 기반 이상 움직임 감지**<br/>현재 **기어 상태(P/R/N/D)** 와 이동 상태를 종합해 위험 수준 산출 |
| **3-3. 자동 제동 제어** | 운전자 부재 + 차량 이동 조건에서 **자동 제동 개입**<br/>**FORCE / HOLD 제어**로 강한 제동 후 정지 상태 유지<br/>운전자 착석 + 도어 닫힘 조건 충족 시에만 안전하게 해제 |
| **3-4. 상태 표시 및 모니터링** | **LCD**를 통한 차량 상태 / 경고 / 제동 상태 표시<br/>**LED / 부저 / 음성 알림**을 통한 시청각 경고<br/>**Raspberry Pi 기반 모니터링 시스템**으로 이벤트 수신, 저장, 조회, 웹 표시 지원<br/>경고 및 제어 이력을 기록하여 사후 분석 가능 |

---

## 프로젝트 시연

### 모듈 별 기능

<div align="center">
  <table>
    <tr>
      <td align="center" width="50%">
        <img src="https://github.com/user-attachments/assets/b05058ea-9dbb-491b-bc40-56e8bcf4767a" width="100%" alt="모듈 별 기능 1" />
      </td>
      <td align="center" width="50%">
        <img src="https://github.com/user-attachments/assets/5d2bcb48-3fa2-4528-b4e8-e230ee358ad8" width="100%" alt="모듈 별 기능 2" />
      </td>
    </tr>
    <tr>
      <td align="center" width="50%">
        <img src="https://github.com/user-attachments/assets/b6659007-8f73-41ff-95cc-3f60c522eee3" width="100%" alt="모듈 별 기능 3" />
      </td>
      <td align="center" width="50%">
        <img src="https://github.com/user-attachments/assets/c855f3de-2776-4ace-95bb-d466af995c52" width="100%" alt="모듈 별 기능 4" />
      </td>
    </tr>
  </table>
</div>

### 시연 영상

---

<div align="center">

[![YouTube Demo](https://img.shields.io/badge/YouTube-Demo-red?logo=youtube&logoColor=white&style=for-the-badge)](https://youtube.com/watch?v=FU-7IzhxiU4&feature=youtu.be)

</div>

---

## 4. 시스템 구성

본 시스템은 4개의 주요 ECU/보드로 구성됩니다.

- **MAIN (TC375)** - 운전자 존재 판정, 위험 판단, 제어 명령 생성
- **ACT (TC375)** - 모터 구동, 브레이크 동작, 차량 속도 계산
- **CLU (TC275)** - LCD / LED / 부저를 통한 경고 및 상태 표시
- **RPi (Raspberry Pi 4)** - UART 기반 이벤트 수신, 로그 저장(SQLite), 웹 모니터링(Flask), LED / MP3 알림

### 시스템 설계도

<p align="center">
  <img src="https://github.com/user-attachments/assets/842e7c62-c840-4845-8f94-0df7ce30e2a6" width="90%" alt="시스템 설계도" />
</p>

### 전체 시스템 아키텍처

> VAPS를 구성하는 보드, 센서, 출력 장치의 전체 구조와 역할을 나타낸다.

<p align="center">
  <img src="https://github.com/user-attachments/assets/04e6e478-64e8-4e7c-9d1b-3021b37a11eb" width="90%" alt="전체 시스템 아키텍처" />
</p>

### 네트워크 아키텍처

> ECU 간 제어·상태 데이터가 CAN과 UART를 통해 전달되는 통신 구조를 나타낸다.

<p align="center">
  <img src="https://github.com/user-attachments/assets/567ec0e9-eb97-4dd8-80f1-cb83f7634447" width="90%" alt="네트워크 아키텍처" />
</p>

---

## 5. 개발 포인트

- **V-Model 기반 개발 프로세스**를 적용해 요구사항 정의부터 테스트까지 단계적으로 진행 |
- 사용자 요구사항을 세분화하여 **36개의 세부 요구사항** 정의 |
- 보드별로 정리한 **총 90개의 시스템 요구사항** 기반 설계 |
- CAN / UART 인터페이스 명세 및 ECU 간 역할 분리 |
- Fail-safe와 예외 상황 대응을 고려한 구조 설계 |

---

## 6. 테스트 결과

<div align="center">

| **총 테스트 수** | **Pass** | **Fail** | **N/A** |
| --- | --- | --- | --- |
| **373** | **361** | **3** | **9** |

</div>

단위 테스트, 통합 테스트, 시스템 테스트, 인수 테스트까지 수행하며 전체 시스템 흐름을 검증했습니다.

---

## 7. 담당 역할

<p align="center">
  <img src="https://github.com/user-attachments/assets/13cc5e82-236a-489f-9eeb-3ecb69ee55e0" width="90%" alt="담당 역할" />
</p>

<table>
  <tr>
    <td width="50%" valign="top">
      <strong>김태곤</strong><br /><br />
      - 보드: <strong>TC375 ACT ECU</strong><br />
      - 담당: <strong>차량 구동 및 제동 실행부</strong><br />
      - 센서/장치: 엔코더 DC 모터, 서보 모터, 가속도 센서(IMU)<br />
      - 기능: MAIN ECU 명령 수신, 모터 제어 및 브레이크 상태 반영, 차량 속도 및 센서 데이터 송신, 실행부 Fail-safe 처리<br />
      - 비고: ACT ECU에서 제어 명령을 받아 실제 차량 동작으로 연결되는 실행 계층 구현, 통신 이상 시 안전하게 동작하도록 예외 상황 처리까지 고려
    </td>
    <td width="50%" valign="top">
      <strong>국동균</strong><br /><br />
      - 보드: <strong>Raspberry Pi ECU</strong><br />
      - 담당: <strong>모니터링 시스템(MON)</strong><br />
      - 센서/장치: MP3 모듈, RGB LED, UART 수신부, SQLite / Flask Web Server<br />
      - 기능: UART 이벤트 수신 및 해석, 이벤트 로그 저장 및 조회, 웹 모니터링 화면 구현, LED / 음성 알림 연동<br />
      - 비고: 상태 확인과 이벤트 기록이 가능한 MON 프로그램 구현, UART, 로그 저장, 웹 화면, LED/음성 알림까지 핵심 흐름 완성
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <strong>이승아, 조하빈</strong><br /><br />
      - 보드: <strong>TC375 MAIN ECU</strong><br />
      - 담당: <strong>메인 제어 로직</strong><br />
      - 센서/장치: 압력 센서, 초음파 센서, ToF 센서, 도어 스위치, 기어 스위치, RTOS<br />
      - 기능: 센서 데이터 수집 및 처리, 운전자 존재 여부 판단, 위험 상황 판별, CAN 통신 기반 제어 흐름 구현<br />
      - 비고: 센서 처리와 CAN 통신을 중심으로 MAIN ECU 기능 구현에 참여, 전체 툴 작성까지 담당하며 시스템 연결 안정화에 기여
    </td>
    <td width="50%" valign="top">
      <strong>윤한준</strong><br /><br />
      - 보드: <strong>TC275 CLU ECU</strong><br />
      - 담당: <strong>경고 및 상태 표시(HMI)</strong><br />
      - 센서/장치: LCD, LED, 부저<br />
      - 기능: 차량 상태 표시, 위험 상태 표시, 경고 출력, UART 로그 전송<br />
      - 비고: 부저, LED, LCD를 통해 사용자에게 실시간 정보 제공, 계획한 동작을 모두 수행했고 표시 계층 구현을 완성
    </td>
  </tr>
</table>

---

## 8. 기술 스택

| Hardware | Software | Tools |
| --- | --- | --- |
| Infineon **TC375**, **TC275**<br />**Raspberry Pi 4**<br />압력 센서, ToF 센서, 초음파 센서<br />엔코더 DC 모터, 서보 모터, IMU 센서<br />LCD, LED, 부저, MP3 모듈 | **C / C++**<br />**Python 3**<br />**Flask**<br />**SQLite**<br />**CAN / UART 통신** | **AURIX IDE**<br />**VS Code**<br />**GitHub**<br />**Jira / Confluence** |

---

## 9. 프로젝트 의의  
VAPS는 단순 경고 시스템이 아니라,  
**운전자 부재 감지 → 위험 판단 → 경고 → 자동 제동 → 기록 및 모니터링**까지 이어지는 전체 안전 제어 흐름을 구현한 프로젝트입니다.

특히 임베디드 환경에서 센서, 통신, 제어, 사용자 알림, 로깅, 웹 모니터링을 하나의 시스템으로 통합해 본 경험이라는 점에서 의미가 있습니다.

---

## 10. 아쉬웠던 점 및 개선 방향

- 통신 오류 및 예외 상황 대응 보완 필요
- 로그 조회 / 필터링 기능 고도화 필요
- 모니터링 화면의 직관성 개선 필요
- 실제 동작 타이밍과 일부 예외 케이스 추가 검증 필요
- 라이브러리 오버헤드 및 성능 최적화 필요
- 

### 4-1. 저장소 구조

| 디렉터리 | 설명 |
| --- | --- |
| **MAIN/** | TC375 기반 MAIN ECU 프로젝트입니다. 센서 입력 수집, 운전자 이탈 및 위험 상태 판단, 상태 머신 기반 제어 명령 생성, CAN 통신 로직이 포함되어 있으며 실제 애플리케이션 코드는 `iLLD_TC375_ADS_FreeRTOS_Basic/App/` 아래에 정리되어 있습니다. |
| **ACT/** | TC375 기반 ACT ECU 프로젝트입니다. MAIN ECU 명령을 수신해 구동 모터와 브레이크를 제어하고, 엔코더와 IMU 데이터를 이용해 차량 상태를 피드백합니다. `Servo_driver/`, `encoder_moter/` 폴더에는 구동부 단위 테스트 및 주변장치 제어 코드가 포함되어 있습니다. |
| **CLU/** | TC275 기반 CLU ECU 프로젝트입니다. LCD, LED, 부저를 통해 경고와 차량 상태를 표시하고, CAN으로 전달받은 정보를 사용자에게 보여주는 HMI 역할을 담당합니다. `UART_driver/`와 `Utility/`에는 UART 전송 및 LCD 관련 보조 코드가 포함되어 있습니다. |
| **RPI/** | Raspberry Pi 기반 모니터링 파트입니다. `MON/` 폴더를 중심으로 Flask 웹 대시보드, SQLite 이벤트 로그 저장, UART 이벤트 수신 및 파싱, RGB LED 경고 출력, MP3 음성 알림 기능을 제공합니다. |
| **can_example/** | TC375 보드 기준 CAN 송수신 동작을 별도로 확인하기 위한 예제 코드 모음입니다. 송신(`can_tx`), 수신(`can_rx`), 응답 보드 테스트(`TC375_CAN`) 예제가 포함되어 있습니다. |
| **tof_test/** | VL53L0X ToF 거리 센서 단독 동작을 검증하기 위한 테스트 폴더입니다. 센서 연동용 샘플 코드와 벤더 API가 포함되어 있어 MAIN ECU 통합 전 거리 측정 기능을 개별 확인할 수 있습니다. |
